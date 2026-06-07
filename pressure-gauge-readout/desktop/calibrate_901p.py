import argparse
import asyncio
import re
import sys

from bleak import BleakClient, BleakScanner


DEVICE_NAME = "PG-XIAO"
SERVICE_UUID = "9b3f0001-7f64-4f76-8f6d-8a2f0b6a4c10"
DATA_UUID = "9b3f0002-7f64-4f76-8f6d-8a2f0b6a4c10"
COMMAND_UUID = "9b3f0003-7f64-4f76-8f6d-8a2f0b6a4c10"

NAK_CODES = {
    "8": "zero/sensor adjustment attempted at too high pressure",
    "9": "atmospheric adjustment attempted at too low pressure",
    "160": "unrecognized message",
    "169": "invalid argument",
    "172": "value out of range",
    "175": "command/query character invalid",
    "180": "transducer is locked; try --unlock if appropriate",
}


def format_pressure(value):
    mantissa, exponent = f"{value:.2E}".split("E")
    return f"{mantissa}E{int(exponent):+d}"


def build_command(address, body):
    return f"@{address:03d}{body};FF"


def parse_response(response):
    match = re.fullmatch(r"@(?P<addr>\d{3})(?P<kind>ACK|NAK)(?P<data>.*);FF", response.strip())
    if not match:
        return None, None
    return match.group("kind"), match.group("data")


def parse_float(text):
    try:
        return float(text)
    except ValueError:
        return None


def require_ack(response):
    kind, data = parse_response(response)
    if kind == "ACK":
        return data
    if kind == "NAK":
        detail = NAK_CODES.get(data, "unknown NAK code")
        raise RuntimeError(f"{response} ({detail})")
    raise RuntimeError(f"Unexpected response: {response}")


def confirm_or_abort(prompt, token, assume_yes):
    if assume_yes:
        return

    print()
    print(prompt)
    answer = input(f"Type {token} to continue: ").strip()
    if answer != token:
        raise SystemExit("Aborted.")


async def find_pressure_monitor(timeout):
    print(f"Scanning for {DEVICE_NAME} service {SERVICE_UUID}...")

    loop = asyncio.get_running_loop()
    found = loop.create_future()
    seen = {}

    def on_advertisement(device, advertisement):
        seen[device.address] = (device, advertisement)
        services = {str(uuid).lower() for uuid in advertisement.service_uuids}
        if SERVICE_UUID in services and not found.done():
            found.set_result((device, advertisement))

    scanner = BleakScanner(on_advertisement)
    await scanner.start()
    try:
        device, advertisement = await asyncio.wait_for(found, timeout=timeout)
    except asyncio.TimeoutError:
        await scanner.stop()
        print("Could not find the XIAO BLE service. Nearby devices:")
        for device, advertisement in seen.values():
            name = advertisement.local_name or device.name or "<no name>"
            services = ", ".join(str(uuid) for uuid in advertisement.service_uuids) or "<no services>"
            print(f"  {name} [{device.address}] RSSI={advertisement.rssi} services={services}")
        return None
    else:
        await scanner.stop()

    name = advertisement.local_name or device.name or "<no name>"
    print(f"Found {name}: {device.address} RSSI={advertisement.rssi}")
    return device


class GaugeLink:
    def __init__(self, client):
        self.client = client
        self.command_responses = asyncio.Queue()

    def on_data(self, sender, data):
        text = data.decode("utf-8", errors="replace").strip()
        if text.startswith("CMD "):
            self.command_responses.put_nowait(text[4:])
        else:
            print(f"pressure notification: {text}")

    async def start(self):
        await self.client.start_notify(DATA_UUID, self.on_data)

    async def command(self, command, timeout=3.0):
        while not self.command_responses.empty():
            self.command_responses.get_nowait()

        print(f"> {command}")
        await self.client.write_gatt_char(COMMAND_UUID, command.encode("ascii"), response=True)

        try:
            response = await asyncio.wait_for(self.command_responses.get(), timeout=timeout)
        except asyncio.TimeoutError:
            raw = await self.client.read_gatt_char(DATA_UUID)
            text = raw.decode("utf-8", errors="replace").strip()
            response = text[4:] if text.startswith("CMD ") else text

        print(f"< {response}")
        return response

    async def query(self, address, query_name):
        response = await self.command(build_command(address, f"{query_name}?"))
        return require_ack(response)

    async def set(self, address, body):
        response = await self.command(build_command(address, body))
        return require_ack(response)


async def diagnostics(link, address):
    print()
    print("Diagnostics")
    values = {}
    for query_name in ("MD", "SN", "PN", "FV", "HV", "U", "GT", "TEM", "T", "PR1", "PR2", "PR3", "PR4", "VAC", "ATM", "ATZ"):
        try:
            value = await link.query(address, query_name)
        except RuntimeError as exc:
            value = f"ERROR: {exc}"
        values[query_name] = value
        print(f"  {query_name}? = {value}")

    status = values.get("T")
    if status == "M":
        print("  WARNING: 901P reports MicroPirani sensor failure.")
    elif status == "Z":
        print("  WARNING: 901P reports Piezo sensor failure.")

    pr1 = parse_float(values.get("PR1", ""))
    pr4 = parse_float(values.get("PR4", ""))
    if pr1 is not None and pr4 is not None and pr1 < 1e-3 and pr4 > 500:
        print("  WARNING: MicroPirani reads vacuum while combined pressure reads atmosphere.")


async def main():
    parser = argparse.ArgumentParser(
        description="Calibrate an MKS 901P through the XIAO BLE UART bridge."
    )
    parser.add_argument("--address", type=int, default=253, help="901P serial address, default 253")
    parser.add_argument("--scan-timeout", type=float, default=60, help="BLE scan timeout in seconds")
    parser.add_argument("--yes", action="store_true", help="Skip interactive safety confirmations")
    parser.add_argument("--unlock", action="store_true", help="Temporarily unlock transducer settings")
    parser.add_argument("--lock", action="store_true", help="Lock transducer settings after changes")
    parser.add_argument("--set-unit-torr", action="store_true", help="Set pressure unit to TORR before calibration")
    parser.add_argument(
        "--set-gas",
        choices=("AIR", "NITROGEN", "ARGON", "HELIUM", "HYDROGEN", "H2O", "NEON", "CO2", "XENON"),
        help="Set the MicroPirani gas curve before calibration",
    )
    parser.add_argument(
        "--atmosphere",
        nargs="?",
        type=float,
        const=760.0,
        help="Run MicroPirani atmospheric adjustment at this Torr value, default 760",
    )
    parser.add_argument(
        "--piezo-zero",
        action="store_true",
        help="Run Piezo differential zero adjustment at atmosphere",
    )
    parser.add_argument(
        "--vacuum-zero",
        action="store_true",
        help="Run MicroPirani zero adjustment at high vacuum",
    )
    parser.add_argument(
        "--zero-pressure",
        type=float,
        help="Optional VAC command target pressure in Torr, valid 1e-5 to 5e-3",
    )
    parser.add_argument(
        "--restore",
        choices=("vac", "atm", "atz"),
        action="append",
        help="Restore one calibration item to factory default; can be repeated",
    )
    args = parser.parse_args()

    if not 1 <= args.address <= 253:
        raise SystemExit("--address must be 1 through 253")

    if args.atmosphere is not None and not 500 <= args.atmosphere <= 780:
        raise SystemExit("--atmosphere must be between 500 and 780 Torr")

    if args.zero_pressure is not None and not 1e-5 <= args.zero_pressure <= 5e-3:
        raise SystemExit("--zero-pressure must be between 1e-5 and 5e-3 Torr")

    device = await find_pressure_monitor(args.scan_timeout)
    if device is None:
        return 2

    async with BleakClient(device) as client:
        link = GaugeLink(client)
        await link.start()

        await diagnostics(link, args.address)

        if args.unlock:
            confirm_or_abort(
                "This disables the 901P setup lock until power cycle or --lock.",
                "UNLOCK",
                args.yes,
            )
            await link.set(args.address, "FD!UNLOCK")

        if args.set_unit_torr:
            await link.set(args.address, "U!TORR")

        if args.set_gas:
            await link.set(args.address, f"GT!{args.set_gas}")

        unit = await link.query(args.address, "U")
        gas = await link.query(args.address, "GT")

        calibration_requested = (
            args.atmosphere is not None
            or args.piezo_zero
            or args.vacuum_zero
            or bool(args.restore)
        )

        if calibration_requested and unit != "TORR":
            raise SystemExit(f"Pressure unit is {unit}; run with --set-unit-torr first.")

        if args.restore:
            for item in args.restore:
                token = f"RESTORE-{item.upper()}"
                confirm_or_abort(
                    f"This restores 901P {item.upper()} calibration to factory default.",
                    token,
                    args.yes,
                )
                await link.set(args.address, f"FD!{item.upper()}")

        if args.atmosphere is not None:
            if gas not in ("AIR", "NITROGEN"):
                raise SystemExit(
                    f"Atmospheric adjustment is only valid for AIR or NITROGEN; current gas is {gas}."
                )
            current_pr4 = parse_float(await link.query(args.address, "PR4"))
            if current_pr4 is not None and abs(current_pr4 - args.atmosphere) > 30:
                print()
                print(
                    f"Warning: PR4 currently reads {current_pr4:g} Torr, "
                    f"but --atmosphere is {args.atmosphere:g} Torr."
                )
                print(
                    "The ATM value should be actual local station pressure at the gauge, "
                    "not sea-level-corrected weather pressure."
                )
            confirm_or_abort(
                "Vent the 901P to stable atmospheric AIR/NITROGEN at the entered pressure "
                f"({args.atmosphere:g} Torr). This changes MicroPirani full-scale calibration.",
                "ATM",
                args.yes,
            )
            await link.set(args.address, f"ATM!{format_pressure(args.atmosphere)}")

        if args.piezo_zero:
            confirm_or_abort(
                "Place the 901P at stable atmospheric pressure. This changes Piezo differential zero.",
                "ATZ",
                args.yes,
            )
            await link.set(args.address, "ATZ!")

        if args.vacuum_zero:
            confirm_or_abort(
                "Evacuate the 901P below 8e-6 Torr and let it stabilize. "
                "This changes MicroPirani low-pressure zero calibration.",
                "VAC",
                args.yes,
            )
            if args.zero_pressure is None:
                await link.set(args.address, "VAC!")
            else:
                await link.set(args.address, f"VAC!{format_pressure(args.zero_pressure)}")

        if args.lock:
            await link.set(args.address, "FD!LOCK")

        await diagnostics(link, args.address)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except KeyboardInterrupt:
        print("Stopped")
        raise SystemExit(130)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)

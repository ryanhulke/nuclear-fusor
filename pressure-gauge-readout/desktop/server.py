import asyncio
from bleak import BleakScanner, BleakClient

device_name = "PG-XIAO"
service_uuid = "9b3f0001-7f64-4f76-8f6d-8a2f0b6a4c10"
data_uuid = "9b3f0002-7f64-4f76-8f6d-8a2f0b6a4c10"


def handle_data(sender, data):
    text = data.decode("utf-8", errors="replace")
    print(f"{sender}: {text}")


async def find_pressure_monitor(timeout=60):
    print(f"Scanning for {device_name} service {service_uuid}...")

    loop = asyncio.get_running_loop()
    found = loop.create_future()
    seen = {}

    def on_advertisement(device, advertisement):
        seen[device.address] = (device, advertisement)
        advertised_services = {str(uuid).lower() for uuid in advertisement.service_uuids}
        if service_uuid in advertised_services:
            if not found.done():
                found.set_result((device, advertisement))

    scanner = BleakScanner(on_advertisement)
    await scanner.start()
    try:
        device, advertisement = await asyncio.wait_for(found, timeout=timeout)
    except asyncio.TimeoutError:
        await scanner.stop()
        print("Could not find the XIAO service.")
        print("Nearby BLE devices seen:")
        for device, advertisement in seen.values():
            name = advertisement.local_name or device.name or "<no name>"
            advertised_services = ", ".join(str(uuid) for uuid in advertisement.service_uuids) or "<no services>"
            print(f"  {name} [{device.address}] RSSI={advertisement.rssi} services={advertised_services}")
        return None
    else:
        await scanner.stop()

    name = advertisement.local_name or device.name or "<no name>"
    print(f"Found {name}: {device.address} RSSI={advertisement.rssi}")
    if advertisement.rssi <= -90:
        print("Signal is very weak. Move the XIAO closer or check its antenna side/orientation if connection fails.")
    return device


async def main():
    device = await find_pressure_monitor()

    if device is None:
        print("Make sure the updated firmware is uploaded and the serial monitor says BLE advertising started: yes.")
        return

    print("Connecting...")

    async with BleakClient(device) as client:
        print("Connected")
        print("Subscribing to notifications...")

        await client.start_notify(data_uuid, handle_data)

        print("Listening. Press Ctrl+C to stop.")

        while True:
            await asyncio.sleep(1)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Stopped")

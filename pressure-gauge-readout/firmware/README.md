# Pressure Gauge Readout Firmware

Firmware for the MKS 901P pressure gauge BLE bridge. The board reads the gauge
over UART and publishes the current pressure value over BLE for the control panel app
and simple diagnostic tools.

## Requirements

- Python 3
- PlatformIO Core
- Seeed XIAO ESP32S3
- MKS 901P pressure transducer connected to the XIAO hardware UART
- BLE-capable host for the control panel or desktop test scripts

Verify PlatformIO:

```powershell
python -m pip install --upgrade platformio
pio --version
```

## Build And Upload

From this directory, build first:

```powershell
pio run
```

Connect the XIAO over USB, then upload:

```powershell
pio run -t upload
```

Open the serial monitor:

```powershell
pio device monitor
```

Serial monitor speed is `115200`.

PlatformIO reads the board, framework, serial speed, and library dependencies
from `platformio.ini`. The first build downloads the ESP32 platform and
libraries, so it needs internet access.

## Wiring

The firmware uses the XIAO board's labeled serial pins:

| XIAO pin | Connects to |
| --- | --- |
| `RX` / GPIO44 | 901P TX |
| `TX` / GPIO43 | 901P RX |

The 901P serial link is configured for `9600` baud.

## Operation

On boot the firmware:

1. Starts USB serial.
2. Opens the 901P UART.
3. Starts BLE advertising as `PG-XIAO`.
4. Queries the 901P every 200 ms with `@253PR4?;FF`.
5. Publishes the parsed pressure value over BLE.

BLE details:

```text
Device name: PG-XIAO
Service UUID: 9b3f0001-7f64-4f76-8f6d-8a2f0b6a4c10
Data UUID:    9b3f0002-7f64-4f76-8f6d-8a2f0b6a4c10
```

Normal payload format:

```text
<pressure> Torr
```

Error payloads:

```text
ERR parse
ERR timeout
```

`ERR parse` means the gauge returned data that did not match the expected 901P
ACK format. `ERR timeout` means no complete `;FF`-terminated response arrived
before the read timeout.

## Desktop BLE Test Script

`pressure-gauge-readout/desktop/server.py` is a lightweight BLE smoke test. It
scans for the pressure gauge bridge, connects, subscribes to notifications, and
prints raw payloads.

Install the Python dependency:

```powershell
python -m pip install bleak
```

Run it from the repo root:

```powershell
python pressure-gauge-readout\desktop\server.py
```

This script is useful for checking BLE advertisement, connection, and payload
format without launching the full control panel.

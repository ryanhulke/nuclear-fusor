# Pressure Gauge Readout

BLE bridge, desktop utilities, and hardware files for reading an MKS 901P
pressure transducer from the control panel app.

## Layout

| Path | Purpose |
| --- | --- |
| `firmware/` | PlatformIO firmware for the Seeed XIAO ESP32S3 BLE pressure bridge. |
| `desktop/server.py` | Simple BLE smoke-test listener for the production pressure payload. |
| `desktop/calibrate_901p.py` | Maintenance script for 901P command/calibration workflows. |
| `hardware/` | KiCad PCB project and mechanical files. |
| `docs/901P-Loadlock-Transducer-Manual.pdf` | Local copy of the 901P transducer manual. |

## Firmware

The firmware uses PlatformIO. If PlatformIO is not installed yet:

```powershell
python -m pip install --upgrade platformio
pio --version
```

Build and upload from the firmware directory:

```powershell
cd pressure-gauge-readout\firmware
pio run
pio run -t upload
```

The production firmware advertises as `PG-XIAO`, queries `@253PR4?;FF`, and
publishes the parsed pressure string over BLE. See `firmware/README.md` for the
full BLE UUIDs and payload format.

## BLE Smoke Test

Use `desktop/server.py` when you only need to verify that the board advertises,
connects, and publishes pressure notifications.

Install the Python dependency for the desktop scripts:

```powershell
python -m pip install bleak
```

Run from the repo root:

```powershell
python pressure-gauge-readout\desktop\server.py
```

The script prints each BLE notification as raw text. It is intentionally small
and does not depend on the control panel app.

## Calibration Utility

`desktop/calibrate_901p.py` is kept as a maintenance utility for direct 901P
command/calibration workflows. It is not required for normal control panel operation.

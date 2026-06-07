# Meter Readouts Firmware

Firmware for the Fusor meter readout board. The board reads high-voltage and
current sense signals through an ADS1115 and publishes filtered production
readings over USB serial and BLE.

## Requirements

- Python 3
- PlatformIO Core
- Seeed XIAO ESP32-C6
- ADS1115 on I2C address `0x48`

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

PlatformIO reads the board, framework, upload speed, serial speed, and library
dependencies from `platformio.ini`. The first build downloads the ESP32 platform
and libraries, so it needs internet access.

## Initialization

On boot the firmware:

1. Starts USB serial and prints `Starting Fusor Meter`.
2. Initializes I2C on the board's default `SDA` and `SCL` pins at 100 kHz.
3. Looks for the ADS1115 at `0x48`.
4. Configures the ADS1115 for `GAIN_SIXTEEN` and `128 SPS`.
5. Starts BLE advertising as `Fusor-Meter`.

If the ADS1115 is not found, the board still advertises over BLE but reports:

```text
ERR ADS1115
```

It retries ADS1115 initialization every 3 seconds and reports
`INFO ADS1115 ready` after recovery.

Boot-time input zeroing is disabled by default. To enable it, set
`autoZeroAtBoot` to `true` in `src/main.cpp` and boot with both inputs held at
their zero condition.

## Operation

Every 250 ms the firmware reads:

- ADS1115 differential channel `0-1` for the high-voltage divider sense input.
- ADS1115 differential channel `2-3` for the current shunt sense input.

The readings are averaged, converted to `kV` and `mA`, filtered, printed to USB
serial, and sent over the BLE data characteristic when a client is connected.

BLE details:

```text
Device name: Fusor-Meter
Service UUID: 9b3f1001-7f64-4f76-8f6d-8a2f0b6a4c10
Data UUID:    9b3f1002-7f64-4f76-8f6d-8a2f0b6a4c10
```

Payload format:

```text
kv=<filtered kV>,ma=<total mA>,sat=<voltage saturated><current saturated>
```

`sat=10` means the voltage ADC input is saturated and the current ADC input is
not.

Calibration constants are near the top of `src/main.cpp`:

```text
hvResistorOhms
voltageSenseOhms
currentShuntOhms
voltageSign
currentSign
```

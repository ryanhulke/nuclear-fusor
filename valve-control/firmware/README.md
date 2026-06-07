# Fusor Valve Stepper Controller

Firmware for digitally controlling the fusor gas metering valve with a Seeed XIAO ESP32-C6, TB6600 stepper driver, and NEMA 17 stepper motor.

The controller tracks valve position in step counts, saves position and calibration values to flash, exposes BLE control for the control panel app, and keeps a USB serial console available for calibration and maintenance.

## Hardware

- Seeed XIAO ESP32-C6
- TB6600 stepper motor driver
- NEMA 17 stepper motor
- 12 V DC power supply, 3-5 A recommended
- Metering valve with motor/coupler mount

Stepper settings:

- Microstepping: 16 microsteps
- Steps per revolution: 3200
- TB6600 current limit: 2 A

## Software Requirements

- Python 3
- PlatformIO Core

Verify PlatformIO:

```powershell
pip install platformio
pio --version
```

PlatformIO reads the board, framework, upload speed, serial speed, partition
layout, and library dependencies from `platformio.ini`. The first build
downloads the ESP32 platform and libraries, so it needs internet access.

## Build, Upload, And Serial

From this directory, build first:

```powershell
pio run
```

Connect the XIAO over USB, then upload:

```powershell
pio run -t upload
```

Open the USB serial console:

```powershell
pio device monitor
```

Serial monitor speed is `115200`.

The serial console is not only for first-time setup. It remains available during normal operation. If the control panel is connected over BLE, you can still plug in USB, open the PlatformIO serial monitor, jog the valve, print status, restore limits, or recalibrate.

## Normal Operation

In normal operation:

1. Power the valve controller.
2. Start the control panel app.
3. The control panel connects over BLE to `Fusor-Valve`.
4. The firmware reports status once per second and after every move.
5. The control panel sends text commands over BLE, such as `MOVETO`, `MOVE`, `CLOSE`, and `OPEN`.

The firmware also accepts the same text commands from USB serial. On USB, type `:` first, then the command, then Enter.

Example:

```text
:STATUS
:MOVETO 1200
:CLOSE
```

## RUN vs CALIBRATE

The firmware has two persisted modes:

- `RUN`: normal clamped operation. Absolute and relative moves are clamped to `[0, openLimitSteps]`.
- `CALIBRATE`: unclamped operation. Use this when finding or correcting the physical closed and open references.

Switch modes from USB serial or BLE:

```text
:MODE RUN
:MODE CALIBRATE
```

Calibration is not a separate firmware or special boot mode. It is always available through USB serial. For normal control-panel-driven use, leave the controller in `RUN`. When you need to recalibrate, connect USB, open `pio device monitor`, switch to `CALIBRATE`, make corrections, then switch back to `RUN`.

Position is saved after every completed move in both modes. The open limit and mode are also saved when changed.

## First-Time Calibration

Use USB serial for calibration.

1. Upload the firmware and open `pio device monitor`.
2. Switch to calibration mode:

   ```text
   :MODE CALIBRATE
   ```

3. Start the vacuum pump.
4. Jog toward closed with `a` or the left arrow while watching pressure.
5. When pressure stops dropping, treat that as the closed reference and press `z`.
6. Jog toward open with `d` or the right arrow.
7. When the valve is open far enough for your system, press `m` to save the open limit.
8. Press `p` or type `:STATUS` and confirm `pos`, `open`, and `mode`.
9. Switch back to clamped normal operation:

   ```text
   :MODE RUN
   ```

After this, the control panel can use the reported `open=<n>` value for Vent and manual jogs remain clamped inside the saved range.

## Recalibration During Normal Use

You can recalibrate without reflashing and without changing how the control panel connects.

1. Leave the controller powered.
2. Connect USB.
3. Open:

   ```text
   pio device monitor
   ```

4. Switch to `CALIBRATE` if you need unclamped jogs:

   ```text
   :MODE CALIBRATE
   ```

5. Correct the position or limit:

   ```text
   :SETPOS <n>
   :SETOPEN <n>
   ```

   Or jog to the references and use `z` / `m`.

6. Switch back to `RUN`:

   ```text
   :MODE RUN
   ```

BLE remains active while doing this. Be careful not to command motion from the control panel and the serial console at the same time.

## Restoring Calibration Without Jogging

If flash was wiped or you already know the calibration values, restore them directly:

```text
:SETOPEN 12800
:SETPOS 7800
:MODE RUN
```

`SETOPEN` requires a positive value. `SETPOS` is clamped if the firmware is currently in `RUN`; switch to `CALIBRATE` first if you need to assert a position outside the current limit.

## USB Serial Commands

Single-character commands execute immediately:

| Key | Effect |
| --- | --- |
| `a` / left arrow | Jog closed |
| `d` / right arrow | Jog open |
| `c` | Move to closed position `0` |
| `o` | Move to the saved open limit |
| `p` | Print status and publish BLE status |
| `z` | Set current position as zero |
| `m` | Mark current position as open limit |
| `[` / `]` | Smaller / bigger jog size |
| `?` | Print help |

Text commands use `:` plus Enter:

```text
:MODE CALIBRATE
:MODE RUN
:SETOPEN <n>
:SETPOS <n>
:MOVETO <n>
:MOVE <delta>
:CLOSE
:OPEN
:STATUS
```

Backspace edits the line buffer. Escape cancels the buffered command.

## BLE Interface

BLE is always active in both `RUN` and `CALIBRATE`.

```text
Device name:    Fusor-Valve
Service UUID:   9b3f2001-7f64-4f76-8f6d-8a2f0b6a4c10
Status UUID:    9b3f2002-7f64-4f76-8f6d-8a2f0b6a4c10  (read + notify)
Command UUID:   9b3f2003-7f64-4f76-8f6d-8a2f0b6a4c10  (write)
```

Status payload:

```text
pos=<n>,open=<n>,mode=<CALIBRATE|RUN>
```

The status characteristic is updated after every move and once per second as a heartbeat.

The command characteristic accepts the text commands listed above without the leading `:`.

Examples:

```text
STATUS
MOVETO 1200
OPEN
CLOSE
MODE RUN
```

## Position Tracking And Limits

The firmware stores:

- current position: `pos`
- open limit: `open`
- mode: `RUN` or `CALIBRATE`

Position is saved after every completed move. With the partition layout pinned in `platformio.ini`, normal firmware uploads preserve NVS, so calibration should survive firmware updates.

Position can still drift from the physical valve if:

- the motor skips steps,
- the coupler slips,
- the valve is turned by hand while the controller is powered off,
- power is lost during a move.

After any of those, reconnect USB and resync with `:SETPOS <n>`, or jog to a known closed reference and press `z`.

## Notes

- In `RUN`, moves are clamped to `[0, openLimitSteps]`.
- In `CALIBRATE`, moves are intentionally unclamped.
- `OPEN` requires `openLimitSteps > 0`; otherwise it leaves the valve in place and republishes status.
- `MOVETO` and `MOVE` are also clamped in `RUN`.
- The stepper driver is enabled automatically while moving.

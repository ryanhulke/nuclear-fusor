# Nuclear Fusor
A [nuclear fusor](https://en.wikipedia.org/wiki/Fusor) is an inertial electrostatic confinement device that uses a strong electric field to ionize deuterium gas and accelerate the ions toward the center of a vacuum chamber. This is accomplished by applying a large negative voltage to an inner metal grid (the cathode) relative to the grounded chamber wall (the anode). The chamber is operated at low pressure so that ions can travel significant distances and gain energy from the electric field before colliding with other particles. Near the center of the device, some ions collide with enough kinetic energy that, although they still cannot classically overcome the strong [Coulomb repulsion force](https://en.wikipedia.org/wiki/Coulomb's_law) between their positively charged nuclei, they can occasionally [tunnel](https://en.wikipedia.org/wiki/Quantum_tunnelling) through the Coulomb barrier and may fuse, releasing energy and producing fusion products such as neutrons. These devices are purely experimental; they are highly energy-inefficient and have no commercial future. If you have to ask, "then why would I build one?", you probably shouldn't.

## Project Description
Hardware, firmware, and desktop software for the fusor. The build employs 3 microcontrollers with Bluetooth Low-Energy (BLE). One interfaces with the pressure gauge and streams pressure readings to the control panel, another monitors the high-voltage and current, and the third actuates the gas metering valve for remote pressure control. The desktop app control panel brings the live readouts and valve controls together, with option to stream your phone camera to your computer so you can view the plasma at a safe distance. You can also replay recorded sensor logs and camera footage in the control panel for post-run analysis, in either standard or mobile view.

<img src="docs/img/plasma.jpg" alt="Control Panel: Mobile View" height="600">

## Core Components
- a vacuum pump
  - https://www.amazon.com/Kozyvacu-Dual-Stage-HVAC-Vacuum-Pump/dp/B01N0SYCL4/ref=sr_1_1?sr=8-1
- a vacuum chamber with a view port
  - source these parts from ebay. Use KF or Conflat fittings.
- a high-voltage feedthrough into the chamber
- an inner grid connected to the feedthrough (ex. protein shaker ball)
- a variable power supply capable of at least -20kV at a few mA
- deuterium gas source (or just air for testing)
- MKS 901P pressure gauge
- pressure gauge readout board ([pressure-gauge-readout\hardware](pressure-gauge-readout/hardware))
-  remote-controlled valve ([valve-control/Parts.md](valve-control/Parts.md))
- ammeter/voltmeter readout board ([meter-readouts\hardware](meter-readouts/hardware))
- NEMA 17 Stepper motor (mounted to a needle valve on the deuterium inlet side) and driver
- (optional) ESP32-C6 microcontroller to control the motor
  - a more efficient design would be to incorporate valve control into the meter readout board's PCB


## Repository Layout

| Path | Purpose |
| --- | --- |
| `control-panel/` | Windows OpenGL control panel for pressure, meter, valve control, sensor logging, camera background, and playback. |
| `meter-readouts/` | ESP32-C6 firmware and KiCad hardware for the high-voltage/current readout board. |
| `pressure-gauge-readout/` | ESP32-S3 firmware, desktop utilities, and KiCad hardware for the MKS 901P pressure gauge BLE bridge. |
| `valve-control/` | ESP32-C6 firmware for the stepper-driven valve controller. |
| `docs/` | Project notes and supporting documentation. |

Generated build outputs, local logs, camera media, reference papers, IDE state, and local tool installs are intentionally ignored.

## Getting Started
First, check out the hardware and parts lists to make sure you have what you need. Then, set up the software toolchain(s) for the components you want to build.

| What | Toolchain | Where to start |
| --- | --- | --- |
| **Control panel** — the Windows desktop app | CMake + Visual Studio C++ build tools + OpenCV | [control-panel/README.md](control-panel/README.md) |
| **Firmware** — the three microcontroller boards | PlatformIO | [Firmware](#firmware) below |

Everything below assumes **PowerShell, run from the repo root**. Both toolchains need:

- Windows 10 or newer
- Git (`git --version` to confirm)

### Control Panel

The control panel is a native OpenGL desktop app for the live readouts, valve control, camera, sensor logging, and playback. Its setup has a few steps that are easy to get wrong (installing the C++ build tools, pointing CMake at OpenCV, and adding OpenCV's DLLs to `PATH` before running), so it is documented in one place rather than split across files:

**Follow [control-panel/README.md](control-panel/README.md) top to bottom.** It takes you through installing the build tools, installing OpenCV, building, and running, in order.

### Firmware

The firmware runs on three Seeed XIAO microcontrollers that talk to the control panel over BLE.

**1. Install the toolchain**

- **Python 3** — install from https://www.python.org/ with "Add Python to PATH" checked.
- **PlatformIO Core** — install with pip, then confirm it is on `PATH`:
  ```powershell
  pip install platformio
  pio --version
  ```
- **USB drivers for the Seeed XIAO boards** — usually automatic on Windows 10+. Install a driver manually only if a plugged-in board never shows up.

**2. Build and flash each board**

Each board is built from its own directory. The three commands are the same every time: `pio run` compiles, `pio run -t upload` flashes the board over USB, and `pio device monitor` opens the serial console.

```powershell
cd meter-readouts\firmware
pio run
pio run -t upload
pio device monitor

cd ..\..\pressure-gauge-readout\firmware
pio run
pio run -t upload
pio device monitor

cd ..\..\valve-control\firmware
pio run
pio run -t upload
pio device monitor
```

**3. Reference**

Per-board behavior and BLE payloads are documented in:

- [meter-readouts/firmware/README.md](meter-readouts/firmware/README.md)
- [pressure-gauge-readout/README.md](pressure-gauge-readout/README.md) and [pressure-gauge-readout/firmware/README.md](pressure-gauge-readout/firmware/README.md)
- [valve-control/firmware/README.md](valve-control/firmware/README.md)

## BLE Devices

| Device | BLE name | Role |
| --- | --- | --- |
| Meter readout | `Fusor-Meter` | Publishes filtered high-voltage/current readings as `kv=<n>,ma=<n>,sat=<bits>`. |
| Pressure gauge | `PG-XIAO` | Publishes the MKS 901P pressure readout as text. |
| Valve control | `Fusor-Valve` | Publishes `pos=<n>,open=<n>,mode=<mode>` and accepts movement commands. |

The control panel discovers these devices automatically by BLE name/service UUID.

## Hardware

KiCad projects are included for the readout boards:

- `meter-readouts/hardware/MeterReadouts.kicad_pro`
- `pressure-gauge-readout/hardware/readoutPCB.kicad_pro`


## License
MIT.

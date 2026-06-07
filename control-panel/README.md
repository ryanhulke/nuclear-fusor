# Fusor Control Panel

Native OpenGL control panel for the fusor viewport, pressure gauge, meter readouts, valve control, sensor logging, camera background, and playback.

## Requirements

- Windows 10 or newer
- Git
- CMake `3.21` or newer
- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload
- OpenCV Windows prebuilt package
- MediaMTX and Moblin on iPhone for the optional SRT camera feed

Verify the basic tools from PowerShell:

```powershell
git --version
cmake --version
```

The first configure downloads GLFW and SimpleBLE through CMake `FetchContent`,
so it needs internet access. GLAD is vendored in `tools/glad`.

## Install Build Tools

Install Visual Studio 2022 Build Tools from Microsoft and select:

```text
Desktop development with C++
```

That workload installs the MSVC compiler, Windows SDK, and MSBuild. If CMake
later reports that no compiler was found, rerun the Visual Studio installer and
confirm this workload is installed.

Install CMake `3.21` or newer and make sure it is available in PowerShell:

```powershell
cmake --version
```

## Install OpenCV

The control panel links against OpenCV through CMake's `find_package(OpenCV)`.
OpenCV can live anywhere on your machine as long as you tell CMake where its
`OpenCVConfig.cmake` file is.

### Option A: Official Prebuilt Windows Package

Download the Windows package from the official OpenCV releases page:

```text
https://github.com/opencv/opencv/releases/latest
```

Download the asset named like:

```text
opencv-4.x.x-windows.exe
```

Run the installer/extractor and choose any install directory. For example:

```text
D:\Libraries\opencv
```

Find the CMake package config:

```powershell
$OpenCVRoot = "D:\Libraries\opencv"
Get-ChildItem $OpenCVRoot -Recurse -Filter OpenCVConfig.cmake | Select-Object FullName
```

Use the folder that directly contains `OpenCVConfig.cmake` as `OpenCV_DIR`.
Common results from the prebuilt package look like:

```text
D:\Libraries\opencv\build
D:\Libraries\opencv\build\x64\vc16\lib
```

If both are present, start with the shorter `...\build` path.

### Option B: Your Own OpenCV Build

If you build OpenCV from source, install it with CMake and use the installed
CMake package directory as `OpenCV_DIR`. For example, if your install contains:

```text
D:\Libraries\opencv-install\x64\vc16\lib\OpenCVConfig.cmake
```

then configure this project with:

```powershell
$OpenCV_DIR = "D:\Libraries\opencv-install\x64\vc16\lib"
```

## Build

From the repo root:

```powershell
$OpenCV_DIR = "D:\Libraries\opencv\build"
cmake -S control-panel -B control-panel\build -DOpenCV_DIR="$OpenCV_DIR"
cmake --build control-panel\build --config Release
```

If you need a clean CMake reconfigure, delete only the generated build folder
and run the same configure command again:

```powershell
Remove-Item control-panel\build -Recurse -Force
cmake -S control-panel -B control-panel\build -DOpenCV_DIR="$OpenCV_DIR"
```

Before running the app, add the matching OpenCV DLL folder to the current
PowerShell session. For the official prebuilt package this is usually under
`build\x64\vc16\bin`:

```powershell
$OpenCV_BIN = "D:\Libraries\opencv\build\x64\vc16\bin"
$env:PATH = "$OpenCV_BIN;$env:PATH"
```

## Run Without Camera

```powershell
& "control-panel\build\Release\Fusor Control Panel.exe"
```

The app scans for the pressure sensor, meter readout, and valve control boards automatically.

If Windows cannot find OpenCV DLLs at startup, confirm the `PATH` command above
was run in the same PowerShell session used to launch the executable.

## Configure Camera URL

The control panel reads the camera URL from:

```text
control-panel/config/config.yaml
```

Default local bridge config:

```yaml
camera_url: "rtsp://127.0.0.1:8554/fusor"
```

This URL is the MediaMTX RTSP readout on the laptop. The phone streams to MediaMTX separately.

Command-line `--camera-url` still works as a temporary override:

```powershell
& "control-panel\build\Release\Fusor Control Panel.exe" --camera-url "rtsp://127.0.0.1:8554/fusor"
```

## Local iPhone Camera Setup

If you have a Mac, you can obviously use that instead of the iPhone + MediaMTX setup. But for Windows users, the following setup is recommended.

```text
iPhone Moblin -> SRT over iPhone hotspot -> laptop MediaMTX -> RTSP 127.0.0.1 -> Fusor Control Panel
```

The phone and laptop do not need a router. The laptop connects directly to the iPhone hotspot.

### 1. Start The iPhone Hotspot

On iPhone:

```text
Settings -> Personal Hotspot -> Allow Others to Join
```

Connect the laptop to that hotspot.

Find the laptop's hotspot IP:

```powershell
ipconfig
```

Look under the `Wi-Fi` adapter.

Use the laptop's actual IPv4 address in the Moblin URL.

### 2. Install And Run MediaMTX

Download MediaMTX from:

- https://github.com/bluenviron/mediamtx/releases/latest

Download the Windows amd64 zip, extract it, then run:

```powershell
.\mediamtx.exe
```

Allow it through Windows Firewall on private networks.

Default MediaMTX ports:

```text
SRT ingest: 8890/udp
RTSP read: 8554/tcp
```

Leave the MediaMTX PowerShell window open.

### 3. Configure Moblin

Install Moblin on iPhone:

- https://apps.apple.com/us/app/moblin/id6466745933
- https://github.com/eerimoq/moblin

Create a custom stream:

```text
Protocol: SRT
URL: srt://<LAPTOP_HOTSPOT_IP>:8890
Stream ID: publish:fusor
```

Recommended video settings:

```text
Codec: H.264 / AVC
Resolution: 1280x720
FPS: 30
Bitrate: 2000-3000 kbps
Keyframe interval: 1 second
```

### 4. Run With Camera

From the repo root:

```powershell
$OpenCV_BIN = "D:\Libraries\opencv\build\x64\vc16\bin"
$env:PATH = "$OpenCV_BIN;$env:PATH"
& "control-panel\build\Release\Fusor Control Panel.exe"
```


## Controls

```text
Left drag: orbit 3D view
Mouse wheel: zoom 3D view
F: toggle slow 3D rotation
R: start/stop sensor logging
V: show/hide camera background
A/D: jog valve closed/open in manual mode
Esc: quit
```

The valve panel provides `EVACUATE`, `MANUAL`, and `VENT`. `VENT` opens the valve to its reported open limit and is blocked while the meter reports power present.

When the camera is live and visible, the 3D fusor diagram is hidden. If the camera is turned off with `V`, disconnected, or stale, the 3D diagram is shown.

## Sensor Logging

Logging is available when at least one sensor board is connected. Logs are written to:

```text
logs/run_N.log
```

Camera recordings for the same run are written beside the log:

```text
logs/run_N.mp4
logs/run_N.video.tsv
```

The `.video.tsv` file maps video frames to the sensor-log timeline.

Common columns:

```text
elapsed_s    torr    kv    ma    mode    valve_delta    valve_steps    predicted_mtorr    slope_mtorr_s    command_reason    power_present    video_frame_id    video_frames
```

Inactive sensors are logged as empty fields.

## Playback

The playback tool replays a recorded run video with the same control panel HUD over the camera feed:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" 7
```

You can also pass the log path directly:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" "logs\run_7.log" --speed 0.5
```


To hide the valve/control HUD panel during playback:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" 10 --hide-valve-control
```

To rotate playback video 90 degrees clockwise for a vertical view while keeping
the HUD at the bottom of that rotated view:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" 10 --vertical
```

If a vertical run appears upside down, flip the final oriented frame:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" 10 --vertical --flip
```

To export the video with the HUD overlay instead of opening the playback window:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" 10 --vertical --save
```

By default this writes beside the source video using a name like
`run_10_overlay_vertical.mp4`. You can choose the output path and format by
passing a path after `--save`:

```powershell
& "control-panel\build\Release\Fusor Playback.exe" 10 --vertical --flip --save "logs\run_10_overlay_vertical_flip.mp4"
```

Playback keys:

```text
Space: pause/resume
q or Esc: quit
```

## Troubleshooting

- If the phone stream is live but the app does not show video, check the MediaMTX window for a publisher on path `fusor`.
- If the app logs `camera live`, the RTSP path is working.
- The control panel forces OpenCV/FFmpeg to read RTSP over TCP and ignore audio tracks; leaving Moblin audio off is still preferred.
- The phone will not appear in BLE scan output. BLE scanning is only for the pressure and meter boards.
- If console output mentions `hevc`, switch Moblin to H.264/AVC.
- If Moblin cannot connect, check the laptop Wi-Fi IPv4 address and Windows Firewall.
- If delay is high, lower resolution/bitrate, use H.264, turn audio off, and reduce SRT latency.

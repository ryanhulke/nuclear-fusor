# Fusor Control Panel

Native OpenGL control panel for the fusor viewport, pressure gauge, meter readouts, valve control, sensor logging, camera background, and playback.

This guide takes you from nothing to a running app in five steps. Run every command in **PowerShell from the repo root**. You install the build tools once (steps 1–3), build once (step 4), and from then on you only repeat step 5 to run.

> **The OpenCV root.** Everything OpenCV-related is derived from one folder — the directory you extract OpenCV into. This guide calls it `$OpenCVRoot`. Set it once and the build and run steps reuse it, so you never hand-type the long paths twice.

## Step 1 — Visual Studio 2022 Build Tools (C++ compiler)

This provides the MSVC C++ compiler that CMake uses to build the app. Without it, the build fails with "no compiler found."

1. Download the **Build Tools for Visual Studio 2022** from https://visualstudio.microsoft.com/downloads/ (scroll to "Tools for Visual Studio").
2. Run the installer and check the **Desktop development with C++** workload, then install.
3. If you already have Visual Studio Community/Professional with the C++ workload, you can skip this.

## Step 2 — CMake 3.21 or newer

1. Install from https://cmake.org/download/ (choose "Add CMake to the system PATH" during install).
2. Confirm it is available:
   ```powershell
   cmake --version
   ```

## Step 3 — OpenCV

The control panel links against OpenCV at build time and loads its DLLs at run time. CMake does **not** download OpenCV for you, so install it yourself.

1. Download the prebuilt Windows package (`opencv-4.x.x-windows.exe`) from https://github.com/opencv/opencv/releases/latest.
2. Run it — it is a self-extractor — and extract to a folder you will remember, for example `C:\Libraries\opencv`. **That folder is your `$OpenCVRoot`.**
3. Confirm the extract produced an `OpenCVConfig.cmake` and a `bin` folder under it:
   ```powershell
   $OpenCVRoot = "C:\Libraries\opencv"   # the folder you extracted into
   Get-ChildItem $OpenCVRoot -Recurse -Filter OpenCVConfig.cmake | Select-Object FullName
   Get-ChildItem $OpenCVRoot -Recurse -Filter opencv_world*.dll | Select-Object FullName
   ```
   For the prebuilt package these resolve to `$OpenCVRoot\build` (the config) and `$OpenCVRoot\build\x64\vc16\bin` (the DLLs). If you built OpenCV from source instead, point `$OpenCVRoot` at your install folder; the two subpaths may differ (e.g. `...\x64\vc16\lib` and `...\x64\vc16\bin`) — use whatever the commands above print.

## Step 4 — Build the app

This compiles the control panel once. You only repeat it after changing the source.

```powershell
$OpenCVRoot = "C:\Libraries\opencv"   # same folder as step 3
cmake -S control-panel -B control-panel\build -DOpenCV_DIR="$OpenCVRoot\build"
cmake --build control-panel\build --config Release
```

The first configure also downloads GLFW and SimpleBLE via CMake `FetchContent`, so it needs internet access. GLAD is vendored in `tools/glad`.

To start clean, delete the build folder and re-run the two commands above:

```powershell
Remove-Item control-panel\build -Recurse -Force
```

## Step 5 — Run the app

⚠️ **Before launching, add OpenCV's DLL folder to your PATH for this PowerShell session.** This is the step people miss: without it Windows cannot find the OpenCV DLLs and the app fails to start with a missing-DLL error. You must redo this in every new PowerShell window.

```powershell
$OpenCVRoot = "C:\Libraries\opencv"   # same folder as step 3
$env:PATH = "$OpenCVRoot\build\x64\vc16\bin;$env:PATH"
& "control-panel\build\Release\Fusor Control Panel.exe"
```

The app scans for the pressure sensor, meter readout, and valve control boards automatically. If it still reports missing DLLs, you ran the `.exe` in a different PowerShell window than the `$env:PATH` line — run both in the same window.

## Setup at a glance

After the one-time tool installs (steps 1–3), the full build-and-run sequence is:

```powershell
$OpenCVRoot = "C:\Libraries\opencv"   # adjust to where you extracted OpenCV

# Build (repeat only after source changes)
cmake -S control-panel -B control-panel\build -DOpenCV_DIR="$OpenCVRoot\build"
cmake --build control-panel\build --config Release

# Run (repeat every new PowerShell session)
$env:PATH = "$OpenCVRoot\build\x64\vc16\bin;$env:PATH"
& "control-panel\build\Release\Fusor Control Panel.exe"
```

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

With MediaMTX running and Moblin streaming, launch the app exactly as in step 5 (the camera is picked up automatically from `config.yaml`):

```powershell
$OpenCVRoot = "C:\Libraries\opencv"   # same folder as setup
$env:PATH = "$OpenCVRoot\build\x64\vc16\bin;$env:PATH"
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

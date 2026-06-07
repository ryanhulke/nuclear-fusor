#include <glad/gl.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <dwmapi.h>
#include <GLFW/glfw3native.h>
#include "resource.h"
#endif

#include "Camera/Camera.h"
#include "Control/PressureController.h"
#include "Geometry/Geometry.h"
#include "Math/Math.h"
#include "Meter/MeterMonitor.h"
#include "Plasma/PlasmaRenderer.h"
#include "Pressure/PressureMonitor.h"
#include "Render/Hud.h"
#include "Render/Render.h"
#include "Valve/ValveMonitor.h"
#include "Video/VideoBackground.h"
#include "Video/VideoStream.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

OrbitCamera gCamera;
bool gAutoRotateCamera = true;
bool gRotationKeyWasDown = false;
bool gRecordingKeyWasDown = false;
bool gVideoKeyWasDown = false;
bool gManualCloseKeyWasDown = false;
bool gManualOpenKeyWasDown = false;
bool gSensorLogControlsEnabled = false;
bool gVideoStreamEnabled = false;
bool gVideoBackgroundVisible = true;
double gLogSavedToastStartedAt = -1000.0;

constexpr float kAutoOrbitRadiansPerSecond = 0.075f;
constexpr long kManualValveStepDelta = 10;
constexpr double kCurrentReportFloorMA = 0.1;
constexpr double kLogSavedToastHoldSeconds = 2.0;
constexpr double kLogSavedToastFadeSeconds = 0.45;
constexpr double kVideoLastFrameHoldSeconds = 2.0;
constexpr double kVideoRecordingFps = 30.0;
constexpr auto kShutdownGracePeriod = std::chrono::milliseconds(2000);

struct SensorLogState {
    bool recording = false;
    std::ofstream stream;
    std::filesystem::path path;
    std::chrono::steady_clock::time_point startedAt{};
    std::chrono::steady_clock::time_point lastLoggedPressureReading{};
    std::chrono::steady_clock::time_point lastLoggedMeterReading{};
    long lastLoggedValveSteps = 0;
    int samplesWritten = 0;
    cv::VideoWriter videoWriter;
    std::ofstream videoIndexStream;
    std::filesystem::path videoPath;
    std::filesystem::path videoIndexPath;
    std::uint64_t lastRecordedVideoFrameId = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    int videoFramesWritten = 0;
    bool videoRecording = false;
    bool videoUnavailable = false;
};

SensorLogState gSensorLog;
SensorHudLayout gSensorHudLayout;
ControlHudLayout gControlHudLayout;
PressureSnapshot gLatestPressure;
MeterSnapshot gLatestMeter;
PressureController gPressureController;
ValveMonitor* gValveMonitor = nullptr;
std::chrono::steady_clock::time_point gLastControlTick{};
bool gControlTicked = false;

enum class PendingValveAction {
    None,
    Evacuate,
    Manual,
    Vent,
};

PendingValveAction gPendingValveAction = PendingValveAction::None;

bool startSensorLogging();
bool stopSensorLogging();
void showLogSavedToast();

#ifdef _WIN32
void applyWindowsWindowIcon(HWND hwnd) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    HICON smallIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
    ));
    HICON bigIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR
    ));

    if (smallIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(smallIcon));
    }
    if (bigIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(bigIcon));
    }
}

void applyWindowsWindowChrome(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return;
    }

    applyWindowsWindowIcon(hwnd);

    constexpr DWORD kDwmUseImmersiveDarkMode = 20;
    constexpr DWORD kDwmUseImmersiveDarkModeLegacy = 19;
    constexpr DWORD kDwmBorderColor = 34;
    constexpr DWORD kDwmCaptionColor = 35;
    constexpr DWORD kDwmTextColor = 36;

    const BOOL useDarkCaption = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &useDarkCaption, sizeof(useDarkCaption)))) {
        DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkModeLegacy, &useDarkCaption, sizeof(useDarkCaption));
    }

    const COLORREF captionColor = RGB(52, 53, 58);
    const COLORREF borderColor = RGB(52, 53, 58);
    const COLORREF textColor = RGB(238, 238, 242);
    DwmSetWindowAttribute(hwnd, kDwmCaptionColor, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, kDwmBorderColor, &borderColor, sizeof(borderColor));
    DwmSetWindowAttribute(hwnd, kDwmTextColor, &textColor, sizeof(textColor));
}
#else
void applyWindowsWindowChrome(GLFWwindow*) {
}
#endif

struct AppOptions {
    std::string cameraUrl;
};

std::string trimConfigValue(std::string value) {
    const auto isNotSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
    return value;
}

std::string stripConfigComment(std::string value) {
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
        } else if (ch == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
        } else if (ch == '#' && !inSingleQuote && !inDoubleQuote) {
            value.erase(i);
            break;
        }
    }

    return value;
}

std::string unquoteConfigValue(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool loadConfigFile(const std::filesystem::path& path, AppOptions& options) {
    std::ifstream stream(path);
    if (!stream) {
        return false;
    }

    std::string line;
    while (std::getline(stream, line)) {
        line = trimConfigValue(stripConfigComment(line));
        if (line.empty()) {
            continue;
        }

        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = trimConfigValue(line.substr(0, separator));
        std::string value = trimConfigValue(line.substr(separator + 1));
        value = unquoteConfigValue(value);

        if (key == "camera_url" && !value.empty()) {
            options.cameraUrl = value;
        }
    }

    return true;
}

void loadDefaultConfig(AppOptions& options) {
    const std::filesystem::path configPath = std::filesystem::path{"control-panel"} / "config" / "config.yaml";
    std::error_code error;
    if (std::filesystem::is_regular_file(configPath, error) && loadConfigFile(configPath, options) && !options.cameraUrl.empty()) {
        std::cout << "Loaded camera_url from " << configPath.lexically_normal().string() << "\n";
    }
}

bool parseAppOptions(int argc, char** argv, AppOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--camera-url") {
            if (i + 1 >= argc) {
                std::cout << "Missing value for --camera-url\n";
                return false;
            }
            options.cameraUrl = argv[++i];
        } else {
            std::cout << "Unknown argument: " << arg << "\n";
            return false;
        }
    }
    return true;
}

std::string formatPressureValue(double torr, std::string& unit) {
    constexpr int kSignificantFigures = 4;

    double displayValue = torr;
    unit = "Torr";

    if (torr < 1.0) {
        displayValue = torr * 1000.0;
        unit = "mTorr";
    }

    const auto decimalPlacesFor = [kSignificantFigures](double value) {
        const double magnitude = std::abs(value);
        if (magnitude <= 0.0) {
            return kSignificantFigures - 1;
        }

        const int order = static_cast<int>(std::floor(std::log10(magnitude)));
        return std::max(0, kSignificantFigures - order - 1);
    };

    int precision = decimalPlacesFor(displayValue);
    if (precision > 0) {
        const double scale = std::pow(10.0, precision);
        const double roundedValue = std::round(displayValue * scale) / scale;
        precision = decimalPlacesFor(roundedValue);
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << displayValue;
    return out.str();
}

std::string formatFixedValue(double value, int precision, double deadband) {
    const double scale = std::pow(10.0, precision);
    const double roundedDisplayValue = std::round(value * scale) / scale;
    if (std::abs(roundedDisplayValue) <= deadband) {
        value = 0.0;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string formatVoltageValue(double kilovolts) {
    return formatFixedValue(kilovolts, 2, gPressureController.config().voltagePresentThresholdKV);
}

double reportableCurrentMilliamps(double milliamps) {
    return milliamps < kCurrentReportFloorMA ? 0.0 : milliamps;
}

std::string formatCurrentValue(double milliamps) {
    return formatFixedValue(reportableCurrentMilliamps(milliamps), 3, 0.0);
}

std::filesystem::path nextSensorLogPath() {
    const std::filesystem::path logDirectory{"logs"};

    std::error_code error;
    std::filesystem::create_directories(logDirectory, error);
    if (error) {
        std::cout << "Sensor logging: could not create logs directory: " << error.message() << "\n";
    }

    for (int run = 1; run < 100000; ++run) {
        std::filesystem::path path = logDirectory / ("run_" + std::to_string(run) + ".log");
        std::filesystem::path mp4Path = path;
        std::filesystem::path aviPath = path;
        std::filesystem::path videoIndexPath = path;
        mp4Path.replace_extension(".mp4");
        aviPath.replace_extension(".avi");
        videoIndexPath.replace_extension(".video.tsv");
        error.clear();
        const bool logExists = std::filesystem::exists(path, error);
        error.clear();
        const bool mp4Exists = std::filesystem::exists(mp4Path, error);
        error.clear();
        const bool aviExists = std::filesystem::exists(aviPath, error);
        error.clear();
        const bool indexExists = std::filesystem::exists(videoIndexPath, error);
        if (!logExists && !mp4Exists && !aviExists && !indexExists) {
            return path;
        }
    }

    return logDirectory / "run_99999.log";
}

std::filesystem::path videoPathForLogPath(const std::filesystem::path& logPath, const char* extension) {
    std::filesystem::path path = logPath;
    path.replace_extension(extension);
    return path;
}

void resetVideoRecordingState() {
    gSensorLog.videoPath.clear();
    gSensorLog.videoIndexPath.clear();
    gSensorLog.lastRecordedVideoFrameId = 0;
    gSensorLog.videoWidth = 0;
    gSensorLog.videoHeight = 0;
    gSensorLog.videoFramesWritten = 0;
    gSensorLog.videoRecording = false;
    gSensorLog.videoUnavailable = false;
}

void closeVideoRecording() {
    if (gSensorLog.videoWriter.isOpened()) {
        gSensorLog.videoWriter.release();
    }
    if (gSensorLog.videoIndexStream.is_open()) {
        gSensorLog.videoIndexStream.flush();
        gSensorLog.videoIndexStream.close();
    }
    if (gSensorLog.videoRecording) {
        std::cout << "Sensor logging: video stopped " << gSensorLog.videoPath.string()
                  << " frames=" << gSensorLog.videoFramesWritten << "\n";
    }
    gSensorLog.videoRecording = false;
}

bool openVideoRecording(const VideoFrameSnapshot& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgb.empty()) {
        return false;
    }

    const std::filesystem::path mp4Path = videoPathForLogPath(gSensorLog.path, ".mp4");
    cv::VideoWriter writer;
    writer.open(
        mp4Path.string(),
        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        kVideoRecordingFps,
        cv::Size(frame.width, frame.height),
        true
    );

    std::filesystem::path videoPath = mp4Path;
    if (!writer.isOpened()) {
        const std::filesystem::path aviPath = videoPathForLogPath(gSensorLog.path, ".avi");
        writer.open(
            aviPath.string(),
            cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
            kVideoRecordingFps,
            cv::Size(frame.width, frame.height),
            true
        );
        videoPath = aviPath;
    }

    if (!writer.isOpened()) {
        std::cout << "Sensor logging: could not start video recording for " << gSensorLog.path.string() << "\n";
        gSensorLog.videoUnavailable = true;
        return false;
    }

    gSensorLog.videoWriter = std::move(writer);
    gSensorLog.videoPath = videoPath;
    gSensorLog.videoIndexPath = videoPathForLogPath(gSensorLog.path, ".video.tsv");
    gSensorLog.videoIndexStream.clear();
    gSensorLog.videoIndexStream.open(gSensorLog.videoIndexPath, std::ios::out | std::ios::trunc);
    if (gSensorLog.videoIndexStream) {
        gSensorLog.videoIndexStream << "elapsed_s\tvideo_frame_id\tvideo_frame_index\tvideo_file\n";
    } else {
        std::cout << "Sensor logging: could not open video index " << gSensorLog.videoIndexPath.string() << "\n";
    }
    gSensorLog.videoWidth = frame.width;
    gSensorLog.videoHeight = frame.height;
    gSensorLog.videoFramesWritten = 0;
    gSensorLog.videoRecording = true;
    std::cout << "Sensor logging: video started " << gSensorLog.videoPath.string() << "\n";
    return true;
}

void recordVideoFrameIfNeeded(const VideoFrameSnapshot& frame) {
    if (!gSensorLog.recording || gSensorLog.videoUnavailable || !frame.connected || !frame.hasFrame) {
        return;
    }
    if (frame.frameId == 0 || frame.frameId == gSensorLog.lastRecordedVideoFrameId) {
        return;
    }
    const std::size_t requiredBytes = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3U;
    if (frame.width <= 0 || frame.height <= 0 || frame.rgb.size() < requiredBytes) {
        return;
    }
    if (!gSensorLog.videoRecording && !openVideoRecording(frame)) {
        return;
    }

    cv::Mat rgb(frame.height, frame.width, CV_8UC3, const_cast<unsigned char*>(frame.rgb.data()));
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    if (frame.width != gSensorLog.videoWidth || frame.height != gSensorLog.videoHeight) {
        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size(gSensorLog.videoWidth, gSensorLog.videoHeight));
        bgr = std::move(resized);
    }

    gSensorLog.videoWriter.write(bgr);
    gSensorLog.lastRecordedVideoFrameId = frame.frameId;
    ++gSensorLog.videoFramesWritten;

    if (gSensorLog.videoIndexStream) {
        const double elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gSensorLog.startedAt).count();
        gSensorLog.videoIndexStream << std::fixed << std::setprecision(3)
                                    << elapsedSeconds << '\t'
                                    << frame.frameId << '\t'
                                    << gSensorLog.videoFramesWritten << '\t'
                                    << gSensorLog.videoPath.filename().string() << '\n';
    }
}

bool startSensorLogging() {
    if (gSensorLog.recording || !gSensorLogControlsEnabled) {
        return false;
    }

    gSensorLog.path = nextSensorLogPath();
    gSensorLog.stream.clear();
    gSensorLog.stream.open(gSensorLog.path, std::ios::out | std::ios::trunc);
    if (!gSensorLog.stream) {
        std::cout << "Sensor logging: failed to open " << gSensorLog.path.string() << "\n";
        gSensorLog.path.clear();
        return false;
    }

    gSensorLog.recording = true;
    gSensorLog.startedAt = std::chrono::steady_clock::now();
    gSensorLog.lastLoggedPressureReading = gLatestPressure.lastReading;
    gSensorLog.lastLoggedMeterReading = gLatestMeter.lastReading;
    gSensorLog.lastLoggedValveSteps = gPressureController.snapshot().virtualPositionSteps;
    gSensorLog.samplesWritten = 0;
    resetVideoRecordingState();
    gSensorLog.stream
        << "elapsed_s\ttorr\tkv\tma\tmode\tvalve_delta\tvalve_steps"
        << "\tpredicted_mtorr\tslope_mtorr_s\tcommand_reason\tpower_present"
        << "\tvideo_frame_id\tvideo_frames\n";
    gSensorLog.stream.flush();

    std::cout << "Sensor logging: started " << gSensorLog.path.string() << "\n";
    return true;
}

bool stopSensorLogging() {
    if (!gSensorLog.recording) {
        return false;
    }

    const std::string path = gSensorLog.path.string();
    const bool savedData = gSensorLog.samplesWritten > 0 || gSensorLog.videoFramesWritten > 0;
    gSensorLog.recording = false;
    closeVideoRecording();
    if (gSensorLog.stream.is_open()) {
        gSensorLog.stream.flush();
        gSensorLog.stream.close();
    }
    gSensorLog.path.clear();
    gSensorLog.lastLoggedPressureReading = {};
    gSensorLog.lastLoggedMeterReading = {};
    gSensorLog.lastLoggedValveSteps = 0;
    gSensorLog.samplesWritten = 0;
    resetVideoRecordingState();
    std::cout << "Sensor logging: stopped " << path << "\n";
    return savedData;
}

void showLogSavedToast() {
    gLogSavedToastStartedAt = glfwGetTime();
}

void logSensorsIfNeeded(const PressureSnapshot& pressure, const MeterSnapshot& meter) {
    const ControllerSnapshot controller = gPressureController.snapshot();
    const bool pressureUpdated = pressure.hasReading && pressure.lastReading != gSensorLog.lastLoggedPressureReading;
    const bool meterUpdated = (meter.hasVoltage || meter.hasCurrent) && meter.lastReading != gSensorLog.lastLoggedMeterReading;
    const bool valveUpdated = controller.virtualPositionSteps != gSensorLog.lastLoggedValveSteps;
    if (!gSensorLog.recording || (!pressureUpdated && !meterUpdated && !valveUpdated)) {
        return;
    }

    if (pressureUpdated) {
        gSensorLog.lastLoggedPressureReading = pressure.lastReading;
    }
    if (meterUpdated) {
        gSensorLog.lastLoggedMeterReading = meter.lastReading;
    }
    if (valveUpdated) {
        gSensorLog.lastLoggedValveSteps = controller.virtualPositionSteps;
    }

    if (!gSensorLog.stream) {
        stopSensorLogging();
        return;
    }

    const bool pressureActive = pressure.connected && pressure.live && pressure.hasReading;
    const bool voltageActive = meter.connected && meter.live && meter.hasVoltage;
    const bool currentActive = meter.connected && meter.live && meter.hasCurrent;
    const double elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gSensorLog.startedAt).count();

    gSensorLog.stream << std::fixed << std::setprecision(3) << elapsedSeconds << '\t';
    if (pressureActive) {
        gSensorLog.stream << std::setprecision(9) << pressure.torr;
    }
    gSensorLog.stream << '\t';
    if (voltageActive) {
        gSensorLog.stream << std::setprecision(6) << meter.kilovolts;
    }
    gSensorLog.stream << '\t';
    if (currentActive) {
        gSensorLog.stream << std::setprecision(6) << reportableCurrentMilliamps(meter.milliamps);
    }

    gSensorLog.stream << '\t' << controlModeName(controller.mode)
                      << '\t' << controller.lastCommandedDelta
                      << '\t' << controller.virtualPositionSteps
                      << '\t';
    if (controller.hasPressure) {
        gSensorLog.stream << std::setprecision(3) << controller.predictedPressureMTorr;
    }
    gSensorLog.stream << '\t';
    if (controller.hasPressure) {
        gSensorLog.stream << std::setprecision(3) << controller.pressureSlopeMTorrPerSec;
    }
    gSensorLog.stream << '\t' << controller.commandReason
                      << '\t' << (controller.powerPresent ? 1 : 0)
                      << '\t' << gSensorLog.lastRecordedVideoFrameId
                      << '\t' << gSensorLog.videoFramesWritten;
    gSensorLog.stream << '\n';
    gSensorLog.stream.flush();
    ++gSensorLog.samplesWritten;
}

HudMetricData makePressureHudData(const PressureSnapshot& pressure) {
    HudMetricData data{};
    data.label = "PRESSURE";
    data.unit = "Torr";
    data.hasReading = pressure.hasReading;
    data.connected = pressure.connected;
    data.live = pressure.live;

    if (pressure.hasReading) {
        data.value = formatPressureValue(pressure.torr, data.unit);
    }

    if (pressure.live) {
        data.status = "Live";
    } else if (pressure.status == "Gauge error") {
        data.status = "Error";
    } else if (pressure.status == "BLE unavailable") {
        data.status = "No BLE";
    } else {
        data.status = pressure.status;
    }

    return data;
}

std::string meterStatusText(const MeterSnapshot& meter, bool live, bool saturated) {
    if (live) {
        return saturated ? "Sat" : "Live";
    }
    if (meter.status == "Meter error") {
        return "Error";
    }
    if (meter.status == "BLE unavailable") {
        return "No BLE";
    }
    return meter.status;
}

HudMetricData makeVoltageHudData(const MeterSnapshot& meter) {
    HudMetricData data{};
    data.label = "VOLTAGE";
    data.unit = "kV";
    data.hasReading = meter.hasVoltage;
    data.connected = meter.connected;
    data.live = meter.live && meter.hasVoltage;
    if (meter.hasVoltage) {
        data.value = formatVoltageValue(meter.kilovolts);
    }
    data.status = data.hasReading ? meterStatusText(meter, data.live, meter.saturatedVoltage)
                                  : meter.connected ? meterStatusText(meter, false, false) : meter.status;
    return data;
}

HudMetricData makeCurrentHudData(const MeterSnapshot& meter) {
    HudMetricData data{};
    data.label = "CURRENT";
    data.unit = "mA";
    data.hasReading = meter.hasCurrent;
    data.connected = meter.connected;
    data.live = meter.live && meter.hasCurrent;
    if (meter.hasCurrent) {
        data.value = formatCurrentValue(meter.milliamps);
    }
    data.status = data.hasReading ? meterStatusText(meter, data.live, meter.saturatedCurrent)
                                  : meter.connected ? meterStatusText(meter, false, false) : meter.status;
    return data;
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

ControlHudMode toHudMode(ControlMode mode) {
    switch (mode) {
    case ControlMode::Idle: return ControlHudMode::Idle;
    case ControlMode::Evacuate: return ControlHudMode::Evacuate;
    case ControlMode::Fault: return ControlHudMode::Fault;
    }
    return ControlHudMode::Idle;
}

ControlHudData buildControlHudData(const ControllerSnapshot& snapshot) {
    ControlHudData data{};
    data.mode = toHudMode(snapshot.mode);
    data.virtualPositionSteps = snapshot.virtualPositionSteps;
    data.evacuationComplete = snapshot.evacuationComplete;
    data.faultReason = snapshot.faultReason;
    data.statusDetail = snapshot.statusDetail;
    return data;
}

bool meterPowerPresentForSafety(const MeterSnapshot& meter) {
    const ControllerConfig& config = gPressureController.config();
    if (!meter.connected || !meter.live) {
        return false;
    }

    const bool voltagePresent = meter.hasVoltage
                                && std::abs(meter.kilovolts) > config.voltagePresentThresholdKV;
    const bool currentPresent = meter.hasCurrent
                                && std::abs(meter.milliamps) > config.currentPresentThresholdMA;
    return voltagePresent || currentPresent;
}

bool meterPowerKnownClear(const MeterSnapshot& meter) {
    // Missing meter/ADS data is not a vent interlock. A live voltage/current
    // sample blocks vent only when the reported value is above the zero band.
    return !meterPowerPresentForSafety(meter);
}

void applyValveSnapshotToHud(ControlHudData& data, const ValveSnapshot& valve) {
    data.valveConnected = valve.connected;
    data.valveLive = valve.live && valve.hasPosition;
    const bool hasOpenLimit = valve.openLimitSteps > 0;
    const long evacuateTarget = std::clamp(
        gPressureController.config().evacuationValveSteps,
        0L,
        hasOpenLimit ? valve.openLimitSteps : gPressureController.config().evacuationValveSteps
    );
    const bool powerClear = meterPowerKnownClear(gLatestMeter);
    const bool powerPresent = meterPowerPresentForSafety(gLatestMeter);
    data.ventAvailable = data.valveLive && hasOpenLimit && powerClear;
    data.ventBlockedByPower = data.valveLive && hasOpenLimit && powerPresent;
    data.ventMissingLimit = data.valveLive && !hasOpenLimit;
    data.manualAvailable = data.valveLive;
    data.evacuateAvailable = data.valveLive;
    if (gPendingValveAction == PendingValveAction::Manual && data.mode != ControlHudMode::Fault) {
        data.mode = ControlHudMode::Manual;
    }
    data.evacuateSelected = data.valveLive
                             && (data.mode == ControlHudMode::Evacuate
                                 || gPendingValveAction == PendingValveAction::Evacuate);
    if (data.valveLive) {
        data.virtualPositionSteps = valve.positionSteps;
        const bool atEvacuate = std::labs(valve.positionSteps - evacuateTarget) <= 1;
        const bool atVent = hasOpenLimit && valve.positionSteps >= valve.openLimitSteps;
        data.ventOpen = gPendingValveAction == PendingValveAction::Vent || atVent;
        data.manualSelected = data.mode == ControlHudMode::Manual
                              && gPendingValveAction == PendingValveAction::Manual;
        if (gPendingValveAction == PendingValveAction::Evacuate && atEvacuate) {
            data.evacuateSelected = true;
        }
    }
}

bool valveReadyForControl() {
    if (!gValveMonitor) {
        return false;
    }
    const ValveSnapshot valve = gValveMonitor->snapshot();
    return valve.live && valve.hasPosition;
}

bool sendVentValveCommand() {
    if (gPressureController.snapshot().mode == ControlMode::Fault) {
        return false;
    }
    if (!gValveMonitor) {
        return false;
    }

    const ValveSnapshot valve = gValveMonitor->snapshot();
    if (!valve.live || !valve.hasPosition || valve.openLimitSteps <= 0) {
        return false;
    }
    if (!meterPowerKnownClear(gLatestMeter)) {
        return false;
    }

    if (valve.positionSteps >= valve.openLimitSteps) {
        return false;
    }
    gPressureController.requestMode(ControlMode::Idle);
    if (!gValveMonitor->sendOpen()) {
        return false;
    }
    gPendingValveAction = PendingValveAction::Vent;
    return true;
}

bool requestManualValveMode() {
    if (gPressureController.snapshot().mode == ControlMode::Fault) {
        return false;
    }
    if (gPendingValveAction == PendingValveAction::Manual) {
        gPendingValveAction = PendingValveAction::None;
        gPressureController.requestMode(ControlMode::Idle);
        return true;
    }
    if (!valveReadyForControl()) {
        return false;
    }

    gPendingValveAction = PendingValveAction::Manual;
    gPressureController.requestMode(ControlMode::Idle);
    return true;
}

bool manualValveModeActive() {
    return gPendingValveAction == PendingValveAction::Manual
           && gPressureController.snapshot().mode != ControlMode::Fault;
}

bool sendManualValveJog(long deltaSteps) {
    if (!manualValveModeActive() || !gValveMonitor) {
        return false;
    }

    const ValveSnapshot valve = gValveMonitor->snapshot();
    if (!valve.live || !valve.hasPosition) {
        return false;
    }

    gPressureController.requestMode(ControlMode::Idle);
    return gValveMonitor->sendMove(deltaSteps);
}

bool requestEvacuateValveMode() {
    if (gPressureController.snapshot().mode == ControlMode::Fault) {
        return false;
    }
    if (gPressureController.snapshot().mode == ControlMode::Evacuate
        || gPendingValveAction == PendingValveAction::Evacuate) {
        gPendingValveAction = PendingValveAction::None;
        gPressureController.requestMode(ControlMode::Idle);
        return true;
    }
    if (!valveReadyForControl()) {
        return false;
    }
    gPendingValveAction = PendingValveAction::Evacuate;
    gPressureController.requestMode(ControlMode::Evacuate);
    return true;
}

bool handleControlHudClick(GLFWwindow* window) {
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (windowWidth <= 0 || windowHeight <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }

    const float x = static_cast<float>(cursorX) * static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
    const float y = static_cast<float>(cursorY) * static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);

    if (gControlHudLayout.clearFaultButton.contains(x, y)) {
        gPressureController.clearFault();
        return true;
    }
    if (gControlHudLayout.ventButton.contains(x, y)) {
        sendVentValveCommand();
        return true;
    }
    if (gControlHudLayout.manualButton.contains(x, y)) {
        requestManualValveMode();
        return true;
    }
    if (gControlHudLayout.evacuateButton.contains(x, y)) {
        requestEvacuateValveMode();
        return true;
    }
    if (gControlHudLayout.panel.contains(x, y)) {
        return true;
    }
    return false;
}

bool handleSensorHudClick(GLFWwindow* window) {
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);

    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (windowWidth <= 0 || windowHeight <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }

    const float x = static_cast<float>(cursorX) * static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
    const float y = static_cast<float>(cursorY) * static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);

    const bool overStart = gSensorHudLayout.startButton.contains(x, y);
    const bool overStop = gSensorHudLayout.stopButton.contains(x, y);
    if (overStart || overStop) {
        if (!gSensorLogControlsEnabled) {
            return true;
        }

        if (overStart && !gSensorLog.recording) {
            startSensorLogging();
        } else if (overStop && gSensorLog.recording) {
            if (stopSensorLogging()) {
                showLogSavedToast();
            }
        }
        return true;
    }

    return false;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }

    if (action == GLFW_PRESS) {
        if (handleControlHudClick(window)) {
            return;
        }
        if (handleSensorHudClick(window)) {
            return;
        }
        gCamera.dragging = true;
        glfwGetCursorPos(window, &gCamera.lastX, &gCamera.lastY);
    } else if (action == GLFW_RELEASE) {
        gCamera.dragging = false;
    }
}

void cursorPositionCallback(GLFWwindow*, double xpos, double ypos) {
    if (!gCamera.dragging) {
        return;
    }

    const double dx = xpos - gCamera.lastX;
    const double dy = ypos - gCamera.lastY;
    gCamera.lastX = xpos;
    gCamera.lastY = ypos;

    gCamera.yaw -= static_cast<float>(dx) * 0.006f;
    gCamera.pitch += static_cast<float>(dy) * 0.005f;
    gCamera.pitch = std::clamp(gCamera.pitch, -1.42f, 1.42f);
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    gCamera.distance *= std::pow(0.90f, static_cast<float>(yoffset));
    gCamera.distance = std::clamp(gCamera.distance, 58.0f, 500.0f);
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    const bool rotationKeyDown = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
    if (rotationKeyDown && !gRotationKeyWasDown) {
        gAutoRotateCamera = !gAutoRotateCamera;
    }
    gRotationKeyWasDown = rotationKeyDown;

    const bool recordingKeyDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (recordingKeyDown && !gRecordingKeyWasDown && gSensorLogControlsEnabled) {
        if (gSensorLog.recording) {
            if (stopSensorLogging()) {
                showLogSavedToast();
            }
        } else {
            startSensorLogging();
        }
    }
    gRecordingKeyWasDown = recordingKeyDown;

    const bool videoKeyDown = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
    if (videoKeyDown && !gVideoKeyWasDown && gVideoStreamEnabled) {
        gVideoBackgroundVisible = !gVideoBackgroundVisible;
        std::cout << "camera background " << (gVideoBackgroundVisible ? "shown" : "hidden") << "\n";
    }
    gVideoKeyWasDown = videoKeyDown;

    const bool manualCloseKeyDown = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    if (manualCloseKeyDown && !gManualCloseKeyWasDown) {
        sendManualValveJog(-kManualValveStepDelta);
    }
    gManualCloseKeyWasDown = manualCloseKeyDown;

    const bool manualOpenKeyDown = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    if (manualOpenKeyDown && !gManualOpenKeyWasDown) {
        sendManualValveJog(kManualValveStepDelta);
    }
    gManualOpenKeyWasDown = manualOpenKeyDown;
}

int main(int argc, char** argv) {
    AppOptions options{};
    loadDefaultConfig(options);
    if (!parseAppOptions(argc, argv, options)) {
        std::cout << "Usage: app [--camera-url <rtsp-url>]\n";
        return 1;
    }
    gVideoStreamEnabled = !options.cameraUrl.empty();

    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Fusor Control Panel", nullptr, nullptr);
    if (!window) {
        std::cout << "Failed to create GLFW window\n";
        glfwTerminate();
        return 1;
    }
    applyWindowsWindowChrome(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetScrollCallback(window, scrollCallback);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);

    const GLuint meshProgram = createMeshProgram();
    const GLuint lineProgram = createLineProgram();
    HudRenderer hudRenderer = createHudRenderer();
    VideoBackgroundRenderer videoBackgroundRenderer{};
    if (gVideoStreamEnabled) {
        videoBackgroundRenderer = createVideoBackgroundRenderer();
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    gCamera.reset();
    const FusorGeometry& geometry = fusorGeometry();
    GpuMesh shakerBall = buildShakerBallMesh();
    GpuMesh hvFeedthroughConductor = buildHvFeedthroughConductorMesh();
    GpuMesh hvFeedthroughExterior = buildHvFeedthroughExteriorMesh();
    GpuMesh chamberCylinder = buildCylinderMesh();
    GpuLines chamberLines = buildCylinderLines();
    PlasmaRenderer plasmaRenderer = createPlasmaRenderer(geometry);
    PressureMonitor pressureMonitor;
    MeterMonitor meterMonitor;
    ValveMonitor valveMonitor;
    VideoStream videoStream;
    pressureMonitor.start();
    meterMonitor.start();
    valveMonitor.start();
    gValveMonitor = &valveMonitor;
    gPressureController.setValveMonitor(&valveMonitor);
    if (gVideoStreamEnabled) {
        videoStream.start(options.cameraUrl);
    }

    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << "\n";
    std::cout << "Left-drag to orbit, mouse wheel to zoom, F to toggle slow rotation, R to start/stop sensor logging, V to toggle camera, Esc to quit.\n";

    double previousFrameTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        const double frameTime = glfwGetTime();
        const float deltaSeconds = static_cast<float>(std::clamp(frameTime - previousFrameTime, 0.0, 0.05));
        previousFrameTime = frameTime;

        const PressureSnapshot pressure = pressureMonitor.snapshot();
        const MeterSnapshot meter = meterMonitor.snapshot();
        const ValveSnapshot valve = valveMonitor.snapshot();
        VideoFrameSnapshot videoFrame;
        if (gVideoStreamEnabled) {
            videoFrame = videoStream.snapshot();
        }
        gLatestPressure = pressure;
        gLatestMeter = meter;
        if (gSensorLog.recording && !pressure.connected && !meter.connected && !videoFrame.connected) {
            stopSensorLogging();
        }
        gSensorLogControlsEnabled = pressure.connected || meter.connected || videoFrame.connected;

        const auto controlNow = std::chrono::steady_clock::now();
        const auto controlPeriod = std::chrono::milliseconds(static_cast<long long>(gPressureController.config().controlPeriodSec * 1000.0));
        if (!gControlTicked || controlNow - gLastControlTick >= controlPeriod) {
            gPressureController.tick(pressure, meter);
            gLastControlTick = controlNow;
            gControlTicked = true;
        }

        processInput(window);
        if (gAutoRotateCamera && !gCamera.dragging) {
            gCamera.yaw += kAutoOrbitRadiansPerSecond * deltaSeconds;
        }

        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        const float aspect = framebufferHeight > 0 ? static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight) : 1.0f;
        const Vec3 eye = gCamera.position();
        const Mat4 view = lookAt(eye, gCamera.target, {0.0f, 0.0f, 1.0f});
        const Mat4 projection = perspective(45.0f * kPi / 180.0f, aspect, 0.1f, 1000.0f);
        const Mat4 mvp = multiply(projection, view);
        recordVideoFrameIfNeeded(videoFrame);
        logSensorsIfNeeded(pressure, meter);

        glClearColor(0.055f, 0.06f, 0.078f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        bool cameraFrameVisible = false;
        bool cameraConnected = false;
        if (gVideoStreamEnabled) {
            cameraConnected = videoFrame.connected;
            if (gVideoBackgroundVisible && videoFrame.hasFrame) {
                const double frameAge = std::chrono::duration<double>(std::chrono::steady_clock::now() - videoFrame.receivedAt).count();
                if (frameAge <= kVideoLastFrameHoldSeconds) {
                    updateVideoBackgroundTexture(videoBackgroundRenderer, videoFrame);
                    drawVideoBackground(videoBackgroundRenderer, framebufferWidth, framebufferHeight);
                    cameraFrameVisible = true;
                }
            }
        }

        if (!cameraFrameVisible) {
            drawMesh(meshProgram, shakerBall, mvp, eye, 0.58f, 0.60f, 0.62f, 1.0f);
            drawMesh(meshProgram, hvFeedthroughConductor, mvp, eye, 0.58f, 0.60f, 0.62f, 1.0f);

            drawPlasma(
                plasmaRenderer,
                mvp,
                eye,
                makePlasmaParams(geometry, static_cast<float>(frameTime))
            );

            glDepthMask(GL_FALSE);
            drawMesh(meshProgram, chamberCylinder, mvp, eye, 0.28f, 0.68f, 0.95f, 0.17f);
            drawMesh(meshProgram, hvFeedthroughExterior, mvp, eye, 0.28f, 0.68f, 0.95f, 0.17f);
            drawLines(lineProgram, chamberLines, mvp, 0.45f, 0.85f, 1.0f, 0.46f, 1.25f);
            glDepthMask(GL_TRUE);
        }

        SensorHudData hudData{};
        hudData.pressure = makePressureHudData(pressure);
        hudData.voltage = makeVoltageHudData(meter);
        hudData.current = makeCurrentHudData(meter);
        hudData.cameraConnected = cameraConnected;
        hudData.recording = gSensorLog.recording;
        hudData.loggingAvailable = gSensorLogControlsEnabled;
        const double toastAge = frameTime - gLogSavedToastStartedAt;
        if (toastAge >= 0.0 && toastAge < kLogSavedToastHoldSeconds + kLogSavedToastFadeSeconds) {
            const double fadeAge = std::max(0.0, toastAge - kLogSavedToastHoldSeconds);
            hudData.logSavedAlpha = static_cast<float>(1.0 - fadeAge / kLogSavedToastFadeSeconds);
            const double liftT = std::clamp(toastAge / 0.35, 0.0, 1.0);
            hudData.logSavedLift = static_cast<float>(8.0 * (1.0 - (1.0 - liftT) * (1.0 - liftT)));
        }
        gSensorHudLayout = drawSensorHud(hudRenderer, framebufferWidth, framebufferHeight, hudData);

        ControlHudData controlHudData = buildControlHudData(gPressureController.snapshot());
        applyValveSnapshotToHud(controlHudData, valve);
        gControlHudLayout = drawControlHud(hudRenderer, framebufferWidth, framebufferHeight, gSensorHudLayout.panel, controlHudData);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwHideWindow(window);
    gPressureController.setValveMonitor(nullptr);
    gValveMonitor = nullptr;
    videoStream.requestStop();
    meterMonitor.requestStop();
    pressureMonitor.requestStop();
    valveMonitor.requestStop();

    stopSensorLogging();

    const auto shutdownDeadline = std::chrono::steady_clock::now() + kShutdownGracePeriod;
    while (std::chrono::steady_clock::now() < shutdownDeadline
           && (videoStream.running() || meterMonitor.running() || pressureMonitor.running() || valveMonitor.running())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    const bool cleanShutdown = !videoStream.running() && !meterMonitor.running() && !pressureMonitor.running() && !valveMonitor.running();
    if (cleanShutdown) {
        videoStream.stop();
        meterMonitor.stop();
        pressureMonitor.stop();
        valveMonitor.stop();
    } else {
        std::cout << "Shutdown: forcing process exit because a background worker did not stop in time.\n";
    }

    destroyVideoBackgroundRenderer(videoBackgroundRenderer);
    destroyHudRenderer(hudRenderer);
    destroyPlasmaRenderer(plasmaRenderer);
    destroyLines(chamberLines);
    destroyMesh(chamberCylinder);
    destroyMesh(hvFeedthroughExterior);
    destroyMesh(hvFeedthroughConductor);
    destroyMesh(shakerBall);
    glDeleteProgram(lineProgram);
    glDeleteProgram(meshProgram);

    glfwDestroyWindow(window);
    glfwTerminate();

    if (!cleanShutdown) {
#ifdef _WIN32
        ExitProcess(0);
#else
        std::_Exit(0);
#endif
    }

    return 0;
}

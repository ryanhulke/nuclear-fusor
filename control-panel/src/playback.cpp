#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Render/Hud.h"
#include "Video/VideoBackground.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr double kVoltageZeroBandKV = 0.05;
constexpr double kCurrentReportFloorMA = 0.1;
constexpr double kTimelineVisibleSec = 3.0;
constexpr double kTimelineFadeSec = 0.30;
constexpr double kTimelineScrollStepSec = 5.0;

struct Options {
    std::string run;
    std::optional<std::filesystem::path> logsDir;
    double speed = 1.0;
    bool probeOnly = false;
    bool showValveControl = true;
    bool vertical = false;
    bool flip = false;
    bool save = false;
    std::optional<std::filesystem::path> savePath;
};

struct WindowSize {
    int width = 1280;
    int height = 720;
};

struct OffscreenRenderTarget {
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthStencil = 0;
    int width = 0;
    int height = 0;
};

struct RunPaths {
    std::filesystem::path log;
    std::filesystem::path video;
    std::filesystem::path videoIndex;
};

struct ResolveResult {
    std::optional<std::filesystem::path> log;
    std::optional<RunPaths> paths;
    std::vector<std::filesystem::path> videoCandidates;
};

struct SensorSample {
    double elapsedSec = 0.0;
    std::optional<double> torr;
    std::optional<double> kilovolts;
    std::optional<double> milliamps;
    std::string mode;
    std::optional<long> valveSteps;
    std::optional<int> powerPresent;
};

struct FrameTimePoint {
    double elapsedSec = 0.0;
    std::size_t frameIndex = 0;
};

struct PlaybackUiState {
    PlaybackTimelineLayout timelineLayout;
    double lastPointerActivityAt = -1000.0;
    double currentElapsedSec = 0.0;
    double durationSec = 0.0;
    bool hasPendingSeek = false;
    double pendingSeekElapsedSec = 0.0;
    bool draggingTimeline = false;
};

std::string trim(std::string text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

std::map<std::string, std::size_t> headerMap(const std::vector<std::string>& header) {
    std::map<std::string, std::size_t> fields;
    for (std::size_t i = 0; i < header.size(); ++i) {
        fields[trim(header[i])] = i;
    }
    return fields;
}

std::string field(const std::map<std::string, std::size_t>& header,
                  const std::vector<std::string>& row,
                  const char* name) {
    const auto it = header.find(name);
    if (it == header.end() || it->second >= row.size()) {
        return {};
    }
    return trim(row[it->second]);
}

std::optional<double> parseDouble(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || errno == ERANGE) {
        return {};
    }
    return value;
}

std::optional<long> parseLong(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || errno == ERANGE) {
        return {};
    }
    return value;
}

bool parsePositiveDouble(const std::string& text, double& value) {
    const std::optional<double> parsed = parseDouble(text);
    if (!parsed || *parsed <= 0.0) {
        return false;
    }
    value = *parsed;
    return true;
}

bool isFlagLike(const std::string& text) {
    return text.size() >= 2 && text[0] == '-' && text[1] == '-';
}

std::string logNameForRun(std::string run) {
    run = trim(std::move(run));
    if (run.empty()) {
        return {};
    }

    const std::filesystem::path asPath(run);
    if (asPath.has_extension() || run.find('/') != std::string::npos || run.find('\\') != std::string::npos) {
        return run;
    }

    if (run.rfind("run_", 0) == 0) {
        return run + ".log";
    }

    return "run_" + run + ".log";
}

std::vector<std::filesystem::path> candidateLogPaths(const std::string& runArg,
                                                     const std::optional<std::filesystem::path>& logsDir,
                                                     const std::filesystem::path& exeDir) {
    const std::string name = logNameForRun(runArg);
    const std::filesystem::path input(runArg);
    const std::filesystem::path logName(name);
    const std::filesystem::path cwd = std::filesystem::current_path();
    const bool runLooksLikePath = input.has_extension()
                                  || runArg.find('/') != std::string::npos
                                  || runArg.find('\\') != std::string::npos;

    std::vector<std::filesystem::path> candidates;
    if (runLooksLikePath) {
        candidates.push_back(input);
    }
    if (logsDir) {
        candidates.push_back(*logsDir / logName.filename());
    }
    candidates.push_back(cwd / "logs" / logName.filename());
    return candidates;
}

std::optional<std::filesystem::path> firstExistingPath(const std::vector<std::filesystem::path>& candidates) {
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error)) {
            continue;
        }
        error.clear();
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, error);
        return error ? candidate : canonical;
    }
    return {};
}

std::optional<std::filesystem::path> videoPathFromIndex(const std::filesystem::path& videoIndex) {
    std::ifstream stream(videoIndex);
    if (!stream) {
        return {};
    }

    std::string line;
    if (!std::getline(stream, line)) {
        return {};
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const auto header = headerMap(splitTabs(line));
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string> row = splitTabs(line);
        const std::string videoFile = field(header, row, "video_file");
        if (!videoFile.empty()) {
            return videoIndex.parent_path() / videoFile;
        }
    }
    return {};
}

ResolveResult resolveRunPaths(const Options& options, const char* argv0) {
    ResolveResult result;
    const std::filesystem::path exeDir = std::filesystem::absolute(argv0).parent_path();
    const auto log = firstExistingPath(candidateLogPaths(options.run, options.logsDir, exeDir));
    if (!log) {
        return result;
    }
    result.log = *log;

    RunPaths paths;
    paths.log = *log;
    paths.videoIndex = paths.log;
    paths.videoIndex.replace_extension(".video.tsv");

    std::vector<std::filesystem::path> videoCandidates;
    if (std::filesystem::is_regular_file(paths.videoIndex)) {
        if (auto indexedVideo = videoPathFromIndex(paths.videoIndex)) {
            videoCandidates.push_back(*indexedVideo);
        }
    }

    std::filesystem::path mp4 = paths.log;
    mp4.replace_extension(".mp4");
    std::filesystem::path avi = paths.log;
    avi.replace_extension(".avi");
    videoCandidates.push_back(mp4);
    videoCandidates.push_back(avi);
    result.videoCandidates = videoCandidates;

    const auto video = firstExistingPath(videoCandidates);
    if (!video) {
        return result;
    }
    paths.video = *video;
    result.paths = paths;
    return result;
}

std::vector<SensorSample> loadSamples(const std::filesystem::path& logPath) {
    std::ifstream stream(logPath);
    if (!stream) {
        throw std::runtime_error("could not open log file");
    }

    std::string line;
    if (!std::getline(stream, line)) {
        throw std::runtime_error("log file is empty");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const auto header = headerMap(splitTabs(line));

    std::vector<SensorSample> samples;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (trim(line).empty()) {
            continue;
        }

        const std::vector<std::string> row = splitTabs(line);
        const std::optional<double> elapsed = parseDouble(field(header, row, "elapsed_s"));
        if (!elapsed) {
            continue;
        }

        SensorSample sample;
        sample.elapsedSec = *elapsed;
        sample.torr = parseDouble(field(header, row, "torr"));
        sample.kilovolts = parseDouble(field(header, row, "kv"));
        sample.milliamps = parseDouble(field(header, row, "ma"));
        sample.mode = field(header, row, "mode");
        sample.valveSteps = parseLong(field(header, row, "valve_steps"));
        if (std::optional<long> power = parseLong(field(header, row, "power_present"))) {
            sample.powerPresent = static_cast<int>(*power);
        }
        samples.push_back(std::move(sample));
    }

    std::sort(samples.begin(), samples.end(), [](const SensorSample& a, const SensorSample& b) {
        return a.elapsedSec < b.elapsedSec;
    });
    return samples;
}

std::vector<double> loadFrameTimes(const std::filesystem::path& videoIndexPath) {
    std::ifstream stream(videoIndexPath);
    if (!stream) {
        return {};
    }

    std::string line;
    if (!std::getline(stream, line)) {
        return {};
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const auto header = headerMap(splitTabs(line));

    std::vector<double> frameTimes;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string> row = splitTabs(line);
        const std::optional<double> elapsed = parseDouble(field(header, row, "elapsed_s"));
        const std::optional<long> frameIndex = parseLong(field(header, row, "video_frame_index"));
        if (!elapsed || !frameIndex || *frameIndex <= 0) {
            continue;
        }
        const std::size_t zeroBasedIndex = static_cast<std::size_t>(*frameIndex - 1);
        if (frameTimes.size() <= zeroBasedIndex) {
            frameTimes.resize(zeroBasedIndex + 1, -1.0);
        }
        frameTimes[zeroBasedIndex] = *elapsed;
    }

    return frameTimes;
}

std::vector<FrameTimePoint> buildFrameTimeline(const std::vector<double>& frameTimes) {
    std::vector<FrameTimePoint> timeline;
    timeline.reserve(frameTimes.size());
    for (std::size_t i = 0; i < frameTimes.size(); ++i) {
        if (frameTimes[i] >= 0.0 && std::isfinite(frameTimes[i])) {
            timeline.push_back({frameTimes[i], i});
        }
    }
    std::sort(timeline.begin(), timeline.end(), [](const FrameTimePoint& a, const FrameTimePoint& b) {
        return a.elapsedSec < b.elapsedSec;
    });
    return timeline;
}

double playbackDurationSec(const std::vector<SensorSample>& samples,
                           const std::vector<FrameTimePoint>& frameTimeline,
                           double fps,
                           int frameCount) {
    double duration = 0.0;
    if (!samples.empty()) {
        duration = std::max(duration, samples.back().elapsedSec);
    }
    if (!frameTimeline.empty()) {
        duration = std::max(duration, frameTimeline.back().elapsedSec);
    }
    if (fps > 0.0 && frameCount > 0) {
        duration = std::max(duration, static_cast<double>(frameCount) / fps);
    }
    return duration;
}

std::size_t sampleIndexForElapsed(const std::vector<SensorSample>& samples, double elapsedSec) {
    if (samples.empty()) {
        return 0;
    }
    const auto it = std::upper_bound(
        samples.begin(),
        samples.end(),
        elapsedSec,
        [](double value, const SensorSample& sample) {
            return value < sample.elapsedSec;
        }
    );
    if (it == samples.begin()) {
        return 0;
    }
    return static_cast<std::size_t>((it - 1) - samples.begin());
}

std::size_t frameIndexForElapsed(const std::vector<FrameTimePoint>& frameTimeline,
                                 double elapsedSec,
                                 double fps,
                                 int frameCount) {
    std::size_t frameIndex = 0;
    if (!frameTimeline.empty()) {
        const auto it = std::lower_bound(
            frameTimeline.begin(),
            frameTimeline.end(),
            elapsedSec,
            [](const FrameTimePoint& point, double value) {
                return point.elapsedSec < value;
            }
        );
        if (it == frameTimeline.end()) {
            frameIndex = frameTimeline.back().frameIndex;
        } else {
            frameIndex = it->frameIndex;
        }
    } else if (fps > 0.0) {
        frameIndex = static_cast<std::size_t>(std::max(0.0, std::round(elapsedSec * fps)));
    }

    if (frameCount > 0) {
        frameIndex = std::min(frameIndex, static_cast<std::size_t>(frameCount - 1));
    }
    return frameIndex;
}

double frameElapsedSec(const cv::VideoCapture& capture,
                       const std::vector<double>& frameTimes,
                       std::size_t frameIndex,
                       double fps) {
    if (frameIndex < frameTimes.size() && frameTimes[frameIndex] >= 0.0) {
        return frameTimes[frameIndex];
    }

    const double msec = capture.get(cv::CAP_PROP_POS_MSEC);
    if (msec > 0.0) {
        return msec / 1000.0;
    }

    return fps > 0.0 ? static_cast<double>(frameIndex) / fps : 0.0;
}

WindowSize playbackWindowSize(int sourceWidth, int sourceHeight, bool vertical) {
    int displayWidth = sourceWidth > 0 ? sourceWidth : 1280;
    int displayHeight = sourceHeight > 0 ? sourceHeight : 720;
    if (vertical) {
        std::swap(displayWidth, displayHeight);
    }

    constexpr int kMaxWindowWidth = 1920;
    constexpr int kMaxWindowHeight = 1080;
    constexpr int kMinLandscapeWidth = 960;
    constexpr int kMinLandscapeHeight = 540;
    constexpr int kMinVerticalWidth = 540;
    constexpr int kMinVerticalHeight = 960;

    const double maxScale = std::min(
        static_cast<double>(kMaxWindowWidth) / static_cast<double>(displayWidth),
        static_cast<double>(kMaxWindowHeight) / static_cast<double>(displayHeight)
    );

    double scale = std::min(1.0, maxScale);
    if (vertical) {
        if (displayWidth * scale < kMinVerticalWidth && displayHeight * maxScale >= kMinVerticalHeight) {
            scale = std::min(maxScale, static_cast<double>(kMinVerticalHeight) / static_cast<double>(displayHeight));
        }
    } else {
        if (displayWidth * scale < kMinLandscapeWidth || displayHeight * scale < kMinLandscapeHeight) {
            const double minScale = std::max(
                static_cast<double>(kMinLandscapeWidth) / static_cast<double>(displayWidth),
                static_cast<double>(kMinLandscapeHeight) / static_cast<double>(displayHeight)
            );
            scale = std::min(maxScale, minScale);
        }
    }

    WindowSize size;
    size.width = std::max(1, static_cast<int>(std::round(displayWidth * scale)));
    size.height = std::max(1, static_cast<int>(std::round(displayHeight * scale)));
    return size;
}

WindowSize transformedVideoSize(int sourceWidth, int sourceHeight, bool vertical) {
    WindowSize size;
    size.width = sourceWidth > 0 ? sourceWidth : 1280;
    size.height = sourceHeight > 0 ? sourceHeight : 720;
    if (vertical) {
        std::swap(size.width, size.height);
    }

    // Most video encoders are happier with even dimensions.
    if (size.width > 1 && size.width % 2 != 0) {
        --size.width;
    }
    if (size.height > 1 && size.height % 2 != 0) {
        --size.height;
    }
    return size;
}

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

std::filesystem::path defaultSavePath(const RunPaths& paths, const Options& options) {
    std::string stem = paths.video.stem().string() + "_overlay";
    if (options.vertical) {
        stem += "_vertical";
    }
    if (options.flip) {
        stem += "_flip";
    }
    return paths.video.parent_path() / (stem + ".mp4");
}

std::filesystem::path resolvedSavePath(const RunPaths& paths, const Options& options) {
    std::filesystem::path output = options.savePath.value_or(defaultSavePath(paths, options));
    if (output.extension().empty()) {
        output.replace_extension(".mp4");
    }

    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(output, error);
    return error ? output : absolute;
}

int videoWriterFourcc(const std::filesystem::path& outputPath) {
    const std::string extension = lowercaseExtension(outputPath);
    if (extension == ".avi") {
        return cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    }
    return cv::VideoWriter::fourcc('m', 'p', '4', 'v');
}

std::string playbackOrientationDescription(const Options& options) {
    if (!options.vertical && !options.flip) {
        return "source";
    }

    std::string description = options.vertical ? "vertical, rotated 90 degrees clockwise" : "source";
    if (options.flip) {
        description += ", flipped 180 degrees";
    }
    return description;
}

double frameDelaySec(const std::vector<double>& frameTimes,
                     std::size_t frameIndex,
                     double fps,
                     double speed) {
    double delay = fps > 0.0 ? 1.0 / fps : 1.0 / 30.0;
    if (frameIndex + 1 < frameTimes.size()
        && frameTimes[frameIndex] >= 0.0
        && frameTimes[frameIndex + 1] >= frameTimes[frameIndex]) {
        delay = frameTimes[frameIndex + 1] - frameTimes[frameIndex];
    }
    return std::clamp(delay / std::max(speed, 0.001), 0.001, 0.25);
}

const SensorSample* sampleAtElapsed(const std::vector<SensorSample>& samples,
                                    double elapsedSec,
                                    std::size_t& sampleIndex) {
    if (samples.empty()) {
        return nullptr;
    }
    if (sampleIndex >= samples.size()) {
        sampleIndex = samples.size() - 1;
    }
    while (sampleIndex > 0 && samples[sampleIndex].elapsedSec > elapsedSec) {
        --sampleIndex;
    }
    while (sampleIndex + 1 < samples.size()
           && samples[sampleIndex + 1].elapsedSec <= elapsedSec) {
        ++sampleIndex;
    }
    return &samples[sampleIndex];
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

HudMetricData makePlaybackMetric(const char* label,
                                 const char* unit,
                                 const std::optional<double>& value,
                                 int precision,
                                 double zeroBand) {
    HudMetricData data{};
    data.label = label;
    data.unit = unit;
    data.hasReading = value.has_value();
    data.connected = value.has_value();
    data.live = value.has_value();
    data.status = value ? "Live" : "Waiting";
    if (value) {
        data.value = formatFixedValue(*value, precision, zeroBand);
    }
    return data;
}

std::optional<double> reportableCurrentMilliamps(const SensorSample* sample) {
    if (!sample || !sample->milliamps) {
        return {};
    }
    return *sample->milliamps < kCurrentReportFloorMA ? 0.0 : *sample->milliamps;
}

SensorHudData makeSensorHudData(const SensorSample* sample) {
    SensorHudData data{};
    data.cameraConnected = true;
    data.recording = true;
    data.loggingAvailable = true;

    data.pressure.label = "PRESSURE";
    data.pressure.unit = "Torr";
    data.pressure.hasReading = sample && sample->torr.has_value();
    data.pressure.connected = data.pressure.hasReading;
    data.pressure.live = data.pressure.hasReading;
    data.pressure.status = data.pressure.hasReading ? "Live" : "Waiting";
    if (sample && sample->torr) {
        data.pressure.value = formatPressureValue(*sample->torr, data.pressure.unit);
    }

    data.voltage = makePlaybackMetric("VOLTAGE", "kV", sample ? sample->kilovolts : std::optional<double>{}, 2, kVoltageZeroBandKV);
    data.current = makePlaybackMetric("CURRENT", "mA", reportableCurrentMilliamps(sample), 3, 0.0);
    return data;
}

ControlHudMode controlModeFromLog(const std::string& mode) {
    if (mode == "EVACUATE") {
        return ControlHudMode::Evacuate;
    }
    if (mode == "FAULT") {
        return ControlHudMode::Fault;
    }
    if (mode == "MANUAL" || mode == "LEAK") {
        return ControlHudMode::Manual;
    }
    return ControlHudMode::Idle;
}

ControlHudData makeControlHudData(const SensorSample* sample) {
    ControlHudData data{};
    if (!sample) {
        return data;
    }

    data.mode = controlModeFromLog(sample->mode);
    data.virtualPositionSteps = sample->valveSteps.value_or(0);
    data.valveConnected = sample->valveSteps.has_value();
    data.valveLive = sample->valveSteps.has_value();
    data.evacuateAvailable = data.valveLive;
    data.evacuateSelected = data.mode == ControlHudMode::Evacuate;
    data.manualAvailable = false;
    data.manualSelected = data.mode == ControlHudMode::Manual;
    data.ventAvailable = false;
    data.ventBlockedByPower = sample->powerPresent.value_or(0) != 0;
    return data;
}

bool makeVideoFrameSnapshot(
    const cv::Mat& bgr,
    std::uint64_t frameId,
    bool rotateClockwise,
    bool flipFinal,
    VideoFrameSnapshot& frame
) {
    if (bgr.empty()) {
        return false;
    }

    const cv::Mat* sourceBgr = &bgr;
    cv::Mat rotatedBgr;
    cv::Mat flippedBgr;
    if (rotateClockwise) {
        cv::rotate(bgr, rotatedBgr, cv::ROTATE_90_CLOCKWISE);
        sourceBgr = &rotatedBgr;
    }
    if (flipFinal) {
        cv::flip(*sourceBgr, flippedBgr, -1);
        sourceBgr = &flippedBgr;
    }

    cv::Mat rgb;
    cv::cvtColor(*sourceBgr, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous()) {
        rgb = rgb.clone();
    }

    const std::size_t byteCount = static_cast<std::size_t>(rgb.cols) * static_cast<std::size_t>(rgb.rows) * 3U;
    frame.connected = true;
    frame.hasFrame = true;
    frame.width = rgb.cols;
    frame.height = rgb.rows;
    frame.frameId = frameId;
    frame.rgb.assign(rgb.data, rgb.data + byteCount);
    frame.receivedAt = std::chrono::steady_clock::now();
    frame.status = VideoStreamStatus::Live;
    return true;
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

void requestTimelineSeek(PlaybackUiState& state, double elapsedSec) {
    if (state.durationSec <= 0.0) {
        return;
    }
    state.pendingSeekElapsedSec = std::clamp(elapsedSec, 0.0, state.durationSec);
    state.hasPendingSeek = true;
}

float timelineAlpha(const PlaybackUiState& state) {
    const double age = glfwGetTime() - state.lastPointerActivityAt;
    if (age < 0.0 || age > kTimelineVisibleSec) {
        return 0.0f;
    }
    if (age > kTimelineVisibleSec - kTimelineFadeSec) {
        return static_cast<float>((kTimelineVisibleSec - age) / kTimelineFadeSec);
    }
    return 1.0f;
}

bool cursorFramebufferPosition(GLFWwindow* window, double cursorX, double cursorY, float& x, float& y) {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (windowWidth <= 0 || windowHeight <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }

    x = static_cast<float>(cursorX) * static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
    y = static_cast<float>(cursorY) * static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);
    return true;
}

double timelineElapsedForX(const PlaybackUiState& state, float x) {
    const HudRect& track = state.timelineLayout.track;
    if (track.width <= 0.0f || state.durationSec <= 0.0) {
        return state.currentElapsedSec;
    }
    const float ratio = std::clamp((x - track.x) / track.width, 0.0f, 1.0f);
    return static_cast<double>(ratio) * state.durationSec;
}

void playbackCursorCallback(GLFWwindow* window, double cursorX, double cursorY) {
    auto* state = static_cast<PlaybackUiState*>(glfwGetWindowUserPointer(window));
    if (!state) {
        return;
    }

    state->lastPointerActivityAt = glfwGetTime();
    if (!state->draggingTimeline) {
        return;
    }

    float x = 0.0f;
    float y = 0.0f;
    if (cursorFramebufferPosition(window, cursorX, cursorY, x, y)) {
        requestTimelineSeek(*state, timelineElapsedForX(*state, x));
    }
}

void playbackMouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    auto* state = static_cast<PlaybackUiState*>(glfwGetWindowUserPointer(window));
    if (!state) {
        return;
    }

    state->lastPointerActivityAt = glfwGetTime();
    if (action == GLFW_RELEASE) {
        state->draggingTimeline = false;
        return;
    }
    if (action != GLFW_PRESS || timelineAlpha(*state) <= 0.0f || state->durationSec <= 0.0) {
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    float x = 0.0f;
    float y = 0.0f;
    if (!cursorFramebufferPosition(window, cursorX, cursorY, x, y)) {
        return;
    }

    if (state->timelineLayout.panel.contains(x, y)) {
        state->draggingTimeline = true;
        requestTimelineSeek(*state, timelineElapsedForX(*state, x));
    }
}

void playbackScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* state = static_cast<PlaybackUiState*>(glfwGetWindowUserPointer(window));
    if (!state || state->durationSec <= 0.0) {
        return;
    }

    state->lastPointerActivityAt = glfwGetTime();
    const double scroll = std::abs(xoffset) > std::abs(yoffset) ? xoffset : yoffset;
    if (std::abs(scroll) <= 0.0) {
        return;
    }

    const double baseElapsed = state->hasPendingSeek ? state->pendingSeekElapsedSec : state->currentElapsedSec;
    requestTimelineSeek(*state, baseElapsed + scroll * kTimelineScrollStepSec);
}

bool initGlfwWindow(GLFWwindow*& window, int width, int height, bool visible = true) {
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    window = glfwCreateWindow(width, height, "Fusor Playback", nullptr, nullptr);
    if (!window) {
        std::cout << "Failed to create playback window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(visible ? 1 : 0);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    return true;
}

void configurePlaybackGlState() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
}

bool createOffscreenRenderTarget(OffscreenRenderTarget& target, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);

    glGenTextures(1, &target.colorTexture);
    glBindTexture(GL_TEXTURE_2D, target.colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.colorTexture, 0);

    glGenRenderbuffers(1, &target.depthStencil);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depthStencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target.depthStencil);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        std::cout << "Failed to create offscreen playback framebuffer\n";
        return false;
    }

    target.width = width;
    target.height = height;
    return true;
}

void destroyOffscreenRenderTarget(OffscreenRenderTarget& target) {
    if (target.depthStencil) {
        glDeleteRenderbuffers(1, &target.depthStencil);
    }
    if (target.colorTexture) {
        glDeleteTextures(1, &target.colorTexture);
    }
    if (target.framebuffer) {
        glDeleteFramebuffers(1, &target.framebuffer);
    }
    target = {};
}

void drawPlaybackBaseFrame(
    HudRenderer& hudRenderer,
    VideoBackgroundRenderer& videoBackgroundRenderer,
    const VideoFrameSnapshot& videoFrame,
    int framebufferWidth,
    int framebufferHeight,
    const SensorSample* sample,
    const Options& options
) {
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.055f, 0.06f, 0.078f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    updateVideoBackgroundTexture(videoBackgroundRenderer, videoFrame);
    drawVideoBackground(videoBackgroundRenderer, framebufferWidth, framebufferHeight);

    const SensorHudData sensorHudData = makeSensorHudData(sample);
    const SensorHudLayout sensorLayout = drawSensorHud(hudRenderer, framebufferWidth, framebufferHeight, sensorHudData);
    if (options.showValveControl) {
        const ControlHudData controlHudData = makeControlHudData(sample);
        drawControlHud(hudRenderer, framebufferWidth, framebufferHeight, sensorLayout.panel, controlHudData);
    }
}

bool readFramebufferBgr(int width, int height, std::vector<unsigned char>& pixels, cv::Mat& bgr) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (glGetError() != GL_NO_ERROR) {
        std::cout << "Failed to read rendered playback frame\n";
        return false;
    }

    cv::Mat glRgb(height, width, CV_8UC3, pixels.data());
    cv::Mat topRgb;
    cv::flip(glRgb, topRgb, 0);
    cv::cvtColor(topRgb, bgr, cv::COLOR_RGB2BGR);
    return true;
}

void processPlaybackInput(GLFWwindow* window, bool& paused, bool& spaceWasDown) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS
        || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    const bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (spaceDown && !spaceWasDown) {
        paused = !paused;
    }
    spaceWasDown = spaceDown;
}

bool applyPendingSeek(PlaybackUiState& state,
                      cv::VideoCapture& capture,
                      const std::vector<SensorSample>& samples,
                      const std::vector<FrameTimePoint>& frameTimeline,
                      double fps,
                      int frameCount,
                      std::size_t& frameIndex,
                      std::size_t& sampleIndex,
                      bool& haveFrame) {
    if (!state.hasPendingSeek) {
        return false;
    }

    const double targetElapsed = std::clamp(state.pendingSeekElapsedSec, 0.0, state.durationSec);
    const std::size_t targetFrame = frameIndexForElapsed(frameTimeline, targetElapsed, fps, frameCount);
    capture.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(targetFrame));
    frameIndex = targetFrame;
    sampleIndex = sampleIndexForElapsed(samples, targetElapsed);
    haveFrame = false;
    state.currentElapsedSec = targetElapsed;
    state.hasPendingSeek = false;
    return true;
}

int runPlayback(const RunPaths& paths, const Options& options) {
    const std::vector<SensorSample> samples = loadSamples(paths.log);
    const std::vector<double> frameTimes = loadFrameTimes(paths.videoIndex);
    const std::vector<FrameTimePoint> frameTimeline = buildFrameTimeline(frameTimes);
    if (samples.empty()) {
        std::cout << "Playback warning: no sensor samples loaded from " << paths.log.string() << "\n";
    }

    cv::VideoCapture capture(paths.video.string());
    if (!capture.isOpened()) {
        std::cout << "Could not open video " << paths.video.string() << "\n";
        return 2;
    }

    double fps = capture.get(cv::CAP_PROP_FPS);
    if (!std::isfinite(fps) || fps <= 0.0) {
        fps = 30.0;
    }

    const int frameCount = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    const double durationSec = playbackDurationSec(samples, frameTimeline, fps, frameCount);
    const int sourceWidth = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int sourceHeight = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const WindowSize windowSize = playbackWindowSize(sourceWidth, sourceHeight, options.vertical);

    GLFWwindow* window = nullptr;
    if (!initGlfwWindow(window, windowSize.width, windowSize.height)) {
        return 1;
    }

    PlaybackUiState uiState{};
    uiState.durationSec = durationSec;
    glfwSetWindowUserPointer(window, &uiState);
    glfwSetCursorPosCallback(window, playbackCursorCallback);
    glfwSetMouseButtonCallback(window, playbackMouseButtonCallback);
    glfwSetScrollCallback(window, playbackScrollCallback);

    HudRenderer hudRenderer = createHudRenderer();
    VideoBackgroundRenderer videoBackgroundRenderer = createVideoBackgroundRenderer();

    configurePlaybackGlState();

    std::cout << "Playback log:   " << paths.log.string() << "\n";
    std::cout << "Playback video: " << paths.video.string() << "\n";
    if (std::filesystem::is_regular_file(paths.videoIndex)) {
        std::cout << "Frame index:    " << paths.videoIndex.string() << "\n";
    }
    if (options.vertical || options.flip) {
        std::cout << "Playback video orientation: " << playbackOrientationDescription(options) << "\n";
    }
    std::cout << "Space pauses, mouse shows timeline, wheel/drag seeks, q/Esc quits.\n";

    bool paused = false;
    bool spaceWasDown = false;
    std::size_t frameIndex = 0;
    std::size_t sampleIndex = 0;
    cv::Mat bgr;
    VideoFrameSnapshot videoFrame;
    bool haveFrame = false;

    while (!glfwWindowShouldClose(window)) {
        applyPendingSeek(
            uiState,
            capture,
            samples,
            frameTimeline,
            fps,
            frameCount,
            frameIndex,
            sampleIndex,
            haveFrame
        );

        if (!haveFrame || !paused) {
            if (!capture.read(bgr) || !makeVideoFrameSnapshot(bgr, frameIndex + 1, options.vertical, options.flip, videoFrame)) {
                if (!videoFrame.hasFrame) {
                    break;
                }
                paused = true;
                haveFrame = true;
                if (frameCount > 0) {
                    frameIndex = std::min(frameIndex, static_cast<std::size_t>(frameCount - 1));
                } else if (frameIndex > 0) {
                    --frameIndex;
                }
            } else {
                haveFrame = true;
            }
        }

        const double elapsedSec = frameElapsedSec(capture, frameTimes, frameIndex, fps);
        uiState.currentElapsedSec = elapsedSec;
        uiState.durationSec = durationSec;
        const SensorSample* sample = sampleAtElapsed(samples, elapsedSec, sampleIndex);

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        drawPlaybackBaseFrame(hudRenderer, videoBackgroundRenderer, videoFrame, framebufferWidth, framebufferHeight, sample, options);

        const float timelineOverlayAlpha = timelineAlpha(uiState);
        if (timelineOverlayAlpha > 0.0f && durationSec > 0.0) {
            PlaybackTimelineData timelineData{};
            timelineData.visible = true;
            timelineData.paused = paused;
            timelineData.elapsedSec = elapsedSec;
            timelineData.durationSec = durationSec;
            timelineData.alpha = timelineOverlayAlpha;
            uiState.timelineLayout = drawPlaybackTimeline(hudRenderer, framebufferWidth, framebufferHeight, timelineData);
        } else {
            uiState.timelineLayout = {};
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
        processPlaybackInput(window, paused, spaceWasDown);
        const bool seekedAfterDraw = applyPendingSeek(
            uiState,
            capture,
            samples,
            frameTimeline,
            fps,
            frameCount,
            frameIndex,
            sampleIndex,
            haveFrame
        );

        if (!paused && !seekedAfterDraw) {
            const double delaySec = frameDelaySec(frameTimes, frameIndex, fps, options.speed);
            ++frameIndex;
            haveFrame = false;
            std::this_thread::sleep_for(std::chrono::duration<double>(delaySec));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    destroyVideoBackgroundRenderer(videoBackgroundRenderer);
    destroyHudRenderer(hudRenderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

int savePlayback(const RunPaths& paths, const Options& options) {
    const std::vector<SensorSample> samples = loadSamples(paths.log);
    const std::vector<double> frameTimes = loadFrameTimes(paths.videoIndex);
    if (samples.empty()) {
        std::cout << "Playback warning: no sensor samples loaded from " << paths.log.string() << "\n";
    }

    cv::VideoCapture capture(paths.video.string());
    if (!capture.isOpened()) {
        std::cout << "Could not open video " << paths.video.string() << "\n";
        return 2;
    }

    double fps = capture.get(cv::CAP_PROP_FPS);
    if (!std::isfinite(fps) || fps <= 0.0) {
        fps = 30.0;
    }

    const int frameCount = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    const int sourceWidth = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int sourceHeight = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const WindowSize outputSize = transformedVideoSize(sourceWidth, sourceHeight, options.vertical);
    const std::filesystem::path outputPath = resolvedSavePath(paths, options);

    std::error_code error;
    if (std::filesystem::exists(outputPath, error)
        && std::filesystem::equivalent(outputPath, paths.video, error)) {
        std::cout << "Save path matches the source video; choose a different output path\n";
        return 2;
    }

    const std::filesystem::path outputParent = outputPath.parent_path();
    if (!outputParent.empty()) {
        std::filesystem::create_directories(outputParent, error);
        if (error) {
            std::cout << "Could not create output directory " << outputParent.string() << "\n";
            return 2;
        }
    }

    const double outputFps = std::clamp(fps * std::max(options.speed, 0.001), 1.0, 240.0);
    cv::VideoWriter writer;
    if (!writer.open(
            outputPath.string(),
            videoWriterFourcc(outputPath),
            outputFps,
            cv::Size(outputSize.width, outputSize.height),
            true
        )) {
        std::cout << "Could not open output video " << outputPath.string() << "\n";
        return 2;
    }

    GLFWwindow* window = nullptr;
    if (!initGlfwWindow(window, 64, 64, false)) {
        return 1;
    }

    OffscreenRenderTarget renderTarget{};
    if (!createOffscreenRenderTarget(renderTarget, outputSize.width, outputSize.height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    HudRenderer hudRenderer = createHudRenderer();
    VideoBackgroundRenderer videoBackgroundRenderer = createVideoBackgroundRenderer();
    configurePlaybackGlState();

    std::size_t sampleIndex = 0;
    std::size_t frameIndex = 0;
    std::uint64_t writtenFrames = 0;
    cv::Mat bgr;
    cv::Mat renderedBgr;
    VideoFrameSnapshot videoFrame;
    std::vector<unsigned char> readbackPixels;

    while (capture.read(bgr)) {
        if (!makeVideoFrameSnapshot(bgr, frameIndex + 1, options.vertical, options.flip, videoFrame)) {
            break;
        }

        const double elapsedSec = frameElapsedSec(capture, frameTimes, frameIndex, fps);
        const SensorSample* sample = sampleAtElapsed(samples, elapsedSec, sampleIndex);

        glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.framebuffer);
        drawPlaybackBaseFrame(
            hudRenderer,
            videoBackgroundRenderer,
            videoFrame,
            renderTarget.width,
            renderTarget.height,
            sample,
            options
        );

        if (!readFramebufferBgr(renderTarget.width, renderTarget.height, readbackPixels, renderedBgr)) {
            break;
        }
        writer.write(renderedBgr);

        ++writtenFrames;
        ++frameIndex;
        if (frameCount > 0 && writtenFrames % 300 == 0) {
            std::cout << "Saved " << writtenFrames << " / " << frameCount << " frames\r";
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    writer.release();
    capture.release();

    destroyVideoBackgroundRenderer(videoBackgroundRenderer);
    destroyHudRenderer(hudRenderer);
    destroyOffscreenRenderTarget(renderTarget);
    glfwDestroyWindow(window);
    glfwTerminate();

    if (frameCount > 0 && writtenFrames >= 300) {
        std::cout << "\n";
    }
    if (writtenFrames == 0) {
        std::cout << "No frames were saved\n";
        return 2;
    }

    std::cout << "Saved playback video: " << outputPath.string() << "\n";
    return 0;
}

bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            return false;
        }
        if (arg == "--probe") {
            options.probeOnly = true;
            continue;
        }
        if (arg == "--hide-valve-control") {
            options.showValveControl = false;
            continue;
        }
        if (arg == "--vertical") {
            options.vertical = true;
            continue;
        }
        if (arg == "--flip") {
            options.flip = true;
            continue;
        }
        if (arg == "--save") {
            options.save = true;
            if (!options.run.empty() && i + 1 < argc) {
                const std::string next = argv[i + 1];
                if (!isFlagLike(next)) {
                    options.savePath = argv[++i];
                }
            }
            continue;
        }
        if (arg == "--output" || arg == "--save-path") {
            if (i + 1 >= argc) {
                std::cout << "Missing value for " << arg << "\n";
                return false;
            }
            options.save = true;
            options.savePath = argv[++i];
            continue;
        }
        if (arg == "--logs-dir") {
            if (i + 1 >= argc) {
                std::cout << "Missing value for --logs-dir\n";
                return false;
            }
            options.logsDir = argv[++i];
            continue;
        }
        if (arg == "--speed") {
            if (i + 1 >= argc || !parsePositiveDouble(argv[++i], options.speed)) {
                std::cout << "Invalid --speed value\n";
                return false;
            }
            continue;
        }
        if (options.run.empty()) {
            options.run = arg;
            continue;
        }
        std::cout << "Unexpected argument: " << arg << "\n";
        return false;
    }
    return !options.run.empty();
}

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  Fusor Playback <run-number|log-path> [--speed N] [--logs-dir DIR] [--hide-valve-control] [--vertical] [--flip] [--save [OUTPUT]]\n\n"
        << "Examples:\n"
        << "  Fusor Playback 10\n"
        << "  Fusor Playback logs\\run_10.log --speed 0.5\n"
        << "  Fusor Playback 10 --logs-dir logs\n"
        << "  Fusor Playback 10 --hide-valve-control\n"
        << "  Fusor Playback 10 --vertical\n"
        << "  Fusor Playback 10 --vertical --flip\n"
        << "  Fusor Playback 10 --vertical --save\n"
        << "  Fusor Playback 10 --vertical --save logs\\run_10_overlay.mp4\n\n"
        << "Keys:\n"
        << "  Space: pause/resume\n"
        << "  Mouse move: show timeline\n"
        << "  Wheel or drag timeline: seek\n"
        << "  q or Esc: quit\n";
}

void printVideoCandidates(const std::vector<std::filesystem::path>& candidates) {
    if (candidates.empty()) {
        return;
    }
    std::cout << "Checked video paths:\n";
    for (const std::filesystem::path& candidate : candidates) {
        std::cout << "  " << candidate.lexically_normal().string() << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return argc > 1 ? 0 : 1;
    }

    const ResolveResult resolved = resolveRunPaths(options, argv[0]);
    if (!resolved.log) {
        std::cout << "Could not find run log for: " << options.run << "\n";
        std::cout << "Expected a file like logs\\run_N.log.\n";
        std::cout << "Use --logs-dir if your logs are outside the repo logs folder.\n";
        return 2;
    }
    if (!resolved.paths) {
        std::cout << "Found log but no matching video for: " << resolved.log->string() << "\n";
        std::cout << "Expected a matching .mp4 or .avi beside the log.\n";
        printVideoCandidates(resolved.videoCandidates);
        return 2;
    }
    if (options.probeOnly) {
        std::cout << "Playback log:   " << resolved.paths->log.string() << "\n";
        std::cout << "Playback video: " << resolved.paths->video.string() << "\n";
        if (std::filesystem::is_regular_file(resolved.paths->videoIndex)) {
            std::cout << "Frame index:    " << resolved.paths->videoIndex.string() << "\n";
        }
        if (options.vertical || options.flip) {
            std::cout << "Orientation:    " << playbackOrientationDescription(options) << "\n";
        }
        if (options.save) {
            std::cout << "Save output:    " << resolvedSavePath(*resolved.paths, options).string() << "\n";
        }
        return 0;
    }

    try {
        if (options.save) {
            return savePlayback(*resolved.paths, options);
        }
        return runPlayback(*resolved.paths, options);
    } catch (const std::exception& ex) {
        std::cout << "Playback failed: " << ex.what() << "\n";
        return 2;
    }
}

#include "Video/VideoStream.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace {

constexpr int kOpenTimeoutMs = 5000;
constexpr int kReadTimeoutMs = 5000;
constexpr int kMaxConsecutiveReadFailures = 3;
constexpr auto kReadFailureBackoff = std::chrono::milliseconds(150);
constexpr const char* kOpenCvFfmpegRtspOptions =
    "rtsp_transport;tcp|allowed_media_types;video|max_delay;500000|reorder_queue_size;0";

const char* statusName(VideoStreamStatus status) {
    switch (status) {
    case VideoStreamStatus::Connecting:
        return "connecting";
    case VideoStreamStatus::Live:
        return "live";
    case VideoStreamStatus::Stalled:
        return "stalled";
    case VideoStreamStatus::Reconnecting:
        return "reconnecting";
    case VideoStreamStatus::Offline:
    default:
        return "offline";
    }
}

std::string frameDetail(int width, int height) {
    std::ostringstream out;
    out << width << "x" << height;
    return out.str();
}

void configureOpenCvFfmpegRtspOptions() {
    static std::once_flag once;
    std::call_once(once, []() {
        const char* overrideOptions = std::getenv("FUSOR_MONITOR_FFMPEG_CAPTURE_OPTIONS");
        const char* options = overrideOptions && overrideOptions[0] != '\0'
                                  ? overrideOptions
                                  : kOpenCvFfmpegRtspOptions;

#ifdef _WIN32
        _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", options);
#else
        setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", options, 1);
#endif
        std::cout << "camera using OPENCV_FFMPEG_CAPTURE_OPTIONS=" << options << "\n";
    });
}

} // namespace

VideoStream::~VideoStream() {
    stop();
}

void VideoStream::start(std::string url) {
    if (url.empty() || running_.exchange(true)) {
        return;
    }

    url_ = std::move(url);
    stopRequested_ = false;
    worker_ = std::thread(&VideoStream::run, this);
}

void VideoStream::requestStop() {
    stopRequested_ = true;
}

void VideoStream::stop() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
}

bool VideoStream::running() const {
    return running_.load();
}

bool VideoStream::connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.connected;
}

VideoFrameSnapshot VideoStream::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void VideoStream::setStatus(VideoStreamStatus status, std::string detail) {
    bool changed = false;
    std::string logDetail;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = snapshot_.status != status || snapshot_.detail != detail;
        snapshot_.status = status;
        snapshot_.detail = std::move(detail);
        snapshot_.connected = status == VideoStreamStatus::Live;
        logDetail = snapshot_.detail;
    }

    if (changed) {
        std::cout << "camera " << statusName(status);
        if (!logDetail.empty()) {
            std::cout << " | " << logDetail;
        }
        std::cout << "\n";
    }
}

void VideoStream::storeFrame(int width, int height, std::vector<unsigned char> rgb) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.connected = true;
    snapshot_.hasFrame = true;
    snapshot_.width = width;
    snapshot_.height = height;
    snapshot_.rgb = std::move(rgb);
    snapshot_.receivedAt = std::chrono::steady_clock::now();
    ++snapshot_.frameId;
}

void VideoStream::sleepUntilStopped(std::chrono::milliseconds duration) const {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stopRequested_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void VideoStream::run() {
    configureOpenCvFfmpegRtspOptions();

    while (!stopRequested_) {
        setStatus(VideoStreamStatus::Connecting, url_);

        cv::VideoCapture capture;
        const std::vector<int> captureParams = {
            cv::CAP_PROP_OPEN_TIMEOUT_MSEC, kOpenTimeoutMs,
            cv::CAP_PROP_READ_TIMEOUT_MSEC, kReadTimeoutMs,
        };
        if (!capture.open(url_, cv::CAP_FFMPEG, captureParams)) {
            setStatus(VideoStreamStatus::Offline, "open failed");
            sleepUntilStopped(std::chrono::seconds(2));
            if (!stopRequested_) {
                setStatus(VideoStreamStatus::Reconnecting, "retrying");
            }
            continue;
        }
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

        bool sawFrame = false;
        int consecutiveReadFailures = 0;
        cv::Mat bgr;
        cv::Mat rgb;

        while (!stopRequested_) {
            if (!capture.read(bgr) || bgr.empty()) {
                ++consecutiveReadFailures;
                setStatus(
                    sawFrame ? VideoStreamStatus::Stalled : VideoStreamStatus::Offline,
                    sawFrame ? "waiting for frame" : "waiting for first frame"
                );
                if (consecutiveReadFailures >= kMaxConsecutiveReadFailures) {
                    break;
                }
                sleepUntilStopped(kReadFailureBackoff);
                continue;
            }
            consecutiveReadFailures = 0;

            cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
            if (!rgb.isContinuous()) {
                rgb = rgb.clone();
            }
            std::vector<unsigned char> pixels(
                rgb.data,
                rgb.data + static_cast<std::size_t>(rgb.total() * rgb.elemSize())
            );
            storeFrame(rgb.cols, rgb.rows, std::move(pixels));

            if (!sawFrame) {
                setStatus(VideoStreamStatus::Live, frameDetail(rgb.cols, rgb.rows));
            }
            sawFrame = true;
        }

        capture.release();
        if (!stopRequested_) {
            setStatus(VideoStreamStatus::Reconnecting, "retrying");
            sleepUntilStopped(std::chrono::seconds(2));
        }
    }

    setStatus(VideoStreamStatus::Offline, "stopped");
    running_ = false;
}

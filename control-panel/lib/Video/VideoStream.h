#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class VideoStreamStatus {
    Offline,
    Connecting,
    Live,
    Stalled,
    Reconnecting,
};

struct VideoFrameSnapshot {
    bool connected = false;
    bool hasFrame = false;
    int width = 0;
    int height = 0;
    std::uint64_t frameId = 0;
    std::vector<unsigned char> rgb;
    std::chrono::steady_clock::time_point receivedAt{};
    VideoStreamStatus status = VideoStreamStatus::Offline;
    std::string detail;
};

class VideoStream {
public:
    VideoStream() = default;
    ~VideoStream();

    VideoStream(const VideoStream&) = delete;
    VideoStream& operator=(const VideoStream&) = delete;

    void start(std::string url);
    void requestStop();
    void stop();
    bool running() const;
    bool connected() const;
    VideoFrameSnapshot snapshot() const;

private:
    void run();
    void setStatus(VideoStreamStatus status, std::string detail = {});
    void storeFrame(int width, int height, std::vector<unsigned char> rgb);
    void sleepUntilStopped(std::chrono::milliseconds duration) const;

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
    std::string url_;
    VideoFrameSnapshot snapshot_;
};

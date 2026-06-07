#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

struct PressureSnapshot {
    bool hasReading = false;
    bool connected = false;
    bool live = false;
    double torr = 0.0;
    int rssi = 0;
    std::string status = "Starting";
    std::string detail;
    std::chrono::steady_clock::time_point lastReading{};
};

class PressureMonitor {
public:
    PressureMonitor() = default;
    ~PressureMonitor();

    PressureMonitor(const PressureMonitor&) = delete;
    PressureMonitor& operator=(const PressureMonitor&) = delete;

    void start();
    void requestStop();
    void stop();
    bool running() const;
    PressureSnapshot snapshot() const;

private:
    void run();
    void handlePayload(const std::string& payload);
    void setStatus(std::string status, std::string detail, bool connected, bool live, int rssi);
    void sleepUntilStopped(std::chrono::milliseconds duration) const;

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
    PressureSnapshot snapshot_;
};

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

struct MeterSnapshot {
    bool hasVoltage = false;
    bool hasCurrent = false;
    bool connected = false;
    bool live = false;
    bool saturatedVoltage = false;
    bool saturatedCurrent = false;
    double kilovolts = 0.0;
    double milliamps = 0.0;
    int rssi = 0;
    std::string status = "Starting";
    std::string detail;
    std::chrono::steady_clock::time_point lastReading{};
};

class MeterMonitor {
public:
    MeterMonitor() = default;
    ~MeterMonitor();

    MeterMonitor(const MeterMonitor&) = delete;
    MeterMonitor& operator=(const MeterMonitor&) = delete;

    void start();
    void requestStop();
    void stop();
    bool running() const;
    MeterSnapshot snapshot() const;

private:
    void run();
    void handlePayload(const std::string& payload);
    void setStatus(std::string status, std::string detail, bool connected, bool live, int rssi);
    void sleepUntilStopped(std::chrono::milliseconds duration) const;

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
    MeterSnapshot snapshot_;
};

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

struct ValveSnapshot {
    bool connected = false;
    bool live = false;
    bool hasPosition = false;
    long positionSteps = 0;
    long openLimitSteps = 0;
    int rssi = 0;
    std::string status = "Starting";
    std::string detail;
    std::chrono::steady_clock::time_point lastReading{};
};

class ValveMonitor {
public:
    ValveMonitor() = default;
    ~ValveMonitor();

    ValveMonitor(const ValveMonitor&) = delete;
    ValveMonitor& operator=(const ValveMonitor&) = delete;

    void start();
    void requestStop();
    void stop();
    bool running() const;
    ValveSnapshot snapshot() const;

    bool sendMoveTo(long absoluteSteps);
    bool sendMove(long deltaSteps);
    bool sendClose();
    bool sendOpen();

private:
    void run();
    void handlePayload(const std::string& payload);
    void setStatus(std::string status, std::string detail, bool connected, bool live, int rssi);
    void sleepUntilStopped(std::chrono::milliseconds duration) const;
    bool enqueueCommand(std::string command);

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
    std::atomic_bool linkLive_{false};
    ValveSnapshot snapshot_;
    std::deque<std::string> outgoing_;
};

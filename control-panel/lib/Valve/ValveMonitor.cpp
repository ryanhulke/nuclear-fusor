#include "Valve/ValveMonitor.h"

#include "Ble/BleScan.h"

#include <simpleble/Config.h>
#include <simpleble/SimpleBLE.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kDeviceName = "Fusor-Valve";
constexpr const char* kServiceUuid = "9b3f2001-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* kStatusUuid = "9b3f2002-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* kCommandUuid = "9b3f2003-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr int kScanDurationMs = 5000;
constexpr int kScanPollMs = 50;
constexpr int kRunLoopPollMs = 50;

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

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool parseKeyValue(const std::string& field, std::string& key, std::string& value) {
    const auto eq = field.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    key = trim(field.substr(0, eq));
    value = trim(field.substr(eq + 1));
    return !key.empty();
}

void parseValveStatus(const std::string& payload, ValveSnapshot& snapshot) {
    snapshot.detail = payload;
    bool sawPos = false;
    std::stringstream stream(payload);
    std::string field;
    while (std::getline(stream, field, ',')) {
        std::string key;
        std::string value;
        if (!parseKeyValue(field, key, value)) {
            continue;
        }
        key = lowercase(std::move(key));
        if (key == "pos") {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str()) {
                snapshot.positionSteps = v;
                sawPos = true;
            }
        } else if (key == "open") {
            char* end = nullptr;
            const long v = std::strtol(value.c_str(), &end, 10);
            if (end != value.c_str()) {
                snapshot.openLimitSteps = v;
            }
        }
    }
    if (sawPos) {
        snapshot.hasPosition = true;
    }
}

bool advertisesService(SimpleBLE::Peripheral& peripheral) {
    try {
        for (SimpleBLE::Service service : peripheral.services()) {
            if (lowercase(service.uuid()) == kServiceUuid) {
                return true;
            }
        }
    } catch (const std::exception&) {
    }
    return false;
}

bool isValvePeripheral(SimpleBLE::Peripheral& peripheral) {
    return peripheral.identifier() == kDeviceName || advertisesService(peripheral);
}

std::optional<SimpleBLE::Adapter> firstAdapter() {
    if (!SimpleBLE::Adapter::bluetooth_enabled()) {
        return {};
    }

    std::vector<SimpleBLE::Adapter> adapters = SimpleBLE::Adapter::get_adapters();
    if (adapters.empty()) {
        return {};
    }

    return adapters.front();
}

std::optional<SimpleBLE::Peripheral> findValvePeripheral(SimpleBLE::Adapter& adapter, const std::atomic_bool& stopRequested) {
    std::cout << "SimpleBLE scanning for " << kDeviceName << " service " << kServiceUuid << "\n";

    std::vector<SimpleBLE::Peripheral> peripherals;
    {
        std::lock_guard<std::mutex> scanLock(bleScanMutex());
        if (stopRequested.load()) {
            return {};
        }

        bool scanStarted = false;
        try {
            adapter.scan_start();
            scanStarted = true;

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kScanDurationMs);
            while (!stopRequested.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kScanPollMs));
            }

            adapter.scan_stop();
            scanStarted = false;
        } catch (...) {
            if (scanStarted) {
                try {
                    adapter.scan_stop();
                } catch (const std::exception&) {
                }
            }
            throw;
        }

        if (stopRequested.load()) {
            return {};
        }

        peripherals = adapter.scan_get_results();
    }

    for (SimpleBLE::Peripheral& peripheral : peripherals) {
        if (stopRequested.load()) {
            return {};
        }
        if (isValvePeripheral(peripheral)) {
            std::cout << "SimpleBLE valve candidate: " << peripheral.identifier() << " [" << peripheral.address() << "]\n";
            return peripheral;
        }
    }

    return {};
}

bool findValveCharacteristics(
    SimpleBLE::Peripheral& peripheral,
    SimpleBLE::BluetoothUUID& serviceUuid,
    SimpleBLE::BluetoothUUID& statusUuid,
    SimpleBLE::BluetoothUUID& commandUuid
) {
    bool foundStatus = false;
    bool foundCommand = false;
    for (SimpleBLE::Service service : peripheral.services()) {
        if (lowercase(service.uuid()) != kServiceUuid) {
            continue;
        }

        for (SimpleBLE::Characteristic characteristic : service.characteristics()) {
            const std::string uuid = lowercase(characteristic.uuid());
            if (uuid == kStatusUuid) {
                serviceUuid = service.uuid();
                statusUuid = characteristic.uuid();
                foundStatus = true;
            } else if (uuid == kCommandUuid) {
                serviceUuid = service.uuid();
                commandUuid = characteristic.uuid();
                foundCommand = true;
            }
        }
    }
    return foundStatus && foundCommand;
}

} // namespace

ValveMonitor::~ValveMonitor() {
    stop();
}

void ValveMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }
    stopRequested_ = false;
    worker_ = std::thread(&ValveMonitor::run, this);
}

void ValveMonitor::requestStop() {
    stopRequested_ = true;
}

void ValveMonitor::stop() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
}

bool ValveMonitor::running() const {
    return running_.load();
}

ValveSnapshot ValveMonitor::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ValveSnapshot copy = snapshot_;
    if (copy.connected && copy.hasPosition) {
        const auto age = std::chrono::steady_clock::now() - copy.lastReading;
        if (age > std::chrono::seconds(4)) {
            copy.live = false;
            copy.status = "Waiting";
        }
    }
    return copy;
}

bool ValveMonitor::sendMoveTo(long absoluteSteps) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "MOVETO %ld", absoluteSteps);
    return enqueueCommand(buffer);
}

bool ValveMonitor::sendMove(long deltaSteps) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "MOVE %ld", deltaSteps);
    return enqueueCommand(buffer);
}

bool ValveMonitor::sendClose() {
    return enqueueCommand("CLOSE");
}

bool ValveMonitor::sendOpen() {
    return enqueueCommand("OPEN");
}

bool ValveMonitor::enqueueCommand(std::string command) {
    if (!linkLive_.load()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (outgoing_.size() >= 16) {
        outgoing_.pop_front();
    }
    outgoing_.push_back(std::move(command));
    return true;
}

void ValveMonitor::handlePayload(const std::string& payload) {
    const std::string text = trim(payload);

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.connected = true;
    if (text.empty() || text == "boot") {
        snapshot_.detail = text;
        snapshot_.live = false;
        snapshot_.status = "Waiting";
        return;
    }

    parseValveStatus(text, snapshot_);
    snapshot_.live = true;
    snapshot_.status = "Live";
    snapshot_.lastReading = std::chrono::steady_clock::now();
}

void ValveMonitor::setStatus(std::string status, std::string detail, bool connected, bool live, int rssi) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.status = std::move(status);
    snapshot_.detail = std::move(detail);
    snapshot_.connected = connected;
    snapshot_.live = live;
    snapshot_.rssi = rssi;
}

void ValveMonitor::sleepUntilStopped(std::chrono::milliseconds duration) const {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stopRequested_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void ValveMonitor::run() {
    SimpleBLE::Config::WinRT::experimental_use_own_mta_apartment = true;
    SimpleBLE::Config::WinRT::experimental_reinitialize_winrt_apartment_on_main_thread = false;

    while (!stopRequested_) {
        try {
            setStatus("Scanning", std::string(kDeviceName), false, false, 0);
            linkLive_ = false;

            std::optional<SimpleBLE::Adapter> adapter = firstAdapter();
            if (!adapter.has_value()) {
                setStatus("Offline", "No Bluetooth adapter", false, false, 0);
                sleepUntilStopped(std::chrono::seconds(3));
                continue;
            }

            std::optional<SimpleBLE::Peripheral> found = findValvePeripheral(*adapter, stopRequested_);
            if (stopRequested_) {
                break;
            }

            if (!found.has_value()) {
                setStatus("Offline", "Valve not found", false, false, 0);
                sleepUntilStopped(std::chrono::seconds(2));
                continue;
            }

            SimpleBLE::Peripheral peripheral = *found;
            setStatus("Connecting", peripheral.identifier(), false, false, peripheral.rssi());
            std::cout << "SimpleBLE connecting to valve " << peripheral.identifier() << " [" << peripheral.address() << "]\n";

            peripheral.set_callback_on_disconnected([this]() {
                std::cout << "SimpleBLE valve disconnected\n";
                linkLive_ = false;
                setStatus("Offline", "Disconnected", false, false, 0);
            });

            peripheral.connect();
            setStatus("Waiting", "Discovering services", true, false, peripheral.rssi());

            SimpleBLE::BluetoothUUID serviceUuid;
            SimpleBLE::BluetoothUUID statusUuid;
            SimpleBLE::BluetoothUUID commandUuid;
            if (!findValveCharacteristics(peripheral, serviceUuid, statusUuid, commandUuid)) {
                setStatus("Offline", "Valve characteristics unavailable", false, false, peripheral.rssi());
                peripheral.disconnect();
                sleepUntilStopped(std::chrono::seconds(2));
                continue;
            }

            bool subscribed = false;
            peripheral.notify(serviceUuid, statusUuid, [this](SimpleBLE::ByteArray payload) {
                handlePayload(std::string(payload));
            });
            subscribed = true;

            linkLive_ = true;
            setStatus("Waiting", "Waiting for status", true, false, peripheral.rssi());

            try {
                const std::string initial = std::string(peripheral.read(serviceUuid, statusUuid));
                handlePayload(initial);
            } catch (const std::exception& error) {
                std::cout << "SimpleBLE valve initial read failed: " << error.what() << "\n";
            }

            while (!stopRequested_ && peripheral.is_connected()) {
                std::deque<std::string> pending;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending.swap(outgoing_);
                }
                while (!pending.empty()) {
                    const std::string& cmd = pending.front();
                    try {
                        peripheral.write_command(serviceUuid, commandUuid, SimpleBLE::ByteArray(cmd));
                        std::cout << "SimpleBLE valve cmd sent: " << cmd << "\n";
                    } catch (const std::exception& error) {
                        std::cout << "SimpleBLE valve cmd failed: " << error.what() << "\n";
                    }
                    pending.pop_front();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(kRunLoopPollMs));
            }

            linkLive_ = false;
            if (subscribed && peripheral.is_connected()) {
                peripheral.unsubscribe(serviceUuid, statusUuid);
            }
            if (peripheral.is_connected()) {
                peripheral.disconnect();
            }
        } catch (const std::exception& error) {
            linkLive_ = false;
            setStatus("Offline", error.what(), false, false, 0);
            std::cout << "SimpleBLE valve monitor error: " << error.what() << "\n";
            sleepUntilStopped(std::chrono::seconds(2));
        }
    }

    linkLive_ = false;
    running_ = false;
}

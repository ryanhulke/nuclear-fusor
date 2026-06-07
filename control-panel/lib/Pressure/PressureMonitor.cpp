#include "Pressure/PressureMonitor.h"

#include "Ble/BleScan.h"

#include <simpleble/Config.h>
#include <simpleble/SimpleBLE.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kDeviceName = "PG-XIAO";
constexpr const char* kServiceUuid = "9b3f0001-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* kDataUuid = "9b3f0002-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr int kScanDurationMs = 5000;
constexpr int kScanPollMs = 50;

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

bool parsePressureTorr(const std::string& payload, double& torr) {
    const std::string text = trim(payload);
    if (text.empty()) {
        return false;
    }

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '-' && ch != '+' && ch != '.') {
            continue;
        }

        const char* start = text.c_str() + i;
        char* end = nullptr;
        const double value = std::strtod(start, &end);
        if (end == start || !std::isfinite(value)) {
            continue;
        }

        const std::string unit = lowercase(std::string(end));
        torr = unit.find("mtorr") != std::string::npos ? value / 1000.0 : value;
        return true;
    }

    return false;
}

std::string byteArrayToString(const SimpleBLE::ByteArray& payload) {
    return std::string(payload);
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

bool isPressurePeripheral(SimpleBLE::Peripheral& peripheral) {
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

std::optional<SimpleBLE::Peripheral> findPressurePeripheral(SimpleBLE::Adapter& adapter, const std::atomic_bool& stopRequested) {
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

    std::cout << "SimpleBLE scan complete; seen=" << peripherals.size() << "\n";

    for (SimpleBLE::Peripheral& peripheral : peripherals) {
        const std::string identifier = peripheral.identifier().empty() ? "<no name>" : peripheral.identifier();
        std::cout << "  " << identifier << " [" << peripheral.address() << "] RSSI=" << peripheral.rssi()
                  << " connectable=" << (peripheral.is_connectable() ? "yes" : "no") << "\n";

        if (stopRequested.load()) {
            return {};
        }

        if (isPressurePeripheral(peripheral)) {
            std::cout << "SimpleBLE pressure candidate: " << identifier << " [" << peripheral.address() << "]\n";
            return peripheral;
        }
    }

    return {};
}

bool findPressureCharacteristic(
    SimpleBLE::Peripheral& peripheral,
    SimpleBLE::BluetoothUUID& serviceUuid,
    SimpleBLE::BluetoothUUID& characteristicUuid
) {
    for (SimpleBLE::Service service : peripheral.services()) {
        std::cout << "SimpleBLE service: " << service.uuid() << "\n";
        if (lowercase(service.uuid()) != kServiceUuid) {
            continue;
        }

        for (SimpleBLE::Characteristic characteristic : service.characteristics()) {
            std::cout << "  characteristic: " << characteristic.uuid()
                      << " notify=" << (characteristic.can_notify() ? "yes" : "no")
                      << " read=" << (characteristic.can_read() ? "yes" : "no") << "\n";
            if (lowercase(characteristic.uuid()) == kDataUuid) {
                serviceUuid = service.uuid();
                characteristicUuid = characteristic.uuid();
                return true;
            }
        }
    }

    return false;
}

} // namespace

PressureMonitor::~PressureMonitor() {
    stop();
}

void PressureMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }

    stopRequested_ = false;
    worker_ = std::thread(&PressureMonitor::run, this);
}

void PressureMonitor::requestStop() {
    stopRequested_ = true;
}

void PressureMonitor::stop() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
}

bool PressureMonitor::running() const {
    return running_.load();
}

PressureSnapshot PressureMonitor::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PressureSnapshot copy = snapshot_;
    if (copy.connected && copy.hasReading) {
        const auto age = std::chrono::steady_clock::now() - copy.lastReading;
        if (age > std::chrono::seconds(4)) {
            copy.live = false;
            copy.status = "Waiting";
        }
    }
    return copy;
}

void PressureMonitor::handlePayload(const std::string& payload) {
    const std::string text = trim(payload);
    double torr = 0.0;

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.connected = true;
    snapshot_.detail = text;

    if (parsePressureTorr(text, torr)) {
        snapshot_.hasReading = true;
        snapshot_.live = true;
        snapshot_.torr = torr;
        snapshot_.status = "Live";
        snapshot_.lastReading = std::chrono::steady_clock::now();
        return;
    }

    snapshot_.live = false;
    if (text.empty() || text == "boot") {
        snapshot_.status = "Waiting";
    } else if (text.rfind("ERR", 0) == 0) {
        snapshot_.status = "Gauge error";
    } else {
        snapshot_.status = "Waiting";
    }
}

void PressureMonitor::setStatus(std::string status, std::string detail, bool connected, bool live, int rssi) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.status = std::move(status);
    snapshot_.detail = std::move(detail);
    snapshot_.connected = connected;
    snapshot_.live = live;
    snapshot_.rssi = rssi;
}

void PressureMonitor::sleepUntilStopped(std::chrono::milliseconds duration) const {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stopRequested_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void PressureMonitor::run() {
    SimpleBLE::Config::WinRT::experimental_use_own_mta_apartment = true;
    SimpleBLE::Config::WinRT::experimental_reinitialize_winrt_apartment_on_main_thread = false;

    while (!stopRequested_) {
        try {
            setStatus("Scanning", std::string(kDeviceName) + " " + kServiceUuid, false, false, 0);

            std::optional<SimpleBLE::Adapter> adapter = firstAdapter();
            if (!adapter.has_value()) {
                setStatus("Offline", "No Bluetooth adapter", false, false, 0);
                sleepUntilStopped(std::chrono::seconds(3));
                continue;
            }

            std::optional<SimpleBLE::Peripheral> found = findPressurePeripheral(*adapter, stopRequested_);
            if (stopRequested_) {
                break;
            }

            if (!found.has_value()) {
                setStatus("Offline", "BLE service not found", false, false, 0);
                sleepUntilStopped(std::chrono::seconds(2));
                continue;
            }

            SimpleBLE::Peripheral peripheral = *found;
            setStatus("Connecting", peripheral.identifier(), false, false, peripheral.rssi());
            std::cout << "SimpleBLE connecting to " << peripheral.identifier() << " [" << peripheral.address() << "]\n";

            peripheral.set_callback_on_disconnected([this]() {
                std::cout << "SimpleBLE disconnected\n";
                setStatus("Offline", "Disconnected", false, false, 0);
            });

            peripheral.connect();
            setStatus("Waiting", "Discovering services", true, false, peripheral.rssi());
            std::cout << "SimpleBLE connected\n";

            SimpleBLE::BluetoothUUID serviceUuid;
            SimpleBLE::BluetoothUUID characteristicUuid;
            if (!findPressureCharacteristic(peripheral, serviceUuid, characteristicUuid)) {
                setStatus("Offline", "Pressure characteristic unavailable", false, false, peripheral.rssi());
                peripheral.disconnect();
                sleepUntilStopped(std::chrono::seconds(2));
                continue;
            }

            bool subscribed = false;
            peripheral.notify(serviceUuid, characteristicUuid, [this](SimpleBLE::ByteArray payload) {
                const std::string text = byteArrayToString(payload);
                std::cout << "SimpleBLE notification: " << text << "\n";
                handlePayload(text);
            });
            subscribed = true;

            setStatus("Waiting", "Waiting for pressure", true, false, peripheral.rssi());
            try {
                const std::string initial = byteArrayToString(peripheral.read(serviceUuid, characteristicUuid));
                std::cout << "SimpleBLE initial read: " << initial << "\n";
                handlePayload(initial);
            } catch (const std::exception& error) {
                std::cout << "SimpleBLE initial read failed: " << error.what() << "\n";
            }

            while (!stopRequested_ && peripheral.is_connected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }

            if (subscribed && peripheral.is_connected()) {
                peripheral.unsubscribe(serviceUuid, characteristicUuid);
            }
            if (peripheral.is_connected()) {
                peripheral.disconnect();
            }
        } catch (const std::exception& error) {
            setStatus("Offline", error.what(), false, false, 0);
            std::cout << "SimpleBLE pressure monitor error: " << error.what() << "\n";
            sleepUntilStopped(std::chrono::seconds(2));
        }
    }

    running_ = false;
}

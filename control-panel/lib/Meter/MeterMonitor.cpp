#include "Meter/MeterMonitor.h"

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

constexpr const char* kDeviceName = "Fusor-Meter";
constexpr const char* kServiceUuid = "9b3f1001-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* kDataUuid = "9b3f1002-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr int kScanDurationMs = 5000;
constexpr int kScanPollMs = 50;

struct ParsedMeterPayload {
    bool hasVoltage = false;
    bool hasCurrent = false;
    bool hasSaturation = false;
    bool saturatedVoltage = false;
    bool saturatedCurrent = false;
    double kilovolts = 0.0;
    double milliamps = 0.0;
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

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool parseDouble(const std::string& text, double& value) {
    const std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || !std::isfinite(parsed)) {
        return false;
    }

    value = parsed;
    return true;
}

bool parseMeterPayload(const std::string& payload, ParsedMeterPayload& parsed) {
    const std::string text = trim(payload);
    std::size_t start = 0;

    while (start <= text.size()) {
        const std::size_t end = text.find(',', start);
        const std::string token = trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        const std::size_t equals = token.find('=');

        if (equals != std::string::npos) {
            const std::string key = lowercase(trim(token.substr(0, equals)));
            const std::string valueText = trim(token.substr(equals + 1));
            double value = 0.0;

            if (key == "kv" && parseDouble(valueText, value)) {
                parsed.hasVoltage = true;
                parsed.kilovolts = value;
            } else if (key == "ma" && parseDouble(valueText, value)) {
                parsed.hasCurrent = true;
                parsed.milliamps = value;
            } else if (key == "sat" && !valueText.empty()) {
                parsed.hasSaturation = true;
                parsed.saturatedVoltage = valueText[0] == '1';
                parsed.saturatedCurrent = valueText.size() > 1 ? valueText[1] == '1' : parsed.saturatedVoltage;
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return parsed.hasVoltage || parsed.hasCurrent;
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

bool isMeterPeripheral(SimpleBLE::Peripheral& peripheral) {
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

std::optional<SimpleBLE::Peripheral> findMeterPeripheral(SimpleBLE::Adapter& adapter, const std::atomic_bool& stopRequested) {
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

    std::cout << "SimpleBLE meter scan complete; seen=" << peripherals.size() << "\n";

    for (SimpleBLE::Peripheral& peripheral : peripherals) {
        const std::string identifier = peripheral.identifier().empty() ? "<no name>" : peripheral.identifier();
        std::cout << "  " << identifier << " [" << peripheral.address() << "] RSSI=" << peripheral.rssi()
                  << " connectable=" << (peripheral.is_connectable() ? "yes" : "no") << "\n";

        if (stopRequested.load()) {
            return {};
        }

        if (isMeterPeripheral(peripheral)) {
            std::cout << "SimpleBLE meter candidate: " << identifier << " [" << peripheral.address() << "]\n";
            return peripheral;
        }
    }

    return {};
}

bool findMeterCharacteristic(
    SimpleBLE::Peripheral& peripheral,
    SimpleBLE::BluetoothUUID& serviceUuid,
    SimpleBLE::BluetoothUUID& characteristicUuid
) {
    for (SimpleBLE::Service service : peripheral.services()) {
        std::cout << "SimpleBLE meter service: " << service.uuid() << "\n";
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

MeterMonitor::~MeterMonitor() {
    stop();
}

void MeterMonitor::start() {
    if (running_.exchange(true)) {
        return;
    }

    stopRequested_ = false;
    worker_ = std::thread(&MeterMonitor::run, this);
}

void MeterMonitor::requestStop() {
    stopRequested_ = true;
}

void MeterMonitor::stop() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
    running_ = false;
}

bool MeterMonitor::running() const {
    return running_.load();
}

MeterSnapshot MeterMonitor::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MeterSnapshot copy = snapshot_;
    if (copy.connected && (copy.hasVoltage || copy.hasCurrent)) {
        const auto age = std::chrono::steady_clock::now() - copy.lastReading;
        if (age > std::chrono::seconds(4)) {
            copy.live = false;
            copy.status = "Waiting";
        }
    }
    return copy;
}

void MeterMonitor::handlePayload(const std::string& payload) {
    const std::string text = trim(payload);
    ParsedMeterPayload parsed{};

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.connected = true;
    snapshot_.detail = text;

    if (parseMeterPayload(text, parsed)) {
        if (parsed.hasVoltage) {
            snapshot_.hasVoltage = true;
            snapshot_.kilovolts = parsed.kilovolts;
        }
        if (parsed.hasCurrent) {
            snapshot_.hasCurrent = true;
            snapshot_.milliamps = parsed.milliamps;
        }
        if (parsed.hasSaturation) {
            snapshot_.saturatedVoltage = parsed.saturatedVoltage;
            snapshot_.saturatedCurrent = parsed.saturatedCurrent;
        }

        snapshot_.live = true;
        snapshot_.status = (snapshot_.saturatedVoltage || snapshot_.saturatedCurrent) ? "Saturated" : "Live";
        snapshot_.lastReading = std::chrono::steady_clock::now();
        return;
    }

    snapshot_.live = false;
    if (text.empty() || text == "boot") {
        snapshot_.status = "Waiting";
    } else if (text.rfind("ERR", 0) == 0) {
        snapshot_.status = "Meter error";
    } else {
        snapshot_.status = "Waiting";
    }
}

void MeterMonitor::setStatus(std::string status, std::string detail, bool connected, bool live, int rssi) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.status = std::move(status);
    snapshot_.detail = std::move(detail);
    snapshot_.connected = connected;
    snapshot_.live = live;
    snapshot_.rssi = rssi;
}

void MeterMonitor::sleepUntilStopped(std::chrono::milliseconds duration) const {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stopRequested_ && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void MeterMonitor::run() {
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

            std::optional<SimpleBLE::Peripheral> found = findMeterPeripheral(*adapter, stopRequested_);
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
            std::cout << "SimpleBLE connecting to meter " << peripheral.identifier() << " [" << peripheral.address() << "]\n";

            peripheral.set_callback_on_disconnected([this]() {
                std::cout << "SimpleBLE meter disconnected\n";
                setStatus("Offline", "Disconnected", false, false, 0);
            });

            peripheral.connect();
            setStatus("Waiting", "Discovering services", true, false, peripheral.rssi());
            std::cout << "SimpleBLE meter connected\n";

            SimpleBLE::BluetoothUUID serviceUuid;
            SimpleBLE::BluetoothUUID characteristicUuid;
            if (!findMeterCharacteristic(peripheral, serviceUuid, characteristicUuid)) {
                setStatus("Offline", "Meter characteristic unavailable", false, false, peripheral.rssi());
                peripheral.disconnect();
                sleepUntilStopped(std::chrono::seconds(2));
                continue;
            }

            bool subscribed = false;
            peripheral.notify(serviceUuid, characteristicUuid, [this](SimpleBLE::ByteArray payload) {
                const std::string text = byteArrayToString(payload);
                std::cout << "SimpleBLE meter notification: " << text << "\n";
                handlePayload(text);
            });
            subscribed = true;

            setStatus("Waiting", "Waiting for meter", true, false, peripheral.rssi());
            try {
                const std::string initial = byteArrayToString(peripheral.read(serviceUuid, characteristicUuid));
                std::cout << "SimpleBLE meter initial read: " << initial << "\n";
                handlePayload(initial);
            } catch (const std::exception& error) {
                std::cout << "SimpleBLE meter initial read failed: " << error.what() << "\n";
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
            std::cout << "SimpleBLE meter monitor error: " << error.what() << "\n";
            sleepUntilStopped(std::chrono::seconds(2));
        }
    }

    running_ = false;
}

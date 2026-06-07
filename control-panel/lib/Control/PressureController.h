#pragma once

#include "Meter/MeterMonitor.h"
#include "Pressure/PressureMonitor.h"

#include <chrono>
#include <deque>
#include <string>

class ValveMonitor;

enum class ControlMode {
    Idle,
    Evacuate,
    Fault,
};

const char* controlModeName(ControlMode mode);

struct ControllerConfig {
    double controlPeriodSec = 0.35;
    double pressureFilterAlpha = 0.35;
    double pressureStaleSec = 2.0;

    double voltagePresentThresholdKV = 0.05;
    double currentPresentThresholdMA = 1.2;
    double plasmaTripPressureMTorr = 140.0;
    double faultCloseRepeatSec = 0.5;

    double baseTargetMTorr = 15.0;
    double baseStableSlopeMTorrPerSec = 0.1;
    double baseStableTimeSec = 10.0;
    long evacuationValveSteps = 0;

    double rollingSlopeWindowSec = 4.0;

    double moveDwellBaseSec = 1.2;
    double moveDwellPerStepSec = 0.02;
    double moveDwellMaxSec = 4.0;

    long openLimitSteps = 25600;
};

struct ControllerSnapshot {
    ControlMode mode = ControlMode::Idle;
    bool hasPressure = false;
    double filteredPressureMTorr = 0.0;
    double predictedPressureMTorr = 0.0;
    double pressureSlopeMTorrPerSec = 0.0;
    long virtualPositionSteps = 0;
    long lastCommandedDelta = 0;
    bool powerPresent = false;
    bool evacuationComplete = false;
    bool valveLinked = false;
    std::string commandReason;
    std::string faultReason;
    std::string statusDetail;
};

class PressureController {
public:
    explicit PressureController(ControllerConfig config = {});

    void tick(const PressureSnapshot& pressure, const MeterSnapshot& meter);
    void requestMode(ControlMode mode);
    void clearFault();
    void setValveMonitor(ValveMonitor* valve) { valve_ = valve; }

    ControllerSnapshot snapshot() const { return snapshot_; }
    const ControllerConfig& config() const { return config_; }

private:
    struct PressureSample {
        double timeSec = 0.0;
        double pressureMTorr = 0.0;
    };

    void enterFault(std::string reason);
    void updatePressureEstimates(const PressureSnapshot& pressure, double dtSec);
    bool runSafetyChecks(const PressureSnapshot& pressure);
    void runMode();
    void appendPressureSample(double timeSec, double pressureMTorr);
    double rollingPressureSlope(double windowSec) const;
    void commandDelta(long delta);
    void commandFaultCloseIfNeeded();
    void commandClose();
    long clampPosition(long target) const;
    bool meterPowerPresent(const MeterSnapshot& meter) const;

    ControllerConfig config_;
    ControllerSnapshot snapshot_;
    ValveMonitor* valve_ = nullptr;

    bool hasPrevFiltered_ = false;
    std::chrono::steady_clock::time_point lastTickTime_{};
    bool hasTicked_ = false;
    std::chrono::steady_clock::time_point stableSince_{};
    bool stableRunning_ = false;
    std::chrono::steady_clock::time_point controllerStartedAt_{};
    std::chrono::steady_clock::time_point nextMoveAllowedAt_{};
    std::chrono::steady_clock::time_point lastFaultCloseCommandAt_{};
    bool hasControllerStartTime_ = false;
    long virtualPositionSteps_ = 0;

    std::deque<PressureSample> pressureHistory_;
};

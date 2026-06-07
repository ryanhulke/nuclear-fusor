#include "Control/PressureController.h"

#include "Valve/ValveMonitor.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {

constexpr double kMTorrPerTorr = 1000.0;

double torrToMTorr(double torr) {
    return torr * kMTorrPerTorr;
}

bool pressureFresh(const PressureSnapshot& pressure, double staleSec) {
    if (!pressure.connected || !pressure.hasReading) {
        return false;
    }
    const auto age = std::chrono::steady_clock::now() - pressure.lastReading;
    return age <= std::chrono::duration<double>(staleSec);
}

} // namespace

const char* controlModeName(ControlMode mode) {
    switch (mode) {
    case ControlMode::Idle: return "IDLE";
    case ControlMode::Evacuate: return "EVACUATE";
    case ControlMode::Fault: return "FAULT";
    }
    return "IDLE";
}

PressureController::PressureController(ControllerConfig config) : config_(config) {
    snapshot_.mode = ControlMode::Idle;
    snapshot_.virtualPositionSteps = 0;
}

void PressureController::requestMode(ControlMode mode) {
    if (snapshot_.mode == ControlMode::Fault) {
        return;
    }

    if (snapshot_.mode != mode) {
        stableRunning_ = false;
        snapshot_.evacuationComplete = false;
        snapshot_.statusDetail.clear();
        nextMoveAllowedAt_ = {};
    }
    snapshot_.mode = mode;
}

void PressureController::clearFault() {
    if (snapshot_.mode != ControlMode::Fault) {
        return;
    }
    snapshot_.mode = ControlMode::Idle;
    snapshot_.faultReason.clear();
    snapshot_.statusDetail = "Fault cleared";
    snapshot_.evacuationComplete = false;
    stableRunning_ = false;
    lastFaultCloseCommandAt_ = {};
}

void PressureController::tick(const PressureSnapshot& pressure, const MeterSnapshot& meter) {
    const auto now = std::chrono::steady_clock::now();
    if (!hasControllerStartTime_) {
        controllerStartedAt_ = now;
        hasControllerStartTime_ = true;
    }

    double dtSec = config_.controlPeriodSec;
    if (hasTicked_) {
        dtSec = std::chrono::duration<double>(now - lastTickTime_).count();
        if (dtSec <= 0.0) {
            dtSec = config_.controlPeriodSec;
        }
    }
    lastTickTime_ = now;
    hasTicked_ = true;

    if (valve_) {
        const ValveSnapshot valveSnap = valve_->snapshot();
        snapshot_.valveLinked = valveSnap.live && valveSnap.hasPosition;
        if (snapshot_.valveLinked) {
            virtualPositionSteps_ = valveSnap.positionSteps;
            snapshot_.virtualPositionSteps = virtualPositionSteps_;
            if (valveSnap.openLimitSteps > 0) {
                config_.openLimitSteps = valveSnap.openLimitSteps;
            }
        }
    } else {
        snapshot_.valveLinked = false;
    }

    snapshot_.powerPresent = meterPowerPresent(meter);

    updatePressureEstimates(pressure, dtSec);

    if (runSafetyChecks(pressure)) {
        return;
    }

    runMode();
}

bool PressureController::meterPowerPresent(const MeterSnapshot& meter) const {
    if (!meter.connected || !meter.live) {
        return false;
    }

    const bool voltagePresent = meter.hasVoltage
                                && std::abs(meter.kilovolts) > config_.voltagePresentThresholdKV;
    const bool currentPresent = meter.hasCurrent
                                && std::abs(meter.milliamps) > config_.currentPresentThresholdMA;
    return voltagePresent || currentPresent;
}

void PressureController::updatePressureEstimates(const PressureSnapshot& pressure, double dtSec) {
    snapshot_.hasPressure = pressureFresh(pressure, config_.pressureStaleSec);
    if (!snapshot_.hasPressure) {
        return;
    }

    const double rawMTorr = torrToMTorr(pressure.torr);
    if (!hasPrevFiltered_) {
        snapshot_.filteredPressureMTorr = rawMTorr;
        snapshot_.pressureSlopeMTorrPerSec = 0.0;
        hasPrevFiltered_ = true;
    } else {
        const double alpha = std::clamp(config_.pressureFilterAlpha, 0.0, 1.0);
        const double previous = snapshot_.filteredPressureMTorr;
        snapshot_.filteredPressureMTorr = (1.0 - alpha) * previous + alpha * rawMTorr;
        snapshot_.pressureSlopeMTorrPerSec = dtSec > 0.0
                                                 ? (snapshot_.filteredPressureMTorr - previous) / dtSec
                                                 : 0.0;
    }

    const double elapsedSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - controllerStartedAt_).count();
    appendPressureSample(elapsedSec, snapshot_.filteredPressureMTorr);

    const double rollingSlope = rollingPressureSlope(config_.rollingSlopeWindowSec);
    if (std::isfinite(rollingSlope)) {
        snapshot_.pressureSlopeMTorrPerSec = rollingSlope;
    }

    snapshot_.predictedPressureMTorr =
        snapshot_.filteredPressureMTorr + config_.rollingSlopeWindowSec * snapshot_.pressureSlopeMTorrPerSec;
}

bool PressureController::runSafetyChecks(const PressureSnapshot& pressure) {
    if (snapshot_.mode == ControlMode::Fault) {
        commandFaultCloseIfNeeded();
        return true;
    }

    const bool activeMode = snapshot_.mode == ControlMode::Evacuate;

    if (pressure.connected && !pressureFresh(pressure, config_.pressureStaleSec) && activeMode) {
        commandClose();
        enterFault("Pressure sensor stale");
        return true;
    }

    if (!snapshot_.hasPressure) {
        return false;
    }

    const double p = snapshot_.filteredPressureMTorr;
    if (snapshot_.powerPresent && p > config_.plasmaTripPressureMTorr) {
        commandClose();
        enterFault("Power pressure trip");
        return true;
    }

    if (snapshot_.powerPresent && snapshot_.predictedPressureMTorr > config_.plasmaTripPressureMTorr) {
        commandClose();
        enterFault("Predicted pressure trip");
        return true;
    }

    return false;
}

void PressureController::runMode() {
    snapshot_.lastCommandedDelta = 0;
    snapshot_.commandReason.clear();

    switch (snapshot_.mode) {
    case ControlMode::Idle:
    case ControlMode::Fault:
        return;

    case ControlMode::Evacuate: {
        const long evacuateTarget = clampPosition(config_.evacuationValveSteps);
        if (virtualPositionSteps_ != evacuateTarget) {
            snapshot_.commandReason = "evacuate_preset";
            commandDelta(evacuateTarget - virtualPositionSteps_);
        }
        if (!snapshot_.hasPressure) {
            return;
        }
        const bool atBase = snapshot_.filteredPressureMTorr <= config_.baseTargetMTorr;
        const bool slow = std::abs(snapshot_.pressureSlopeMTorrPerSec) <= config_.baseStableSlopeMTorrPerSec;
        if (atBase && slow) {
            if (!stableRunning_) {
                stableRunning_ = true;
                stableSince_ = std::chrono::steady_clock::now();
            } else {
                const double held = std::chrono::duration<double>(std::chrono::steady_clock::now() - stableSince_).count();
                if (held >= config_.baseStableTimeSec) {
                    snapshot_.evacuationComplete = true;
                    snapshot_.statusDetail = "Base pressure stable";
                }
            }
        } else {
            stableRunning_ = false;
            snapshot_.evacuationComplete = false;
        }
        return;
    }
    }
}

void PressureController::appendPressureSample(double timeSec, double pressureMTorr) {
    pressureHistory_.push_back({timeSec, pressureMTorr});
    const double keepWindowSec = config_.rollingSlopeWindowSec + 5.0;
    while (!pressureHistory_.empty()
           && timeSec - pressureHistory_.front().timeSec > keepWindowSec) {
        pressureHistory_.pop_front();
    }
}

double PressureController::rollingPressureSlope(double windowSec) const {
    if (pressureHistory_.size() < 3) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double latest = pressureHistory_.back().timeSec;
    const double start = latest - std::max(0.5, windowSec);
    int count = 0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;

    for (const PressureSample& sample : pressureHistory_) {
        if (sample.timeSec < start) {
            continue;
        }
        const double x = sample.timeSec - latest;
        const double y = sample.pressureMTorr;
        ++count;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }

    const double denom = static_cast<double>(count) * sumXX - sumX * sumX;
    if (count < 3 || std::abs(denom) < 1.0e-9) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (static_cast<double>(count) * sumXY - sumX * sumY) / denom;
}

void PressureController::commandDelta(long delta) {
    if (delta == 0) {
        snapshot_.lastCommandedDelta = 0;
        return;
    }

    const long startPos = virtualPositionSteps_;
    const long target = clampPosition(startPos + delta);
    const long applied = target - startPos;
    virtualPositionSteps_ = target;
    snapshot_.virtualPositionSteps = virtualPositionSteps_;
    snapshot_.lastCommandedDelta = applied;
    if (applied == 0) {
        std::cout << "[ctrl] move skipped (clamped): requested delta=" << delta
                  << " pos=" << startPos << " openLimit=" << config_.openLimitSteps << "\n";
        return;
    }

    const char* direction = applied > 0 ? "OPEN" : "CLOSE";
    std::cout << "[ctrl] " << controlModeName(snapshot_.mode)
              << " sendMoveTo target=" << target
              << " (" << direction << " applied=" << applied
              << " from pos=" << startPos << ")\n";

    const double dwellSec = std::min(
        config_.moveDwellMaxSec,
        config_.moveDwellBaseSec + config_.moveDwellPerStepSec * static_cast<double>(std::abs(applied))
    );
    nextMoveAllowedAt_ = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(dwellSec)
    );
    if (valve_ && snapshot_.valveLinked) {
        valve_->sendMoveTo(target);
    }
}

void PressureController::commandFaultCloseIfNeeded() {
    if (snapshot_.valveLinked && virtualPositionSteps_ <= 0) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastFaultCloseCommandAt_ != std::chrono::steady_clock::time_point{}) {
        const double elapsedSec = std::chrono::duration<double>(now - lastFaultCloseCommandAt_).count();
        if (elapsedSec < config_.faultCloseRepeatSec) {
            return;
        }
    }

    commandClose();
}

void PressureController::commandClose() {
    const long startPos = virtualPositionSteps_;
    virtualPositionSteps_ = 0;
    snapshot_.virtualPositionSteps = virtualPositionSteps_;
    snapshot_.lastCommandedDelta = -startPos;
    snapshot_.commandReason = "safety_close";
    lastFaultCloseCommandAt_ = std::chrono::steady_clock::now();

    if (valve_) {
        const bool sent = valve_->sendClose();
        if (sent) {
            std::cout << "[ctrl] " << controlModeName(snapshot_.mode)
                      << " sendClose from pos=" << startPos;
            if (!snapshot_.valveLinked) {
                std::cout << " (position not linked)";
            }
            std::cout << "\n";
        }
    }
}

long PressureController::clampPosition(long target) const {
    if (target < 0) {
        return 0;
    }
    if (config_.openLimitSteps > 0 && target > config_.openLimitSteps) {
        return config_.openLimitSteps;
    }
    return target;
}

void PressureController::enterFault(std::string reason) {
    snapshot_.mode = ControlMode::Fault;
    snapshot_.faultReason = std::move(reason);
    snapshot_.evacuationComplete = false;
    stableRunning_ = false;
}

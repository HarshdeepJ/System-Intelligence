#include "battery_detector.hpp"

#include "anomaly_detector.hpp"

namespace sysintel {

namespace {

constexpr const char* kMetric = "battery.discharge_watts";
constexpr const char* kDomain = "battery";

// battery.discharge_watts is only emitted while actually discharging (see
// sampler.cpp), so "no recent samples" specifically means "on AC right
// now" for this one domain -- everywhere else it's a more generic
// kNoRecentSamples. Keeping BatteryCheckResult's own enum/string names
// (kNoRecentDischarge etc.) intact here means neither the CLI's existing
// `check-battery --json` shape nor Python's schemas.py needs to change.
BatteryCheckResult map_result(AnomalyCheckResult r) {
    switch (r) {
        case AnomalyCheckResult::kNotEnoughHistory:
            return BatteryCheckResult::kNotEnoughHistory;
        case AnomalyCheckResult::kNoRecentSamples:
            return BatteryCheckResult::kNoRecentDischarge;
        case AnomalyCheckResult::kNormal:
            return BatteryCheckResult::kNormal;
        case AnomalyCheckResult::kAnomalyOpened:
            return BatteryCheckResult::kAnomalyOpened;
        case AnomalyCheckResult::kAnomalyOngoing:
            return BatteryCheckResult::kAnomalyOngoing;
        case AnomalyCheckResult::kResolved:
            return BatteryCheckResult::kResolved;
    }
    return BatteryCheckResult::kNotEnoughHistory;
}

}  // namespace

BatteryAnomalyDetector::BatteryAnomalyDetector(SqliteStore& store, BatteryAnomalyConfig config)
    : store_(store), config_(config) {}

BatteryCheckReport BatteryAnomalyDetector::check() {
    AnomalyDetectorConfig generic_config;
    generic_config.metric = kMetric;
    generic_config.domain = kDomain;
    generic_config.min_history_days = config_.min_history_days;
    generic_config.min_baseline_samples = config_.min_baseline_samples;
    generic_config.live_window_minutes = config_.live_window_minutes;
    generic_config.min_live_samples = config_.min_live_samples;
    // stddev_factor/relative_factor keep AnomalyDetectorConfig's defaults
    // (2.5, 1.5), matching this detector's original hardcoded values exactly.

    AnomalyDetector detector(store_, generic_config);
    AnomalyCheckReport generic_report = detector.check();

    BatteryCheckReport report;
    report.result = map_result(generic_report.result);
    report.live_mean = generic_report.live_mean;
    report.baseline_mean = generic_report.baseline_mean;
    report.baseline_stddev = generic_report.baseline_stddev;
    report.threshold = generic_report.threshold;
    report.incident = generic_report.incident;
    return report;
}

}  // namespace sysintel

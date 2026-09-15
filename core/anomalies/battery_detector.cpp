#include "battery_detector.hpp"

#include <algorithm>

namespace sysintel {

namespace {

constexpr const char* kMetric = "battery.discharge_watts";
constexpr const char* kDomain = "battery";

}  // namespace

BatteryAnomalyDetector::BatteryAnomalyDetector(SqliteStore& store, BatteryAnomalyConfig config)
    : store_(store), config_(config) {}

BatteryCheckReport BatteryAnomalyDetector::check() {
    BatteryCheckReport report;

    int64_t now_ms = current_timestamp_ms();
    int64_t history_cutoff_ms =
        now_ms - static_cast<int64_t>(config_.min_history_days) * 24LL * 60 * 60 * 1000;

    // Gate 1: do we have samples going back far enough at all?
    int64_t earliest_ms = store_.query_earliest_timestamp(kMetric);
    if (earliest_ms < 0 || earliest_ms > history_cutoff_ms) {
        report.result = BatteryCheckResult::kNotEnoughHistory;
        return report;
    }

    // The baseline is computed over the same trailing window that the
    // history gate requires -- "enough history to evaluate" and "the window
    // the baseline is drawn from" are deliberately the same period.
    Stats baseline = store_.query_stats(kMetric, history_cutoff_ms, now_ms);

    // Gate 2: is that window actually dense enough to trust (not just old)?
    if (baseline.count < config_.min_baseline_samples) {
        report.result = BatteryCheckResult::kNotEnoughHistory;
        return report;
    }

    report.baseline_mean = baseline.mean;
    report.baseline_stddev = baseline.stddev;

    int64_t live_cutoff_ms = now_ms - static_cast<int64_t>(config_.live_window_minutes) * 60 * 1000;
    Stats live = store_.query_stats(kMetric, live_cutoff_ms, now_ms);

    auto open_incident = store_.get_open_incident(kDomain);

    if (live.count < config_.min_live_samples) {
        // No recent discharge samples at all -- most likely just on AC right
        // now. Nothing to evaluate; an already-open incident is left as-is
        // rather than guessed at with no fresh data.
        report.result = BatteryCheckResult::kNoRecentDischarge;
        return report;
    }

    report.live_mean = live.mean;
    report.threshold = std::max(baseline.mean + 2.5 * baseline.stddev, baseline.mean * 1.5);

    bool is_anomalous = live.mean > report.threshold;

    if (is_anomalous) {
        if (open_incident.has_value()) {
            report.result = BatteryCheckResult::kAnomalyOngoing;
            report.incident = open_incident;
        } else {
            Incident incident =
                store_.open_incident(kDomain, "high", "automatic", live.mean, baseline.mean);
            report.result = BatteryCheckResult::kAnomalyOpened;
            report.incident = incident;
        }
        return report;
    }

    if (open_incident.has_value()) {
        // Hysteresis: only resolve once comfortably back under the baseline
        // (not just barely under the trigger threshold), so a reading right
        // at the boundary doesn't flap open/resolved every check.
        if (live.mean < baseline.mean + baseline.stddev) {
            store_.resolve_incident(open_incident->id, now_ms);
            open_incident->status = "resolved";
            open_incident->resolved_at_ms = now_ms;
            report.result = BatteryCheckResult::kResolved;
            report.incident = open_incident;
        } else {
            report.result = BatteryCheckResult::kAnomalyOngoing;
            report.incident = open_incident;
        }
        return report;
    }

    report.result = BatteryCheckResult::kNormal;
    return report;
}

}  // namespace sysintel

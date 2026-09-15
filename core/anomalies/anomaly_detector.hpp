#pragma once
#include <optional>
#include <string>

#include "../storage/sqlite_store.hpp"

namespace sysintel {

struct AnomalyDetectorConfig {
    std::string metric;  // e.g. "memory.load_percent"
    std::string domain;  // e.g. "memory" -- matches the incidents table's domain column
    int min_history_days = 14;
    int min_baseline_samples = 200;
    int live_window_minutes = 5;
    int min_live_samples = 3;
    double stddev_factor = 2.5;
    double relative_factor = 1.5;
};

enum class AnomalyCheckResult {
    kNotEnoughHistory,
    kNoRecentSamples,
    kNormal,
    kAnomalyOpened,
    kAnomalyOngoing,
    kResolved,
};

struct AnomalyCheckReport {
    AnomalyCheckResult result = AnomalyCheckResult::kNotEnoughHistory;
    double live_mean = 0.0;
    double baseline_mean = 0.0;
    double baseline_stddev = 0.0;
    double threshold = 0.0;
    std::optional<Incident> incident;
};

// A metric-agnostic version of the original battery-drain detector's logic
// (see tech design's Fast Brain section): a trailing baseline mean/stddev
// compared against a short "right now" window, gated on enough accumulated
// history, with hysteresis on resolve. Proven first on
// battery.discharge_watts (Phase 3) and only generalized here once a second
// real domain existed to validate the abstraction against -- not built
// generic from the start.
class AnomalyDetector {
public:
    AnomalyDetector(SqliteStore& store, AnomalyDetectorConfig config);

    AnomalyCheckReport check();

private:
    SqliteStore& store_;
    AnomalyDetectorConfig config_;
};

}  // namespace sysintel

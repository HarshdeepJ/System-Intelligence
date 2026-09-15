#pragma once
#include <optional>

#include "../storage/sqlite_store.hpp"

namespace sysintel {

struct BatteryAnomalyConfig {
    // Don't evaluate at all until history goes back at least this far --
    // the baseline itself is also computed over this same trailing window.
    int min_history_days = 14;

    // ...and until there are at least this many actual discharge samples
    // within that window (guards against a technically-14-day-old but very
    // sparse history, e.g. a laptop that's almost always plugged in).
    int min_baseline_samples = 200;

    // The "right now" window compared against the baseline.
    int live_window_minutes = 5;
    int min_live_samples = 3;
};

enum class BatteryCheckResult {
    kNotEnoughHistory,  // baseline can't be trusted yet
    kNoRecentDischarge, // e.g. currently on AC -- nothing to evaluate right now
    kNormal,
    kAnomalyOpened,     // a new incident was just created
    kAnomalyOngoing,    // still abnormal, incident was already open
    kResolved,          // was abnormal, just returned to normal
};

struct BatteryCheckReport {
    BatteryCheckResult result = BatteryCheckResult::kNotEnoughHistory;
    double live_mean = 0.0;
    double baseline_mean = 0.0;
    double baseline_stddev = 0.0;
    double threshold = 0.0;
    std::optional<Incident> incident;
};

// Battery-drain anomaly detection: no LLM, just a rolling baseline compared
// against a short "right now" window. See tech design's Fast Brain section.
class BatteryAnomalyDetector {
public:
    explicit BatteryAnomalyDetector(SqliteStore& store, BatteryAnomalyConfig config = {});

    // Call this periodically (e.g. once a minute). Cheap: a couple of
    // aggregate SQL queries, no full table scan.
    BatteryCheckReport check();

private:
    SqliteStore& store_;
    BatteryAnomalyConfig config_;
};

}  // namespace sysintel

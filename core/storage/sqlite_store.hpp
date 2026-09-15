#pragma once
#include <optional>
#include <string>
#include <vector>

#include "metric_sample.hpp"

struct sqlite3;  // forward declaration -- keeps sqlite3.h out of every includer

namespace sysintel {

struct AggregatedValue {
    double min_value = 0.0;
    double max_value = 0.0;
    double avg_value = 0.0;
    int sample_count = 0;
};

// Mean/stddev over a window -- what the anomaly detector compares "right
// now" against "the learned baseline" with.
struct Stats {
    double mean = 0.0;
    double stddev = 0.0;
    int count = 0;
};

struct Incident {
    std::string id;
    std::string domain;         // e.g. "battery"
    std::string status;         // "open" or "resolved"
    std::string severity;       // e.g. "high"
    int64_t started_at_ms = 0;
    int64_t resolved_at_ms = 0;  // 0 while still open
    std::string trigger_type;    // e.g. "automatic"
    double observed_value = 0.0;
    double baseline_mean = 0.0;
};

// Owns one SQLite database file. Single-writer by contract: callers must not
// insert from more than one thread concurrently (see README/design notes on
// the single-writer pattern).
class SqliteStore {
public:
    explicit SqliteStore(const std::string& db_path);
    ~SqliteStore();

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    // Writes every sample in one transaction -- the batching write path.
    void insert_batch(const std::vector<MetricSample>& samples);

    // The read path: everything the CLI's `history` command needs -- one
    // metric, one time window.
    AggregatedValue query_range(const std::string& metric, int64_t since_ms, int64_t until_ms);

    // What the anomaly detector needs: mean/stddev over a window, and how far
    // back a metric's history actually goes (to gate evaluation until enough
    // history has accumulated).
    Stats query_stats(const std::string& metric, int64_t since_ms, int64_t until_ms);
    int64_t query_earliest_timestamp(const std::string& metric);  // -1 if no samples exist

    // Incident lifecycle: at most one open incident per domain at a time.
    std::optional<Incident> get_open_incident(const std::string& domain);
    Incident open_incident(const std::string& domain, const std::string& severity,
                            const std::string& trigger_type, double observed_value,
                            double baseline_mean);
    void resolve_incident(const std::string& id, int64_t resolved_at_ms);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace sysintel

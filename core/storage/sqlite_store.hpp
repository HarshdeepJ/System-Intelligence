#pragma once
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

    // The read path: everything the CLI's `history` command and, later, the
    // anomaly/baseline logic need -- one metric, one time window.
    AggregatedValue query_range(const std::string& metric, int64_t since_ms, int64_t until_ms);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace sysintel

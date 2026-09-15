#include "sqlite_store.hpp"

#include <sqlite3.h>

#include <cmath>
#include <stdexcept>

namespace sysintel {

namespace {

void exec_or_throw(sqlite3* db, const char* sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string message = err_msg ? err_msg : "unknown sqlite error";
        sqlite3_free(err_msg);
        throw std::runtime_error("sqlite error: " + message);
    }
}

}  // namespace

SqliteStore::SqliteStore(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string message = db_ ? sqlite3_errmsg(db_) : "unknown error";
        throw std::runtime_error("failed to open sqlite database '" + db_path + "': " + message);
    }

    // WAL mode: writers don't block readers and commits are cheaper than the
    // default rollback-journal mode -- this matters because we flush every
    // few seconds for as long as the recorder keeps running.
    exec_or_throw(db_, "PRAGMA journal_mode=WAL;");
    exec_or_throw(db_, "PRAGMA synchronous=NORMAL;");

    exec_or_throw(db_, R"SQL(
        CREATE TABLE IF NOT EXISTS metric_samples (
            id INTEGER PRIMARY KEY,
            ts INTEGER NOT NULL,
            metric TEXT NOT NULL,
            value REAL NOT NULL,
            unit TEXT
        );
    )SQL");

    // Only index we need: every query so far is "this metric, in this time
    // range" -- built from the actual read pattern, not defensively.
    exec_or_throw(db_, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_metric_ts
        ON metric_samples(metric, ts);
    )SQL");

    exec_or_throw(db_, R"SQL(
        CREATE TABLE IF NOT EXISTS incidents (
            id TEXT PRIMARY KEY,
            domain TEXT NOT NULL,
            status TEXT NOT NULL,
            severity TEXT,
            started_at INTEGER NOT NULL,
            resolved_at INTEGER,
            trigger_type TEXT,
            observed_value REAL,
            baseline_mean REAL
        );
    )SQL");

    exec_or_throw(db_, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_incidents_domain_status
        ON incidents(domain, status);
    )SQL");
}

SqliteStore::~SqliteStore() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void SqliteStore::insert_batch(const std::vector<MetricSample>& samples) {
    if (samples.empty()) {
        return;
    }

    exec_or_throw(db_, "BEGIN TRANSACTION;");

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO metric_samples (ts, metric, value, unit) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        exec_or_throw(db_, "ROLLBACK;");
        throw std::runtime_error("failed to prepare insert statement");
    }

    for (const auto& sample : samples) {
        sqlite3_reset(stmt);
        sqlite3_bind_int64(stmt, 1, sample.timestamp_ms);
        sqlite3_bind_text(stmt, 2, sample.metric.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, sample.value);
        sqlite3_bind_text(stmt, 4, sample.unit.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            exec_or_throw(db_, "ROLLBACK;");
            throw std::runtime_error("failed to insert metric sample");
        }
    }

    sqlite3_finalize(stmt);
    exec_or_throw(db_, "COMMIT;");
}

AggregatedValue SqliteStore::query_range(const std::string& metric, int64_t since_ms,
                                          int64_t until_ms) {
    AggregatedValue result;

    const char* sql =
        "SELECT MIN(value), MAX(value), AVG(value), COUNT(*) "
        "FROM metric_samples WHERE metric = ? AND ts >= ? AND ts <= ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare query statement");
    }

    sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, since_ms);
    sqlite3_bind_int64(stmt, 3, until_ms);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result.sample_count = sqlite3_column_int(stmt, 3);
        if (result.sample_count > 0) {
            result.min_value = sqlite3_column_double(stmt, 0);
            result.max_value = sqlite3_column_double(stmt, 1);
            result.avg_value = sqlite3_column_double(stmt, 2);
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

Stats SqliteStore::query_stats(const std::string& metric, int64_t since_ms, int64_t until_ms) {
    Stats result;

    // variance = E[x^2] - E[x]^2 -- lets SQLite compute mean and stddev in
    // one pass over two SUM/AVG aggregates instead of pulling every row back
    // to compute it ourselves.
    const char* sql =
        "SELECT AVG(value), AVG(value * value), COUNT(*) "
        "FROM metric_samples WHERE metric = ? AND ts >= ? AND ts <= ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare stats query");
    }

    sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, since_ms);
    sqlite3_bind_int64(stmt, 3, until_ms);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result.count = sqlite3_column_int(stmt, 2);
        if (result.count > 0) {
            double mean = sqlite3_column_double(stmt, 0);
            double mean_of_squares = sqlite3_column_double(stmt, 1);
            double variance = mean_of_squares - mean * mean;
            result.mean = mean;
            result.stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

int64_t SqliteStore::query_earliest_timestamp(const std::string& metric) {
    const char* sql = "SELECT MIN(ts) FROM metric_samples WHERE metric = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare earliest-timestamp query");
    }

    sqlite3_bind_text(stmt, 1, metric.c_str(), -1, SQLITE_TRANSIENT);

    int64_t result = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        result = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<Incident> SqliteStore::get_open_incident(const std::string& domain) {
    const char* sql =
        "SELECT id, domain, status, severity, started_at, resolved_at, trigger_type, "
        "observed_value, baseline_mean "
        "FROM incidents WHERE domain = ? AND status = 'open' "
        "ORDER BY started_at DESC LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare open-incident query");
    }

    sqlite3_bind_text(stmt, 1, domain.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Incident> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Incident inc;
        inc.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        inc.domain = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        inc.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        const unsigned char* severity = sqlite3_column_text(stmt, 3);
        inc.severity = severity ? reinterpret_cast<const char*>(severity) : "";

        inc.started_at_ms = sqlite3_column_int64(stmt, 4);
        inc.resolved_at_ms =
            sqlite3_column_type(stmt, 5) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 5);

        const unsigned char* trigger_type = sqlite3_column_text(stmt, 6);
        inc.trigger_type = trigger_type ? reinterpret_cast<const char*>(trigger_type) : "";

        inc.observed_value = sqlite3_column_double(stmt, 7);
        inc.baseline_mean = sqlite3_column_double(stmt, 8);

        result = inc;
    }

    sqlite3_finalize(stmt);
    return result;
}

Incident SqliteStore::open_incident(const std::string& domain, const std::string& severity,
                                     const std::string& trigger_type, double observed_value,
                                     double baseline_mean) {
    Incident inc;
    inc.started_at_ms = current_timestamp_ms();
    inc.id = domain + "-" + std::to_string(inc.started_at_ms);
    inc.domain = domain;
    inc.status = "open";
    inc.severity = severity;
    inc.trigger_type = trigger_type;
    inc.observed_value = observed_value;
    inc.baseline_mean = baseline_mean;

    const char* sql =
        "INSERT INTO incidents (id, domain, status, severity, started_at, trigger_type, "
        "observed_value, baseline_mean) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare insert-incident statement");
    }

    sqlite3_bind_text(stmt, 1, inc.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, inc.domain.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, inc.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, inc.severity.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, inc.started_at_ms);
    sqlite3_bind_text(stmt, 6, inc.trigger_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 7, inc.observed_value);
    sqlite3_bind_double(stmt, 8, inc.baseline_mean);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("failed to insert incident");
    }

    sqlite3_finalize(stmt);
    return inc;
}

void SqliteStore::resolve_incident(const std::string& id, int64_t resolved_at_ms) {
    const char* sql = "UPDATE incidents SET status = 'resolved', resolved_at = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("failed to prepare resolve-incident statement");
    }

    sqlite3_bind_int64(stmt, 1, resolved_at_ms);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("failed to resolve incident");
    }

    sqlite3_finalize(stmt);
}

}  // namespace sysintel

#include "sqlite_store.hpp"

#include <sqlite3.h>

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

}  // namespace sysintel

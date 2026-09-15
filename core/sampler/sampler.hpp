#pragma once
#include <atomic>

#include "../storage/sqlite_store.hpp"

namespace sysintel {

// A single-threaded, round-robin sampling loop: each metric is collected
// when its own interval has elapsed since it was last sampled, everything
// gets buffered in memory, and the buffer is flushed to SQLite in one
// transaction every few seconds (the batching write path).
//
// This is intentionally simple: the real background service will later give
// each collector its own thread/timer (see tech design's "Internal Service
// Threads"), but a recorder CLI doesn't need that complexity yet.
class Sampler {
public:
    explicit Sampler(SqliteStore& store);

    // Blocks until request_stop() is called (e.g. from a Ctrl+C handler on
    // another thread).
    void run();

    void request_stop();

private:
    SqliteStore& store_;
    std::atomic<bool> stop_requested_{false};
};

}  // namespace sysintel

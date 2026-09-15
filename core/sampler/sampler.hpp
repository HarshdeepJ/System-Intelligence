#pragma once
#include <atomic>
#include <chrono>
#include <functional>

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

    // Runs `hook` roughly every `interval` while the loop is running (checked
    // each tick, same as the per-metric intervals). Keeps Sampler ignorant of
    // *why* a caller wants a periodic callback -- e.g. `watch` uses this to
    // run anomaly checks without Sampler depending on anomaly-detection code.
    void set_periodic_hook(std::chrono::seconds interval, std::function<void()> hook);

    // Blocks until request_stop() is called (e.g. from a Ctrl+C handler on
    // another thread).
    void run();

    void request_stop();

private:
    SqliteStore& store_;
    std::atomic<bool> stop_requested_{false};

    std::chrono::seconds hook_interval_{0};
    std::function<void()> hook_;
};

}  // namespace sysintel

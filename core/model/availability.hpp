#pragma once
#include <optional>

namespace sysintel {

// Distinguishes "this hardware/OS genuinely doesn't expose this sensor" from
// "it should be readable but something went wrong" -- collapsing both into a
// bare null (or worse, a 0) is exactly the trap the fan/thermal telemetry
// discussion flagged: a fan reading of 0 could mean "off" or "we have no
// idea," and those are very different facts for a diagnostic agent.
enum class Availability {
    kOk,
    kUnsupported,   // this machine/OS/vendor doesn't expose this at all
    kUnavailable,   // normally readable, but this query failed right now
    kError,         // an unexpected error occurred while collecting it
};

template <typename T>
struct Reading {
    std::optional<T> value;
    Availability availability = Availability::kOk;

    static Reading<T> ok(T v) { return Reading<T>{std::move(v), Availability::kOk}; }
    static Reading<T> unsupported() {
        return Reading<T>{std::nullopt, Availability::kUnsupported};
    }
    static Reading<T> unavailable() {
        return Reading<T>{std::nullopt, Availability::kUnavailable};
    }
    static Reading<T> error() { return Reading<T>{std::nullopt, Availability::kError}; }
};

inline const char* availability_name(Availability a) {
    switch (a) {
        case Availability::kOk:
            return "ok";
        case Availability::kUnsupported:
            return "unsupported";
        case Availability::kUnavailable:
            return "unavailable";
        case Availability::kError:
            return "error";
    }
    return "unknown";
}

}  // namespace sysintel

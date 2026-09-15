#pragma once
#include <iomanip>
#include <sstream>
#include <string>

namespace sysintel {

// Minimal hand-rolled JSON helpers: every object this project serializes has
// a small, fixed shape, so a real JSON library would be machinery we don't
// need yet (same call as vendoring SQLite directly instead of a package
// manager for one dependency).
inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
        }
    }
    return out;
}

inline std::string json_num(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << value;
    return oss.str();
}

}  // namespace sysintel

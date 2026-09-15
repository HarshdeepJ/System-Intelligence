#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace sysintel {

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

}  // namespace sysintel

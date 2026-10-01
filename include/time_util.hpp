#pragma once
#include <chrono>
#include <string>

using Clock = std::chrono::steady_clock;

inline std::string current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(ms.count());
}

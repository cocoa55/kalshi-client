#pragma once
#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "time_util.hpp"

// Records durations into a fixed-size ring buffer (so memory stays bounded however long the bot runs)
// and reports percentiles over the most recent samples. Recording is a store and an increment; all the
// sorting happens in summary(), off the hot path.
class LatencyStats {
    static constexpr size_t kCapacity = 1 << 16;

    std::string _name;
    std::vector<int64_t> _samples_ns;
    size_t _next{0};
    uint64_t _count{0};

    static std::string pretty(const int64_t ns) {
        if (ns < 1'000) return std::format("{}ns", ns);
        if (ns < 1'000'000) return std::format("{:.1f}us", static_cast<double>(ns) / 1e3);
        return std::format("{:.1f}ms", static_cast<double>(ns) / 1e6);
    }

public:
    explicit LatencyStats(std::string name) : _name(std::move(name)) { _samples_ns.reserve(kCapacity); }

    void record(const Clock::duration d) {
        const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
        if (_samples_ns.size() < kCapacity)
            _samples_ns.push_back(ns);
        else
            _samples_ns[_next] = ns;
        _next = (_next + 1) % kCapacity;
        ++_count;
    }

    uint64_t count() const { return _count; }

    std::string summary() const {
        if (_samples_ns.empty())
            return std::format("{:<22} no samples", _name);
        std::vector<int64_t> sorted = _samples_ns;
        std::ranges::sort(sorted);
        auto pct = [&](const double p) { return sorted[static_cast<size_t>(p * static_cast<double>(sorted.size() - 1))]; };
        return std::format("{:<22} n={:<7} p50={:<9} p90={:<9} p99={:<9} max={}", _name, _count, pretty(pct(0.50)),
                           pretty(pct(0.90)), pretty(pct(0.99)), pretty(sorted.back()));
    }
};

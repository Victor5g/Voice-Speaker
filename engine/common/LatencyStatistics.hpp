#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <vector>
namespace lvt {
struct LatencySummary { std::size_t count{}; double last{}, p50{}, p95{}; };
class LatencyStatistics {
    std::deque<double> values_;
    mutable std::mutex mutex_;
public:
    void add(double ms) { if (!std::isfinite(ms) || ms < 0) return; std::lock_guard lock(mutex_); values_.push_back(ms); if (values_.size() > 256) values_.pop_front(); }
    LatencySummary snapshot() const {
        std::lock_guard lock(mutex_);
        LatencySummary result{values_.size(), values_.empty() ? 0 : values_.back(), 0, 0};
        if (values_.size() < 30) return result;
        std::vector<double> sorted(values_.begin(), values_.end()); std::sort(sorted.begin(), sorted.end());
        result.p50 = sorted[std::size_t(std::ceil(sorted.size() * .5)) - 1];
        result.p95 = sorted[std::size_t(std::ceil(sorted.size() * .95)) - 1]; return result;
    }
    void clear() { std::lock_guard lock(mutex_); values_.clear(); }
};
}

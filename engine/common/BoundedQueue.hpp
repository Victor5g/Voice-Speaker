#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>

namespace lvt {
struct QueueMetrics { std::size_t currentDepth{}, capacity{}, maxDepth{}, droppedItems{}; double oldestItemAgeMs{}; };
// Worker-to-worker queue. Never called from the hardware audio callbacks.
template<typename T, std::size_t Capacity> class BoundedQueue {
    using Clock = std::chrono::steady_clock;
    struct Entry { T value; Clock::time_point queued; };
    std::deque<Entry> items_;
    mutable std::mutex mutex_;
    std::condition_variable_any ready_;
    std::size_t dropped_{}, highWater_{};
public:
    bool push(T item) {
        std::lock_guard lock(mutex_);
        if (items_.size() == Capacity) { ++dropped_; return false; }
        items_.push_back({std::move(item), Clock::now()});
        highWater_ = std::max(highWater_, items_.size());
        ready_.notify_one(); return true;
    }
    bool tryPop(T& item) {
        std::lock_guard lock(mutex_);
        if (items_.empty()) return false;
        item = std::move(items_.front().value); items_.pop_front(); return true;
    }
    bool waitPop(T& item, std::stop_token stop) {
        std::unique_lock lock(mutex_);
        if (!ready_.wait(lock, stop, [this] { return !items_.empty(); })) return false;
        if (stop.stop_requested()) return false;
        item = std::move(items_.front().value); items_.pop_front(); return true;
    }
    bool waitPopFor(T& item, std::stop_token stop, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!ready_.wait_for(lock, stop, timeout, [this] { return !items_.empty(); }) || stop.stop_requested()) return false;
        item = std::move(items_.front().value); items_.pop_front(); return true;
    }
    QueueMetrics metrics() const {
        std::lock_guard lock(mutex_);
        return {items_.size(), Capacity, highWater_, dropped_, items_.empty() ? 0 : std::chrono::duration<double, std::milli>(Clock::now() - items_.front().queued).count()};
    }
    void clear() {
        std::lock_guard lock(mutex_); items_.clear(); dropped_ = 0; highWater_ = 0;
    }
};
}

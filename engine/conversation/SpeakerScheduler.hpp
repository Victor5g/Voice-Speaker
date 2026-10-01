#pragma once
#include "../common/BoundedQueue.hpp"
#include "ConversationTypes.hpp"
#include <map>
namespace lvt {
// Worker-only FIFO per participant, with earliest capture time across queue heads.
// A saturated participant cannot consume all admission slots.
class SpeakerScheduler {
    using Clock = std::chrono::steady_clock;
    struct Entry { TranslationResult result; Clock::time_point queued; };
    std::map<std::uint32_t, std::deque<Entry>> speakers_;
    std::size_t depth_{}, dropped_{}, highWater_{};
    mutable std::mutex mutex_;
    std::condition_variable_any ready_;
    bool popLocked(TranslationResult& result) {
        auto chosen = speakers_.end();
        for (auto it = speakers_.begin(); it != speakers_.end(); ++it) {
            if (chosen == speakers_.end()) { chosen = it; continue; }
            const auto& a = it->second.front().result.identity; const auto& b = chosen->second.front().result.identity;
            if (std::pair(a.captureHostTime, a.segment) < std::pair(b.captureHostTime, b.segment)) chosen = it;
        }
        result = std::move(chosen->second.front().result); chosen->second.pop_front(); --depth_;
        if (chosen->second.empty()) speakers_.erase(chosen); return true;
    }
public:
    bool push(TranslationResult result) {
        std::lock_guard lock(mutex_);
        auto it = speakers_.find(result.identity.speaker);
        if (depth_ >= 8 || (it != speakers_.end() && it->second.size() >= 4)) { ++dropped_; return false; }
        speakers_[result.identity.speaker].push_back({std::move(result), Clock::now()});
        ++depth_; highWater_ = std::max(highWater_, depth_); ready_.notify_one(); return true;
    }
    bool waitPop(TranslationResult& result, std::stop_token stop) {
        std::unique_lock lock(mutex_);
        if (!ready_.wait(lock, stop, [&] { return depth_ > 0; }) || stop.stop_requested()) return false;
        return popLocked(result);
    }
    bool waitPopFor(TranslationResult& result, std::stop_token stop, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!ready_.wait_for(lock, stop, timeout, [&] { return depth_ > 0; }) || stop.stop_requested()) return false;
        return popLocked(result);
    }
    QueueMetrics metrics() const {
        std::lock_guard lock(mutex_); auto oldest = Clock::now();
        for (const auto& [id, queue] : speakers_) oldest = std::min(oldest, queue.front().queued);
        return {depth_, 8, highWater_, dropped_, depth_ ? std::chrono::duration<double, std::milli>(Clock::now() - oldest).count() : 0};
    }
    void clear() { std::lock_guard lock(mutex_); speakers_.clear(); depth_ = 0; dropped_ = 0; highWater_ = 0; }
};
}

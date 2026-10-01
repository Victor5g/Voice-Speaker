#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <stop_token>
#include <utility>

namespace lvt {
// One waiting request per ASR worker. Final requests precede previews; requests
// of the same class are FIFO, so reacquiring on one route cannot bypass another
// route already waiting. An interruptible preview may cooperatively yield to a
// final; the slot remains exclusively owned until its lease is released.
// This runs only on inference workers, never on an audio callback.
class InferenceArbiter {
    struct Request { bool final; };
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    std::list<Request*> waiting_;
    bool active_{};
    bool interruptible_{};
    std::atomic<bool> yieldRequested_{false};
    Request* next() const {
        auto final = std::find_if(waiting_.begin(), waiting_.end(), [](const auto* r) { return r->final; });
        return final != waiting_.end() ? *final : waiting_.empty() ? nullptr : waiting_.front();
    }
    void release() {
        { std::lock_guard lock(mutex_); active_ = false; interruptible_ = false; yieldRequested_ = false; }
        changed_.notify_all();
    }
public:
    class Lease {
        InferenceArbiter* owner_{};
        friend class InferenceArbiter;
        explicit Lease(InferenceArbiter* owner) : owner_(owner) {}
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) { reset(); owner_ = std::exchange(other.owner_, nullptr); }
            return *this;
        }
        ~Lease() { reset(); }
        explicit operator bool() const { return owner_ != nullptr; }
        // Lock-free query for Whisper's cooperative abort callback. A request
        // stays latched even if its final is subsequently cancelled. No callback
        // can revoke an abort already observed by the backend.
        bool shouldYield() const { return owner_ && owner_->yieldRequested_.load(); }
        void reset() { if (auto* owner = std::exchange(owner_, nullptr)) owner->release(); }
    };
    struct Snapshot { bool active; std::size_t finals, previews; };
    Snapshot snapshot() const {
        std::lock_guard lock(mutex_);
        std::size_t finals = 0;
        for (const auto* request : waiting_) finals += request->final;
        return {active_, finals, waiting_.size() - finals};
    }
    Lease acquire(bool final, std::stop_token stop, const std::function<bool()>& obsolete = {}, bool interruptiblePreview = false) {
        Request request{final};
        std::unique_lock lock(mutex_);
        auto position = waiting_.insert(waiting_.end(), &request);
        auto remove = [&] { waiting_.erase(position); changed_.notify_all(); };
        try {
            for (;;) {
                // External callbacks must not execute with the scheduler locked.
                // A failing callback also removes its request before unwinding.
                lock.unlock();
                const bool cancelled = stop.stop_requested() || (obsolete && obsolete());
                lock.lock();
                if (cancelled) { remove(); return {}; }
                if (final && active_ && interruptible_) yieldRequested_ = true;
                if (!active_ && next() == &request) {
                    remove(); active_ = true; interruptible_ = !final && interruptiblePreview;
                    yieldRequested_ = false;
                    return Lease(this);
                }
                // Stop wakes immediately. Revision changes have no notification
                // contract, so retain the previous 20 ms cancellation check bound.
                changed_.wait_for(lock, stop, std::chrono::milliseconds(20), [&] {
                    return !active_ && next() == &request;
                });
            }
        } catch (...) {
            if (!lock.owns_lock()) lock.lock();
            remove();
            throw;
        }
    }
};
}

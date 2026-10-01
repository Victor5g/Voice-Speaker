#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace lvt {
// Exactly one producer and one consumer. All storage is allocated up front.
template<typename T, std::size_t Capacity> class RingBuffer {
    static_assert(Capacity > 0 && std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    std::array<T, Capacity> data_{};
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> dropped_{0}, highWater_{0};
public:
    bool push(const T& value) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        if (w - r >= Capacity) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
        data_[w % Capacity] = value;
        write_.store(w + 1, std::memory_order_release);
        const auto depth = w + 1 - r;
        if (depth > highWater_.load(std::memory_order_relaxed)) highWater_.store(depth, std::memory_order_relaxed);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        value = data_[r % Capacity];
        data_[r % Capacity] = T{};
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    std::size_t depth() const noexcept {
        const auto r = read_.load(std::memory_order_acquire);
        const auto w = write_.load(std::memory_order_acquire);
        return w >= r ? (w - r > Capacity ? Capacity : w - r) : 0;
    }
    std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    std::size_t highWater() const noexcept { return highWater_.load(std::memory_order_relaxed); }
    static constexpr std::size_t capacity() { return Capacity; }
    // Only when producer AND consumer have stopped.
    void clearStopped() noexcept {
        data_.fill(T{}); write_ = 0; read_ = 0; dropped_ = 0; highWater_ = 0;
    }
};
}

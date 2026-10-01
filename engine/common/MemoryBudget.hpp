#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <mach/mach.h>
namespace lvt {
struct MemorySnapshot { std::size_t footprint{}, reserved{}, limit{}, denied{}; unsigned pressure{}; };
class MemoryBudget {
    std::size_t limit_, reserved_{}, denied_{};
    std::atomic<unsigned> pressure_{0};
    std::function<std::size_t()> footprint_;
    mutable std::mutex mutex_;
public:
    struct Unavailable : std::runtime_error { Unavailable() : std::runtime_error("Orçamento de memória insuficiente para esta inferência.") {} };
    static std::size_t physicalFootprint() {
        task_vm_info_data_t vm{}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) != KERN_SUCCESS)
            return 8ULL << 30; // Missing measurement cannot silently permit a heavy allocation.
        return vm.phys_footprint;
    }
    explicit MemoryBudget(std::size_t limit = 8ULL << 30, std::function<std::size_t()> probe = physicalFootprint)
        : limit_(limit), footprint_(std::move(probe)) {}
    class Reservation {
        MemoryBudget& owner_;
        std::size_t size_;
    public:
        Reservation(MemoryBudget& owner, std::size_t size) : owner_(owner), size_(size) {}
        Reservation(const Reservation&) = delete;
        ~Reservation() { std::lock_guard lock(owner_.mutex_); owner_.reserved_ -= size_; }
    };
    using Ticket = std::unique_ptr<Reservation>;
    static MemoryBudget& shared() { static MemoryBudget instance; return instance; }
    Ticket claim(std::size_t estimate) {
        const auto footprint = footprint_();
        std::lock_guard lock(mutex_);
        // Reservations are deliberately conservative: current resident bytes and
        // outstanding estimates are both counted, including other stopping sessions.
        if (pressure_ >= 2 || footprint >= limit_ || reserved_ > limit_ - footprint || estimate > limit_ - footprint - reserved_) {
            ++denied_; throw Unavailable();
        }
        auto ticket = std::make_unique<Reservation>(*this, estimate); reserved_ += estimate; return ticket;
    }
    void setPressure(unsigned pressure) { pressure_ = pressure; }
    unsigned pressure() const { return pressure_; }
    MemorySnapshot snapshot() const {
        const auto footprint = footprint_(); std::lock_guard lock(mutex_);
        return {footprint, reserved_, limit_, denied_, pressure_.load()};
    }
};
}

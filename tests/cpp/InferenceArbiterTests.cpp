#include "../../engine/asr/InferenceArbiter.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace lvt;
using namespace std::chrono_literals;
static int passed;
static void check(bool ok, const char* text) {
    if (!ok) throw std::runtime_error(text);
    ++passed; std::cout << "PASS " << text << '\n';
}
template<class Predicate> void await(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!ready()) {
        if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Arbiter test synchronization timeout");
        std::this_thread::sleep_for(1ms);
    }
}
int main() { try {
    {
        InferenceArbiter arbiter;
        auto active = arbiter.acquire(false, {});
        std::vector<int> order;
        std::mutex orderMutex;
        auto run = [&](int id, bool final, std::stop_token stop) {
            auto lease = arbiter.acquire(final, stop);
            if (lease) { std::lock_guard lock(orderMutex); order.push_back(id); }
        };
        std::jthread p1([&](std::stop_token stop) { run(1, false, stop); });
        await([&] { return arbiter.snapshot().previews == 1; });
        std::jthread p2([&](std::stop_token stop) { run(2, false, stop); });
        await([&] { return arbiter.snapshot().previews == 2; });
        std::jthread f1([&](std::stop_token stop) { run(3, true, stop); });
        await([&] { return arbiter.snapshot().finals == 1; });
        std::jthread f2([&](std::stop_token stop) { run(4, true, stop); });
        await([&] { return arbiter.snapshot().finals == 2; });
        check(order.empty() && arbiter.snapshot().active && !active.shouldYield(), "A final cannot interrupt a non-interruptible Whisper transaction");
        active.reset(); p1.join(); p2.join(); f1.join(); f2.join();
        check(order == std::vector<int>({3,4,1,2}), "Finals precede previews with FIFO fairness within both classes");
        check(!arbiter.snapshot().active && arbiter.snapshot().finals == 0 && arbiter.snapshot().previews == 0,
              "Every completed request releases its inference slot");
    }
    {
        InferenceArbiter arbiter;
        auto active = arbiter.acquire(true, {});
        std::atomic<bool> granted{false};
        std::jthread cancelled([&](std::stop_token stop) { granted = bool(arbiter.acquire(true, stop)); });
        await([&] { return arbiter.snapshot().finals == 1; });
        cancelled.request_stop(); cancelled.join();
        check(!granted && arbiter.snapshot().finals == 0 && arbiter.snapshot().active,
              "STOP removes a waiting final without waiting for the active model");
        std::atomic<bool> outdated{false};
        std::jthread preview([&](std::stop_token stop) { granted = bool(arbiter.acquire(false, stop, [&] { return outdated.load(); })); });
        await([&] { return arbiter.snapshot().previews == 1; });
        outdated = true;
        await([&] { return arbiter.snapshot().previews == 0; }); preview.join();
        check(!granted && arbiter.snapshot().active, "A superseded preview leaves the queue before the active model finishes");
    }
    {
        InferenceArbiter arbiter;
        std::stop_source stopped; stopped.request_stop();
        check(!arbiter.acquire(true, stopped.get_token()) && !arbiter.snapshot().active,
              "An already cancelled session cannot enter an idle model");
        bool caught = false;
        try { arbiter.acquire(false, {}, [&]() -> bool { (void)arbiter.snapshot(); throw std::runtime_error("revision failed"); }); }
        catch (const std::runtime_error&) { caught = true; }
        check(caught && arbiter.snapshot().previews == 0 && bool(arbiter.acquire(true, {})),
              "An external callback can inspect state and throw without leaking a queue entry");
        try { auto lease = arbiter.acquire(true, {}); throw std::runtime_error("inference failed"); }
        catch (const std::runtime_error&) {}
        check(!arbiter.snapshot().active && bool(arbiter.acquire(true, {})),
              "An inference exception releases the shared model for the other route");
        auto lease = arbiter.acquire(true, {});
        auto moved = std::move(lease); lease.reset();
        bool retained = arbiter.snapshot().active;
        moved.reset(); moved.reset();
        check(retained && !arbiter.snapshot().active, "Moving or resetting a lease never releases another owner's slot");
    }
    {
        InferenceArbiter arbiter;
        std::atomic<int> concurrent{0}, violations{0}, completed{0};
        std::vector<std::jthread> routes;
        for (int route = 0; route < 4; ++route) routes.emplace_back([&, route](std::stop_token stop) {
            for (int i = 0; i < 100; ++i) {
                auto lease = arbiter.acquire((i + route) % 3 == 0, stop);
                if (!lease) return;
                if (++concurrent != 1) ++violations;
                std::this_thread::yield();
                --concurrent; ++completed;
            }
        });
        for (auto& route : routes) route.join();
        check(completed == 400 && violations == 0 && !arbiter.snapshot().active,
              "Contended routes complete 400 transactions without concurrent access or lost work");
    }
    {
        InferenceArbiter arbiter;
        auto active = arbiter.acquire(false, {}, {}, true);
        std::atomic<bool> granted{false};
        std::jthread final([&](std::stop_token stop) { auto lease = arbiter.acquire(true, stop); granted = bool(lease); });
        await([&] { return active.shouldYield(); });
        check(!granted && arbiter.snapshot().active, "A queued final requests a yield but cannot enter an active preview");
        final.request_stop(); final.join();
        check(active.shouldYield() && !granted && arbiter.snapshot().finals == 0,
              "Cancelling the requester cannot revoke an abort already observed by the backend");
        auto moved = std::move(active);
        check(!active.shouldYield() && moved.shouldYield(), "Moving a lease transfers its latched yield request");
        moved.reset();
        auto fresh = arbiter.acquire(false, {}, {}, true);
        check(!fresh.shouldYield(), "A new inference never inherits an old abort request");
        std::stop_source stopped; stopped.request_stop();
        check(!arbiter.acquire(true, stopped.get_token()) && !fresh.shouldYield(), "An already cancelled final cannot interrupt a preview");
        check(!arbiter.acquire(true, {}, [] { return true; }) && !fresh.shouldYield(), "An obsolete final cannot interrupt a preview");
        std::jthread preview([&](std::stop_token stop) { (void)arbiter.acquire(false, stop, {}, true); });
        await([&] { return arbiter.snapshot().previews == 1; });
        check(!fresh.shouldYield(), "Another preview cannot interrupt the active preview");
        fresh.reset(); preview.join();
        auto finalLease = arbiter.acquire(true, {}, {}, true);
        std::jthread anotherFinal([&](std::stop_token stop) { (void)arbiter.acquire(true, stop); });
        await([&] { return arbiter.snapshot().finals == 1; });
        check(!finalLease.shouldYield(), "Final inference is never interruptible even when requested by the caller");
        finalLease.reset(); anotherFinal.join();
    }
    std::cout << "RESULT " << passed << " passed, 0 failed\n";
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }

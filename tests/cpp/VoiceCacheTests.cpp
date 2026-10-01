#include "../../engine/tts/VoiceCache.hpp"
#include <iostream>
using namespace lvt;
static unsigned passed;
static void check(bool ok, const char* label) { if (!ok) throw std::runtime_error(label); ++passed; std::cout << "PASS " << label << '\n'; }
struct Model {
    MemoryBudget::Ticket ticket;
    int id;
    Model(MemoryBudget& budget, int value) : ticket(budget.claim(100)), id(value) {}
};
int main() { try {
    MemoryBudget budget(250, [] { return 0; });
    VoiceCache<Model> cache;
    int loads = 0;
    auto factory = [&] { return std::make_unique<Model>(budget, ++loads); };
    auto a = cache.get("a", factory);
    const int firstID = a.engine.id;
    check(a.loaded && budget.snapshot().reserved == 100, "First voice is admitted before it is cached");
    cache.get("b", factory);
    auto again = cache.get("a", factory);
    check(!again.loaded && again.engine.id == firstID && loads == 2, "Returning participant reuses the same model without reloading");
    cache.get("c", factory);
    check(cache.voices() == std::vector<std::string>{"c", "a"} && budget.snapshot().reserved == 200, "Least recently used voice is destroyed before allocating a third");
    auto reloaded = cache.get("b", factory);
    check(reloaded.loaded && cache.voices() == std::vector<std::string>{"b", "c"}, "Evicted voice is loaded explicitly on return");
    auto pressured = cache.get("b", factory, true);
    check(!pressured.loaded && pressured.engine.id==reloaded.engine.id && cache.voices().size() == 1 && budget.snapshot().reserved == 100, "Memory warning evicts inactive voices without reloading the requested model");
    auto competing = budget.claim(100);
    auto recovered = cache.get("a", factory);
    check(recovered.loaded && cache.voices().size() == 1 && budget.snapshot().reserved == 200, "Denied second voice evicts the idle cache and retries admission once");
    budget.setPressure(2);
    bool denied = false;
    try { cache.get("c", factory); } catch (const MemoryBudget::Unavailable&) { denied = true; }
    check(denied && cache.voices().empty() && budget.snapshot().reserved == 100, "Persistent denial reports unavailable and leaves no stale model");
    budget.setPressure(0); competing.reset();
    cache.get("a", factory);
    bool corrupt = false; int attempts = 0;
    try { cache.get("bad", [&]() -> std::unique_ptr<Model> { ++attempts; throw std::runtime_error("corrupt"); }); }
    catch (const std::runtime_error&) { corrupt = true; }
    check(corrupt && attempts == 1 && cache.voices() == std::vector<std::string>{"a"}, "Corrupt model fails explicitly without retries or substituting another voice");
    cache.clear();
    check(budget.snapshot().reserved == 0 && cache.voices().empty(), "Worker cleanup releases all model reservations");
    { VoiceCache<Model> temporary; temporary.get("a", factory); }
    check(budget.snapshot().reserved == 0, "Worker destruction also releases the cache");
    std::cout << "RESULT " << passed << " passed, 0 failed\n";
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }

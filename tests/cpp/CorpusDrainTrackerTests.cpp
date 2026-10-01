#include "CorpusDrainTracker.hpp"
#include <iostream>

int main() { try {
    unsigned passed=0;
    auto check=[&](bool value,const char* text) { if(!value)throw std::runtime_error(text);++passed; };
    lvt::test::CorpusDrainTracker tracker;
    check(tracker.idle(),"No accepted segments means no outstanding work");
    tracker.observe(30,"recognized"); tracker.observe(30,"translating");
    // Reproduce the Haiti fixture failure: translated == played == empty and
    // queues empty while translation is still executing for more than 2 s.
    check(!tracker.idle() && tracker.pending()==1,"Active translation cannot satisfy drain regardless of elapsed quiet time");
    tracker.observe(30,"translated"); tracker.observe(30,"synthesizing");
    check(tracker.pending()==1,"Stage transitions do not duplicate an outstanding unit");
    tracker.observe(30,"queued"); tracker.observe(30,"playing"); tracker.observe(30,"audio-onset");
    check(!tracker.idle(),"First audible PCM is not playback completion");
    tracker.observe(31,"recognized"); tracker.observe(30,"played");
    check(tracker.pending()==1,"One finished unit cannot hide another accepted unit");
    tracker.observe(31,"played");
    check(tracker.idle(),"All accepted units finish before the next recording");
    tracker.observe(100,"partial");tracker.observe(100,"removed");tracker.observe(100,"direction-filtered");
    check(tracker.idle(),"Uncommitted display and filtered events do not create synthesis work");
    for(const auto state:{"error","dropped","unavailable"}) {
        tracker.observe(40,"recognized");tracker.observe(40,state);
        check(tracker.idle(),"Explicit terminal failure releases its accepted unit");
    }
    tracker.observe(99,"translated");
    check(!tracker.idle(),"Later work events still prevent false drain if admission event was unavailable");
    tracker.observe(99,"played");tracker.observe(99,"played");
    check(tracker.idle(),"A duplicate terminal event cannot underflow tracking");
    bool bounded=false;
    try { for(std::uint64_t id=0;id<=512;++id)tracker.observe(id,"recognized"); }
    catch(const std::runtime_error&) { bounded=true; }
    check(bounded,"Unfinished work is bounded and fails explicitly");
    std::cout<<"RESULT "<<passed<<" passed, 0 failed\n";
} catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; } }

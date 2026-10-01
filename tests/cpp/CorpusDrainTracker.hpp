#pragma once
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string_view>

namespace lvt::test {
// Test-side completion ledger. Queue depth does not include a worker's current
// item. A confirmed segment remains pending through translation, synthesis and
// playback, even when every queue is empty for more than the quiet interval.
class CorpusDrainTracker {
    std::set<std::uint64_t> pending_;
public:
    void observe(std::uint64_t segment, std::string_view state) {
        if (state == "played" || state == "error" || state == "dropped" || state == "unavailable" || state=="text-only" || state=="audio-skipped") {
            pending_.erase(segment);
        } else if (state == "recognized" || state == "translating" || state == "translated" ||
                   state == "synthesizing" || state == "queued" || state == "playing" || state == "audio-onset") {
            if (!pending_.contains(segment) && pending_.size() >= 512)
                throw std::runtime_error("Corpus completion ledger exceeded its bound");
            pending_.insert(segment);
        }
    }
    bool idle() const { return pending_.empty(); }
    std::size_t pending() const { return pending_.size(); }
};
}

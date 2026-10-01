#pragma once
#include "../common/MemoryBudget.hpp"
#include <list>
#include <string>
#include <vector>

namespace lvt {
// Owned exclusively by the TTS worker. Factories must obtain their memory ticket
// before allocating a model. References remain valid until the next get/clear.
template<class Engine> class VoiceCache {
    struct Entry { std::string voice; std::unique_ptr<Engine> engine; };
    std::list<Entry> entries_;
    std::size_t capacity_;
public:
    struct Access { Engine& engine; bool loaded; };
    explicit VoiceCache(std::size_t capacity = 2) : capacity_(capacity) {
        if (!capacity || capacity > 2) throw std::invalid_argument("Voice cache capacity must be one or two");
    }
    template<class Factory> Access get(const std::string& voice, Factory factory, bool pressure = false) {
        // A warning evicts other voices, not the model about to be reused.
        // Clearing on every hit caused one full reload per translated segment.
        if (pressure) entries_.remove_if([&](const auto& entry){return entry.voice!=voice;});
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->voice == voice) {
                entries_.splice(entries_.begin(), entries_, it);
                return {*entries_.front().engine, false};
            }
        }
        if (entries_.size() == capacity_) entries_.pop_back();
        std::unique_ptr<Engine> engine;
        try { engine = factory(); }
        catch (const MemoryBudget::Unavailable&) {
            // An inactive cached voice must not deny the requested voice. Release
            // it and retry admission once; persistent denial remains explicit.
            if (entries_.empty()) throw;
            clear(); engine = factory();
        }
        if (!engine) throw std::runtime_error("Voice factory returned no model");
        entries_.push_front({voice, std::move(engine)});
        return {*entries_.front().engine, true};
    }
    void clear() { entries_.clear(); }
    void trimIdle() { while(entries_.size()>1)entries_.pop_back(); }
    std::vector<std::string> voices() const {
        std::vector<std::string> result;
        for (const auto& entry : entries_) result.push_back(entry.voice);
        return result;
    }
};
}

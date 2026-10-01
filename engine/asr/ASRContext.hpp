#pragma once
#include "../common/PipelineTiming.hpp"
#include <map>
#include <string>

namespace lvt {
// Only an artificial window limit can carry a sentence into the next window.
// Replace this snapshot after every final, even when recognition produced no
// units. A silence endpoint must never leave a previous turn's prompt alive.
class ASRContext {
    struct Entry { std::string text; std::uint64_t endFrame{}; };
    std::map<std::pair<std::uint32_t,std::string>,Entry> entries_;
    static bool adjacent(std::uint64_t a,std::uint64_t b) {
        // floor(absolute frame * ratio) versus round(length * ratio) can differ
        // by one 16 kHz sample. This is not a pause/turn grace interval.
        return a>b?a-b<=1:b-a<=1;
    }
public:
    std::string prompt(std::uint32_t speaker,const std::string& language,std::uint64_t firstFrame) const {
        const auto found=entries_.find({speaker,language});
        return speaker&&found!=entries_.end()&&adjacent(firstFrame,found->second.endFrame)?found->second.text:"";
    }
    void retainFinal(const ASRContext& previous,std::uint32_t speaker,const std::string& language,
                     const std::string& text,std::uint64_t firstFrame,std::uint64_t endFrame,
                     std::uint64_t segmentEndFrame,EndpointReason reason,bool overlap=false) {
        if(!speaker||text.empty()||overlap||reason!=EndpointReason::windowLimit||!adjacent(endFrame,segmentEndFrame))return;
        auto combined=previous.prompt(speaker,language,firstFrame);
        if(!combined.empty())combined+=' ';
        combined+=text;
        if(combined.size()>512) {
            const auto boundary=combined.find(' ',combined.size()-512);
            combined=boundary==std::string::npos?"":combined.substr(boundary+1);
        }
        entries_[{speaker,language}]={std::move(combined),endFrame};
    }
};
}

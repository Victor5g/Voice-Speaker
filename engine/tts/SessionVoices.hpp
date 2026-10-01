#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
namespace lvt {
// Worker-owned, session-local fixed pairs. Unknown identity never resets the
// last voice, and changing language never changes the participant's pair.
class SessionVoices {
    std::array<int,65> assigned_{};
    unsigned lastPair_{};
public:
    SessionVoices(){assigned_.fill(-1);}
    std::string voice(std::uint32_t speaker,const std::string& target,bool automatic=true,unsigned manual=0){
        if(target!="pt"&&target!="pt-BR"&&target!="en")throw std::invalid_argument("Unsupported voice language");
        if(manual>2)throw std::invalid_argument("Unsupported voice index");
        unsigned pair=manual;
        if(automatic){
            if(speaker&&speaker<assigned_.size()){
                auto& fixed=assigned_[speaker]; if(fixed<0)fixed=int((speaker-1)%3);
                lastPair_=unsigned(fixed);
            }
            pair=lastPair_;
        }
        constexpr const char* names[][2]={{"pt_BR-faber-medium","en_US-lessac-medium"},{"pt_BR-jeff-medium","en_US-ryan-medium"},{"pt_BR-edresson-low","en_US-amy-medium"}};
        return names[pair][target=="en"];
    }
};
}

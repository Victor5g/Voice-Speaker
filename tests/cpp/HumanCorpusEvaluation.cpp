#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/common/ModelHash.hpp"
#include "../../engine/common/MemoryBudget.hpp"
#include "StreamingRunConfig.hpp"
#include "TimelineJSON.hpp"
#include "CorpusDrainTracker.hpp"
#include <iostream>
#include <set>

// Real-time production route, with the preceding output drained before the next
// human recording. This isolates free-output response; it is not a live call test.
int main(int argc, char** argv) { try {
    if (argc!=3) throw std::runtime_error("Usage: human-corpus-evaluation MODEL_ROOT RUN_CONFIG_JSON");
    const std::filesystem::path root(argv[1]);
    nlohmann::json run, manifest, selection;
    std::ifstream(argv[2])>>run;
    std::ifstream("tests/data/latency-human/manifest.json")>>manifest;
    std::ifstream(root/"selection.json")>>selection;
    const auto language=run.at("language").get<std::string>();
    if (language!="pt" && language!="en") throw std::runtime_error("Expected pt or en corpus route");
    const auto locale=language=="pt"?"pt_br":"en_us";
    const auto split=run.value("split","development");
    if (split!="development" && !(split=="reserved" && run.value("allowReserved",false))) throw std::runtime_error("Reserved corpus requires an explicit frozen-evaluation config");
    const auto startIndex=run.value("startIndex",0);
    const auto limit=run.value("limit",30);
    if(startIndex<0 || startIndex>=30 || limit<1 || limit>30-startIndex)
        throw std::runtime_error("Expected a corpus range inside its 30-recording split");
    const auto policy=lvt::test::streamingPolicy(run);
    lvt::AudioCore core;
    core.configureStreaming(policy);core.setSourceLanguage(language);
    core.configureASR((root/selection.at("filename").get<std::string>()).string(),selection.at("sha256"));
    core.configureConversation(root.string(),48000);core.start();
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    while (std::chrono::steady_clock::now()<deadline) {
        auto s=core.snapshot();
        if(s.asr.state==3 || s.conversation.translationState==3 || s.conversation.ttsState==3)throw std::runtime_error("Corpus model initialization failed");
        if(s.asr.state==2 && s.conversation.translationState==2 && s.conversation.ttsState==2)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if(core.snapshot().asr.state!=2 || core.snapshot().conversation.ttsState!=2 || core.snapshot().conversation.translationState!=2)throw std::runtime_error("Corpus initialization timed out");
    unsigned recordings=0,failed=0;int eligible=0;std::size_t peak=0;
    std::array<float,320> silence{};std::array<float,960> output{};
    for(const auto& record:manifest.at("recordings")) {
        if(record.at("language")!=locale || record.at("split")!=split)continue;
        if(eligible++<startIndex)continue;
        if(recordings>=static_cast<unsigned>(limit))break;
        const auto filename=record.at("filename").get<std::string>();
        lvt::verifyModelHash(filename,record.at("sha256"));
        auto pcm=lvt::readWav16k(filename);
        const auto begin=std::chrono::steady_clock::now();auto next=begin, lastEvent=begin, ended=begin, idleSince=begin;
        bool idle=false;
        const auto before=core.snapshot();const auto firstFrame=before.inputFrames;
        std::uint64_t captureOrigin=0;std::size_t cursor=0;
        std::string original,translation;std::set<std::uint64_t> translated,played;
        lvt::test::CorpusDrainTracker completion;
        std::set<unsigned> speakers;std::vector<std::string> errors;
        bool drained=false;std::size_t onsets=0,unknown=0;double maxLateness=0;
        deadline=begin+std::chrono::seconds(120+(pcm.size()+15999)/16000);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto now=std::chrono::steady_clock::now();
            maxLateness=std::max(maxLateness,std::chrono::duration<double,std::milli>(now-next).count());
            if(maxLateness>1000)throw std::runtime_error("Corpus real-time callbacks paused; this run is invalid");
            const auto count=std::min<std::size_t>(320,pcm.size()-cursor);
            const auto host=lvt::hostclock::now();if(!captureOrigin)captureOrigin=host;
            silence.fill(0);if(count)std::copy_n(pcm.data()+cursor,count,silence.data());
            core.capture(silence.data(),silence.size(),16000,host);
            cursor+=count;if(count)ended=now;
            core.render(output.data(),output.size(),48000,host);
            lvt::ConversationEvent event;
            while(core.popConversationEvent(event)) {
                lastEvent=now;const auto& r=event.result;
                completion.observe(r.identity.segment,event.state);
                if(r.identity.timing.windowEndFrame && r.identity.timing.windowEndFrame<=firstFrame)
                    errors.push_back("event-from-previous-recording");
                if(event.state=="translated") {
                    translated.insert(r.identity.segment);
                    if(!original.empty())original+=' ';original+=r.original;
                    if(!translation.empty())translation+=' ';translation+=r.translated;
                    if(r.identity.speaker)speakers.insert(r.identity.speaker);else ++unknown;
                    if(r.sourceLanguage!=language)errors.push_back("wrong-source-language");
                }
                if(event.state=="played" && !played.insert(r.identity.segment).second)errors.push_back("duplicate-playback");
                if(event.state=="audio-onset")++onsets;
                if(event.state=="error" || event.state=="unavailable" || event.state=="dropped" || event.state=="audio-skipped" || event.state=="revision-conflict" || event.state=="revision-preserved")errors.push_back(event.state);
                std::cout<<nlohmann::json{{"recording",filename},{"state",event.state},{"session",r.identity.session},
                    {"segment",r.identity.segment},{"sourceSegment",r.identity.sourceSegment},{"speaker",r.identity.speaker},
                    {"firstFrame",r.identity.firstFrame},
                    {"language",r.sourceLanguage},{"original",r.original},{"translation",r.translated},
                    {"timing",lvt::test::timelineJSON(event)}}.dump()<<std::endl;
            }
            const auto s=core.snapshot();peak=std::max(peak,lvt::MemoryBudget::physicalFootprint());
            if(peak>=(8ULL<<30))throw std::runtime_error("Corpus exceeded the process memory budget");
            if(s.asr.state==3 || s.conversation.translationState==3 || s.conversation.ttsState==3)throw std::runtime_error("Corpus worker failed");
            const bool idleNow=cursor==pcm.size() && now-ended>std::chrono::seconds(1) &&
                !s.vad.speaking && s.depth<=2 && s.vad.depth<=2 && !s.vad.segmentsQueue.currentDepth &&
                s.asr.processed==s.vad.segments && !s.asr.resultsQueue.currentDepth &&
                s.conversation.coordinatedUtterances==s.asr.processed && !s.conversation.pendingEvents &&
                !s.conversation.translationQueue.currentDepth && !s.conversation.ttsQueue.currentDepth &&
                !s.playbackDepth && s.conversation.playbackSeconds==0 && completion.idle() && (played==translated || !errors.empty());
            // Rendering the last sample and receiving its worker-delivered end
            // event are different instants. Track accepted work through its
            // terminal event as well as requiring sustained idle. A slow active
            // translation can leave both queues and played/translated sets empty.
            // The coordinator acknowledges each final ASR marker after emitting
            // its preceding events. Also require its event queue to be drained,
            // including events published after this loop last consumed them.
            // Capture continues throughout; its live ingress queues need not be
            // persistently empty immediately after publishing each 20 ms block.
            if(idleNow && !idle)idleSince=now;
            idle=idleNow;
            if(idle && now-idleSince>std::chrono::milliseconds(500) && now-lastEvent>std::chrono::milliseconds(500)) { drained=true;break; }
            next+=std::chrono::milliseconds(20);std::this_thread::sleep_until(next);
        }
        if(!drained)errors.push_back("drain-timeout");
        if(translated.empty())errors.push_back("no-translated-speech");
        if(played!=translated || onsets!=played.size())errors.push_back("incomplete-playback");
        if(speakers.size()>1)errors.push_back("multiple-identities-within-one-recording");
        const auto s=core.snapshot();
        if(s.conversation.revisionConflicts > before.conversation.revisionConflicts)errors.push_back("confirmed-text-revised");
        if(s.dropped>before.dropped || s.vad.dropped>before.vad.dropped || s.playbackDropped>before.playbackDropped ||
            s.conversation.eventDrops>before.conversation.eventDrops || s.asr.resultsQueue.droppedItems>before.asr.resultsQueue.droppedItems)errors.push_back("queue-loss");
        ++recordings;failed+=!errors.empty();
        std::cout<<nlohmann::json{{"recordingResult",true},{"recording",filename},{"sha256",record.at("sha256")},
            {"language",language},{"split",split},{"captureOrigin",captureOrigin},{"captureFirstFrame",firstFrame},{"samples",pcm.size()},
            {"reference",record.at("reference")},{"translationReference",record.at("parallelTranslationReference")},
            {"original",original},{"translation",translation},{"speakers",speakers},{"unknownUnits",unknown},
            {"translatedUnits",translated.size()},{"playedUnits",played.size()},{"errors",errors},
            {"pendingUnits",completion.pending()},
            {"coordinatedUtterances",s.conversation.coordinatedUtterances},{"pendingEvents",s.conversation.pendingEvents},
            {"maxCallbackLatenessMs",maxLateness},{"acousticAnnotation","PENDING"},{"meaningReview","PENDING"}}.dump()<<std::endl;
        if(!drained)throw std::runtime_error("Corpus could not drain a recording; later recordings were not run");
        if(std::find(errors.begin(),errors.end(),"event-from-previous-recording")!=errors.end())
            throw std::runtime_error("Corpus attribution failed; later recordings were not run");
    }
    core.requestStop();core.render(output.data(),output.size(),48000);core.stop();
    if(std::any_of(output.begin(),output.end(),[](float sample){return sample!=0;}))throw std::runtime_error("Corpus STOP did not silence playback");
    if(recordings!=static_cast<unsigned>(limit))throw std::runtime_error("Corpus range did not contain all requested recordings");
    std::cout<<nlohmann::json{{"result",failed?"FAIL":"PASS"},{"recordings",recordings},{"failedRecordings",failed},
        {"peakFootprintBytes",peak},{"startIndex",startIndex},{"streaming",lvt::test::streamingJSON(policy)},
        {"scope","Isolated production route with human recordings; functional results are separate from acoustic latency and meaning acceptance"}}.dump()<<std::endl;
    return failed?1:0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }

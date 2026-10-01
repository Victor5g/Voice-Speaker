#include "../../engine/asr/SpeechEvidence.hpp"
#include "../../engine/asr/ASRContext.hpp"
#include "../../engine/speakers/SpeakerTracker.hpp"
#include <iostream>
#include <limits>
#include <vector>

int main() { try {
    unsigned passed = 0;
    auto check = [&](bool value, const char* message) { if (!value) throw std::runtime_error(message); ++passed; };
    std::vector<float> pcm(16000, 0);
    check(lvt::SpeechEvidence::analyze(pcm).silent, "Digital silence has no acoustic speech evidence");
    std::fill(pcm.begin(), pcm.end(), .2f);
    check(lvt::SpeechEvidence::analyze(pcm).silent, "Constant DC is not speech");
    std::fill(pcm.begin(), pcm.end(), 0);
    for (std::size_t i = 8000; i < 9600; ++i) pcm[i] = (i % 2 ? .001f : -.001f);
    auto shortQuiet = lvt::SpeechEvidence::analyze(pcm);
    check(!shortQuiet.silent, "Quiet 100 ms signal survives digital-silence guard despite surrounding silence");
    check(!shortQuiet.rejectsWeakNoSpeech(.99, -.1, 1, .6, -1), "One confident text token is not rejected merely for quietness or length");
    check(!shortQuiet.rejectsWeakNoSpeech(.1, -2, 1, .6, -1), "Weak text without model no-speech evidence is retained");
    check(shortQuiet.rejectsWeakNoSpeech(.8, -1.2, 1, .6, -1), "Weak lexical tokens plus measured no-speech evidence are rejected");
    check(!shortQuiet.rejectsWeakNoSpeech(.99, -1.2, 0, .6, -1), "Absent token evidence is not a fabricated confidence score");
    check(!shortQuiet.rejectsWeakNoSpeech(std::numeric_limits<double>::quiet_NaN(), -1.2, 1, .6, -1), "Invalid probability cannot reject speech");
    auto loud = shortQuiet; loud.peakBlockRMS = .02;
    check(loud.rejectsWeakNoSpeech(.8, -1.2, 1, .6, -1), "Loud non-speech uses the same lexical guard as quiet input");
    check(!shortQuiet.rejectsWeakNoSpeech(.6, -2, 1, .6, -1), "No-speech boundary follows the backend strict comparison");
    check(!shortQuiet.rejectsWeakNoSpeech(.99, -1, 1, .6, -1), "Text confidence boundary follows the backend strict comparison");
    check(!shortQuiet.rejectsWeakNoSpeech(.99, -.8, 1, .6, -1), "Strong lexical probability remains accepted despite no-speech evidence");
    check(!shortQuiet.rejectsWeakNoSpeech(.8, -1.2, 1, .9, -1), "Caller-configured backend threshold is honored without hardcoded override");
    check(!shortQuiet.rejectsWeakNoSpeech(.8, -1.2, 1, .6, std::numeric_limits<double>::quiet_NaN()), "Invalid configured threshold cannot reject speech");
    check(!shortQuiet.rejectsWeakNoSpeech(1.01, -2, 1, .6, -1), "Out-of-range score cannot be treated as measured probability");
    pcm[0] = std::numeric_limits<float>::quiet_NaN();
    bool invalid = false; try { (void)lvt::SpeechEvidence::analyze(pcm); } catch (...) { invalid = true; }
    check(invalid, "Non-finite PCM is rejected explicitly");

    lvt::ASRContext initial, firstWindow, secondWindow, ended, newTurn, tailOnly;
    firstWindow.retainFinal(initial,1,"pt","A primeira parte",0,156800,156800,lvt::EndpointReason::windowLimit);
    check(firstWindow.prompt(1,"pt",156800)=="A primeira parte", "9.8 second artificial cut retains contiguous sentence context");
    check(firstWindow.prompt(1,"pt",156799)=="A primeira parte", "One resampled sample rounding difference is tolerated");
    check(firstWindow.prompt(1,"pt",156802).empty(), "A gap is not treated as continuous speech");
    check(firstWindow.prompt(1,"pt",160000).empty(), "200 ms new-turn gap never reuses the prior prompt");
    check(firstWindow.prompt(1,"en",156800).empty(), "Language switches never use another language's context");
    check(firstWindow.prompt(2,"pt",156800).empty(), "Another participant cannot inherit the prompt");
    secondWindow.retainFinal(firstWindow,1,"pt","e a segunda",156800,313600,313600,lvt::EndpointReason::windowLimit);
    check(secondWindow.prompt(1,"pt",313600)=="A primeira parte e a segunda", "Only genuinely contiguous artificial cuts accumulate text");
    ended.retainFinal(secondWindow,1,"pt","terminou",313600,320000,320000,lvt::EndpointReason::silence);
    check(ended.prompt(1,"pt",320000).empty(), "A silence final clears history even if next PCM is adjacent");
    newTurn.retainFinal(firstWindow,1,"pt","Novo assunto",180000,336800,336800,lvt::EndpointReason::windowLimit);
    check(newTurn.prompt(1,"pt",336800)=="Novo assunto", "A later artificial cut starts fresh instead of accumulating distinct turns");
    tailOnly.retainFinal(firstWindow,1,"pt","old speaker",156800,180000,200000,lvt::EndpointReason::windowLimit);
    check(tailOnly.prompt(1,"pt",180000).empty(), "A diarized unit before the final PCM boundary cannot provide next-window context");
    tailOnly.retainFinal(initial,0,"pt","unknown",0,156800,156800,lvt::EndpointReason::windowLimit);
    check(tailOnly.prompt(0,"pt",156800).empty(), "Unknown identities never own a sentence context");
    tailOnly.retainFinal(initial,1,"pt","overlap",0,156800,156800,lvt::EndpointReason::windowLimit,true);
    check(tailOnly.prompt(1,"pt",156800).empty(), "Unresolved overlapping speech cannot seed a prompt");

    lvt::SpeakerTracker tracker;
    const std::vector<float> a{1, 0, 0}, b{0, 1, 0}, outlier{0, 0, 1};
    for (unsigned i = 0; i < 10; ++i)
        check(tracker.identify(a, i * 16000, true, false, 0, true, false, (i + 1) * 16000) == 0,
            "Preview never creates an identity even with changing frame indices");
    check(tracker.count() == 0 && tracker.provisionalCount() == 0, "Previews do not enroll provisional people");
    check(tracker.identify(a, 0, true, false, 0, true, true, 32000) == 0, "First final remains provisional");
    check(tracker.identify(a, 0, true, false, 0, true, true, 32000) == 0, "Duplicate final is not independent evidence");
    check(tracker.identify(a, 16000, true, false, 0, true, true, 48000) == 0, "Overlapping final window cannot confirm a new identity");
    check(tracker.identify(a, 32000, true, false, 0, true, true, 64000) == 1, "Second independent final confirms participant one");
    check(tracker.identify(outlier, 64000, true, false, 0, true, true, 96000) == 0 && tracker.count() == 1,
        "A single novel outlier never changes the confirmed participant count");
    const std::vector<float> scaled{5, 0, 0};
    check(tracker.identify(scaled, 96000, true) == 1, "Embedding scale changes retain the normalized identity");
    check(tracker.identify(b, 128000, true, true) == 0, "Overlap cannot enroll a new permanent person");
    check(tracker.identify(b, 128000, true, false, 0, true, true, 160000) == 0, "New clean speaker first final is provisional");
    check(tracker.identify(b, 160000, true, false, 0, true, true, 192000) == 2, "Independent confirmation establishes second participant");
    check(tracker.identify(a, 16000ULL * 3600, true) == 1, "Returning participant retains ID after inactivity");
    check(tracker.identify(b, 16000ULL * 3601, true, true, 1) == 2, "Overlap preserves established distinct identities");
    tracker.clear();
    check(tracker.count() == 0 && tracker.provisionalCount() == 0, "Session reset clears confirmed and provisional identities");
    check(tracker.identify(a, 0, true, false, 0, true, false, 32000, true) == 0,
        "Corroborated bootstrap cannot bypass the final-evidence requirement");
    check(tracker.identify(a, 0, true, true, 0, true, true, 32000, true) == 0,
        "Corroborated bootstrap cannot enroll separated overlap");
    check(tracker.identify(a, 0, true, false, 0, true, true, 32000, true) == 1,
        "First person can bootstrap from independently corroborated final speech");
    check(tracker.identify(b, 32000, true, false, 0, true, true, 64000, true) == 0,
        "Bootstrap exemption never applies to a second person even when caller corroborates halves");
    check(tracker.identify(b, 64000, true, false, 0, true, true, 96000, true) == 2,
        "Second person still requires independent final confirmation");
    std::cout << "RESULT " << passed << " passed, 0 failed\n";
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }

#include "../../engine/asr/BoundaryEvidence.hpp"
#include "../../engine/conversation/SegmentStabilityManager.hpp"
#include <iostream>
#include <limits>
int main() { try {
    unsigned passed = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) throw std::runtime_error(name); ++passed; };
    lvt::BoundaryEvidence evidence;
    evidence.token(" Reuni", .9f); evidence.token("ão", .95f); evidence.token(".", .25f); evidence.token("\nAgora", .8f);
    check(evidence.aligned("Reunião. Agora") == std::vector<float>({.25f, 0}), "BPE fragments preserve word alignment and punctuation evidence");
    lvt::BoundaryEvidence mismatch; mismatch.token(" word.", .9f);
    check(mismatch.aligned("different.").empty(), "Alignment mismatch fails closed");
    lvt::BoundaryEvidence invalid; invalid.token("word.", std::numeric_limits<float>::quiet_NaN());
    check(invalid.aligned("word.") == std::vector<float>({0}), "Invalid token probability cannot confirm a boundary");
    lvt::BoundaryEvidence decimal; decimal.token("12.", .1f); decimal.token("50", .9f);
    check(decimal.aligned("12.50") == std::vector<float>({0}), "Internal decimal point is not a closing boundary");
    lvt::BoundaryEvidence bounded; for (int i=0; i<513; ++i) bounded.token("word ", .9f);
    check(bounded.aligned("word").empty(), "Oversized evidence is bounded and unavailable");
    auto policy = lvt::StreamingPolicy::production(); policy.minimumBoundaryProbability = .5f;
    const std::string text = "Vamos começar a reunir. Eu preciso de tempo.";
    std::vector<float> low(8, .9f); low[3] = .22f;
    auto high = low; high[3] = .9f;
    lvt::SegmentStabilityManager held(policy);
    for (int i=1; i<=5; ++i) check(held.observe(text,false,i*16000,true,low).confirmed.empty(), "Repeated weak boundary stays provisional");
    check(held.observe("Vamos começar a reunião. Eu preciso de tempo.",true,96000,true).confirmed == "Vamos começar a reunião. Eu preciso de tempo.", "Final correction retains the complete source");
    lvt::SegmentStabilityManager fast(policy);
    fast.observe(text,false,16000,true,high);
    check(fast.observe(text,false,28000,true,high).confirmed == "Vamos começar a reunir.", "Strong boundary can commit at the existing cadence; no fixed added delay");
    check(fast.observe(text,true,32000,true,high).confirmed == "Eu preciso de tempo.", "Final emits suffix once");
    lvt::SegmentStabilityManager interrupted(policy);
    interrupted.observe(text,false,16000,true,high); interrupted.observe(text,false,28000,true,low);
    check(interrupted.observe(text,false,40000,true,high).confirmed.empty(), "Weak evidence resets consecutive boundary agreement");
    check(interrupted.observe(text,false,52000,true,high).confirmed == "Vamos começar a reunir.", "New consecutive strong evidence can recover");
    lvt::SegmentStabilityManager missing(policy);
    missing.observe(text,false,16000);
    check(missing.observe(text,false,32000).confirmed.empty(), "Missing confidence does not silently bypass experimental guard");
    lvt::SegmentStabilityManager punctuation(policy);
    punctuation.observe("Vamos começar a reunir, Eu preciso de tempo.",false,16000,true,high);
    check(punctuation.observe(text,false,32000,true,high).confirmed.empty(), "Comma agreement cannot establish a later period");
    lvt::SegmentStabilityManager later(policy);
    const std::string two = "Não autorizei. Eu confirmo recebimento. Vou verificar.";
    std::vector<float> mixed(7, .9f); mixed[1] = .2f;
    later.observe(two,false,16000,true,mixed);
    check(later.observe(two,false,32000,true,mixed).confirmed.empty(), "Later strong clause cannot bypass earlier uncertain boundary");
    policy.consecutiveBoundaryEvidence = false;
    lvt::SegmentStabilityManager current(policy);
    current.observe(text,false,16000,true,low);
    check(current.observe(text,false,28000,true,low).confirmed.empty(), "Current-only gate still rejects weak current punctuation");
    check(current.observe(text,false,40000,true,high).confirmed == "Vamos começar a reunir.", "Current-only gate can reuse prior lexical agreements without resetting their age");
    lvt::SegmentStabilityManager currentUnknown(policy);
    currentUnknown.observe(text,false,16000,true,high);
    check(currentUnknown.observe(text,false,28000,true).confirmed.empty(), "Current-only gate fails closed without aligned current evidence");
    lvt::SegmentStabilityManager currentPunctuation(policy);
    currentPunctuation.observe("Vamos começar a reunir, Eu preciso de tempo.",false,16000,true,high);
    check(currentPunctuation.observe(text,false,32000,true,high).confirmed.empty(), "Current-only gate still requires the same clause delimiter in successive hypotheses");
    policy.allowCommaToPeriodBoundary = true;
    policy.validate();
    const std::string comma = "Vamos começar a reunir, Eu preciso de tempo.";
    lvt::SegmentStabilityManager promoted(policy);
    promoted.observe(comma,false,16000,true,low);
    check(promoted.observe(text,false,28000,true,high).confirmed == "Vamos começar a reunir.", "Opt-in comma promotion uses fresh lexical agreement and strong current punctuation");
    check(promoted.observe(text,true,32000).confirmed == "Eu preciso de tempo.", "Comma promotion does not repeat the spoken clause");
    lvt::SegmentStabilityManager weakPromotion(policy);
    weakPromotion.observe(comma,false,16000,true,high);
    check(weakPromotion.observe(text,false,28000,true,low).confirmed.empty(), "Comma promotion cannot bypass current weak punctuation");
    lvt::SegmentStabilityManager changedWord(policy);
    changedWord.observe(comma,false,16000,true,high);
    check(changedWord.observe("Vamos começar a reunião. Eu preciso de tempo.",false,28000,true,high).confirmed.empty(), "Changed word still restarts lexical agreement");
    for (const auto& prefix : {"Vamos começar a reunir?", "Vamos começar a reunir...", "Vamos começar a reunir"}) {
        lvt::SegmentStabilityManager delimiter(policy);
        delimiter.observe(std::string(prefix) + " Eu preciso de tempo.",false,16000,true,high);
        check(delimiter.observe(text,false,28000,true,high).confirmed.empty(), "Only comma-to-period qualifies, not questions, ellipses or absent punctuation");
    }
    lvt::SegmentStabilityManager clock(policy);
    clock.observe(comma,false,16000,true,high);
    check(clock.observe(text,false,16000,true,high).confirmed.empty(), "Comma promotion requires new audio");
    check(clock.observe(text,false,20000,true,high).confirmed.empty(), "Comma promotion respects minimum stable audio duration");
    auto invalidPromotion = policy; invalidPromotion.consecutiveBoundaryEvidence = true;
    bool rejected = false; try { invalidPromotion.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Comma promotion cannot silently weaken consecutive-boundary mode");
    invalidPromotion = policy; invalidPromotion.minimumBoundaryProbability = 0;
    rejected = false; try { invalidPromotion.validate(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Comma promotion cannot be enabled without confidence evidence");
    std::cout << "RESULT " << passed << " passed, 0 failed\n";
} catch(const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; } }

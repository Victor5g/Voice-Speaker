#include "../../engine/conversation/SegmentStabilityManager.hpp"
#include <iostream>
#include <limits>
using namespace lvt;
static int passed;
static void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); ++passed; std::cout << "PASS " << text << '\n'; }
int main() { try {
    const StreamingPolicy candidate{1000,2,900,true}; candidate.validate();
    check(candidate.interval(450)==1000, "One lightly loaded route can use one-second partials");
    check(candidate.interval(900)==1500, "Shared route demand slows partials before saturation");
    check(candidate.interval(1500)==3000, "Heavy inference reduces speculative work instead of growing queues");
    check(candidate.interval(std::numeric_limits<double>::quiet_NaN())==3000, "Unknown capacity selects the conservative cadence");
    check(StreamingPolicy{}.interval(5000)==1500, "Control configuration remains fixed for reproducible comparisons");
    bool invalid=false; try { StreamingPolicy{1000,2,0,true}.validate(); } catch (const std::invalid_argument&) { invalid=true; }
    check(invalid, "Two agreements cannot bypass the minimum time safeguard");
    SegmentStabilityManager stable(candidate);
    const std::string clause="Não autorizei o pagamento. Vou conferir";
    check(stable.observe(clause,false,16000).confirmed.empty(), "First candidate hypothesis is never spoken immediately");
    check(stable.observe(clause,false,16000).confirmed.empty(), "Repeated decoding of identical audio adds no evidence");
    check(stable.observe(clause,false,15999).confirmed.empty(), "Older audio cannot advance confirmation");
    check(stable.observe(clause,false,24000).confirmed.empty(), "Two agreements only 500 ms apart remain provisional");
    check(stable.observe(clause,false,30400).confirmed=="Não autorizei o pagamento.", "Fresh audio and 900 ms of stable context confirm the clause");
    check(stable.observe(clause,true,30400).confirmed=="Vou conferir", "Final silence emits only the unspoken suffix with no new samples");
    check(stable.observe(clause,true,30400).confirmed.empty(), "Duplicate final result cannot create duplicate speech");
    SegmentStabilityManager absent(candidate);
    bool held=true; for (int i=0;i<5;++i) held &= absent.observe(clause,false).confirmed.empty();
    check(held, "Repeated hypotheses without audio timing cannot prove temporal stability");
    check(absent.observe(clause,true).confirmed==clause, "Final confirmation remains available without an audio clock");
    SegmentStabilityManager negation(candidate);
    negation.observe("Eu autorizei o pagamento. Vou conferir",false,16000);
    check(negation.observe(clause,false,32000).confirmed.empty(), "Changed negation resets evidence before speech is committed");
    check(negation.observe(clause,true,33000).confirmed==clause, "Final critical correction is preserved completely");
    SegmentStabilityManager edge(candidate);
    edge.observe("Vamos começar a reunir.",false,16000);
    edge.observe("Vamos começar a reunir. Eu preciso",false,32000);
    check(edge.observe("Vamos começar a reunir. Eu preciso",false,40000).confirmed.empty(), "A boundary guess without following context does not age into confirmation");
    check(edge.observe("Vamos começar a reunião. Eu preciso de tempo.",true,41000).confirmed=="Vamos começar a reunião. Eu preciso de tempo.", "Late correction of an unconfirmed boundary is retained");
    SegmentStabilityManager incomplete(candidate);
    incomplete.observe("I need twenty three",false,16000);
    check(incomplete.observe("I need twenty three",false,48000).confirmed.empty(), "Elapsed time does not turn an unfinished number into a clause");
    SegmentStabilityManager ellipsis(candidate);
    ellipsis.observe("I was thinking... We should",false,16000);
    check(ellipsis.observe("I was thinking... We should",false,48000).confirmed.empty(), "Ellipsis remains unfinished even with repeated following words");
    SegmentStabilityManager unidentified(candidate);
    unidentified.observe(clause,false,16000,false);
    check(unidentified.observe(clause,false,32000,false).confirmed.empty(), "Stable text waits while participant identity is provisional");
    check(unidentified.observe(clause,false,48000,true).confirmed=="Não autorizei o pagamento.", "Confirmed identity releases the accumulated stable clause exactly once");
    check(unidentified.observe(clause,true,49000,true).confirmed=="Vou conferir", "Identity waiting does not duplicate the final continuation");
    std::cout << "RESULT " << passed << " passed, 0 failed\n";
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }

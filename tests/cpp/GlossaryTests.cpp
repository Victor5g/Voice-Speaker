#include "../../engine/translation/Glossary.hpp"
#include <fstream>
#include <iostream>

int main() {
    std::ifstream stream("app/Resources/glossary-defaults.json");
    nlohmann::json seed; stream >> seed;
    unsigned count = 0;
    auto check = [&](bool value, const char* name) {
        if (!value) throw std::runtime_error(name);
        ++count;
    };
    auto glossary = lvt::Glossary::fromJSON(seed.dump());
    check(seed["entries"].size() == 1477, "seed count");
    for (const auto& term : {"unboxing", "feedback", "spoiler"})
        check(glossary.matches(std::string("This is ") + term + ".", "en-pt").size() == 1, "mandatory loanword");
    check(glossary.matches("The unboxings have SPOILERS.", "en-pt").size() == 2, "plural case");
    check(glossary.matches("The unboxings have SPOILERS.", "pt-en").empty(), "direction isolation");
    check(glossary.matches("A feedbacker reviews spoiling.", "en-pt").empty(), "word boundaries");
    check(glossary.matches("My coach helped with my career.", "en-pt").size() == 1, "coach profession");
    for (const auto* phrase : {"My coaches helped me.", "Your coaches helped me.", "OUR COACHES helped me."}) {
        auto plural = glossary.matches(phrase, "en-pt");
        check(plural.size() == 1 && (plural.front().original == "coaches" || plural.front().original == "COACHES"), "possessive coach plural preserves original form");
    }
    check(glossary.matches("Our coaches transport passengers.", "en-pt").empty(), "plural coach transport exclusion overrides possessive cue");
    check(glossary.matches("We took a coach to the station.", "en-pt").empty(), "coach bus");
    check(glossary.matches("Please call me tomorrow.", "en-pt").empty(), "call verb");
    check(glossary.matches("I live in Brazil. Push the door. The cloud is white. Cover the pan.", "en-pt").empty(), "common verbs and nouns");
    check(glossary.matches("“feedback” —unboxing—", "en-pt").size() == 2, "Unicode boundaries");
    check(glossary.matches("POKÉMON", "en-pt").size() == 1, "Unicode case");
    auto mixed = glossary.matches("My coach took a coach to the station.", "en-pt");
    check(mixed.size() == 1 && mixed.front().begin == 3, "mixed coach meanings");
    auto mixedPlural = glossary.matches("My coaches took a coach to the station.", "en-pt");
    check(mixedPlural.size() == 1 && mixedPlural.front().begin == 3 && mixedPlural.front().original == "coaches", "plural professional and singular bus keep occurrence-specific context");
    mixedPlural = glossary.matches("We took a coach to meet my coaches.", "en-pt");
    check(mixedPlural.size() == 1 && mixedPlural.front().original == "coaches", "singular transport exclusion does not leak to plural professionals");
    check(glossary.matches("Join the scheduled call.", "en-pt").size() == 1, "call meeting");
    check(glossary.matches("Spring arrives after winter.", "en-pt").empty(), "Spring season");
    auto java = glossary.matches("Our Java application uses Spring.", "en-pt");
    check(java.size() == 2, "Spring framework");
    auto full = glossary.matches("This Spring Boot application is ready.", "en-pt");
    check(full.size() == 1 && full[0].original == "Spring Boot", "longest phrase");
    check(glossary.matches("Install plug-ins for the front-end.", "en-pt").size() == 2, "hyphen forms");
    auto disabled = seed; disabled["enabled"] = false;
    check(lvt::Glossary::fromJSON(disabled.dump()).matches("unboxing", "en-pt").empty(), "global disable");
    disabled = seed;
    for (auto& category : disabled["categories"]) category["enabled"] = false;
    check(lvt::Glossary::fromJSON(disabled.dump()).matches("unboxing", "en-pt").empty(), "category disable");
    for (auto& entry : disabled["entries"]) if (entry["term"] == "unboxing") entry["categories"] = {"media", "technology"};
    disabled["categories"][0]["enabled"] = true;
    check(lvt::Glossary::fromJSON(disabled.dump()).matches("unboxing", "en-pt").size() == 1, "multi-category enable");
    auto invalid = seed; invalid["entries"][0]["directions"] = {"en-fr"};
    bool rejected = false;
    try { lvt::Glossary::fromJSON(invalid.dump()); } catch (const std::exception&) { rejected = true; }
    check(rejected, "unsupported direction rejected");
    const std::vector<lvt::GlossaryMatch> matches{{0, 8, "feedback", "test"}};
    check(lvt::Glossary::restore("opinião clara", matches, {{0, 8}, {9, 14}}, {{0, 8}, {9, 14}}, {{.9f, .1f}, {.1f, .9f}}) == "feedback clara", "aligned replacement UTF8");
    rejected = false;
    try { lvt::Glossary::restore("sem alinhamento", matches, {{0, 0}}, {{0, 3}}, {{1}}); } catch (const std::exception&) { rejected = true; }
    check(rejected, "unavailable alignment rejected");
    for (const auto& dangerous : {std::string("não"), std::string("never"), std::string("200")}) {
        rejected = false;
        try { lvt::Glossary::restore(dangerous, matches, {{0, 8}}, {{0, dangerous.size()}}, {{1}}); } catch (const std::exception&) { rejected = true; }
        check(rejected, "negation and numeric target protections");
    }
    rejected = false;
    try { lvt::Glossary::restore("opinião", matches, {{0, 8}, {9, 10}, {11, 12}}, {{0, 8}}, {{.34f, .33f, .33f}}); } catch (const std::exception&) { rejected = true; }
    check(rejected, "weak attention is not confidence");
    check(lvt::Glossary::restore("feedback", matches, {}, {}, {}) == "feedback", "already preserved bypasses alignment");
    check(lvt::Glossary::restore("chamada", {{0, 4, "call", "call"}}, {{0, 0}, {0, 4}, {5, 8}, {0, 0}}, {{0, 7}}, {{.17f, .54f, .13f, .16f}}) == "call", "structural tokens excluded from lexical mass");
    std::cout << "PASS " << count << " glossary rules/alignment checks; no model inference\n";
}

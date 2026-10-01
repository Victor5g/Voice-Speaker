#include "../../engine/translation/TranslationEngine.hpp"
#include "../../engine/translation/Glossary.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    try {
        const std::filesystem::path root = argc > 1 ? argv[1] : "models";
        nlohmann::json catalog, seed;
        std::ifstream(root / "manifest.json") >> catalog;
        std::ifstream("app/Resources/glossary-defaults.json") >> seed;
        const auto entry = std::find_if(catalog["models"].begin(), catalog["models"].end(), [](const auto& m) {
            return m.value("type", "") == "translation" && m.value("direction", "") == "en-pt" && m.value("quantization", "") == "int8";
        });
        if (entry == catalog["models"].end()) throw std::runtime_error("Missing en-pt model");
        lvt::TranslationEngine engine(lvt::ModelBundle::fromJSON(root, *entry), "en-pt", "int8");
        const std::vector<std::pair<std::string, std::vector<std::string>>> cases{
            {"Today we are doing an unboxing of this product.", {"unboxing"}},
            {"Thank you for the feedback.", {"feedback"}},
            {"My coach helped me with my career.", {"coach"}},
            {"My coaches helped me.", {"coaches"}},
            {"Please do not tell me any spoilers.", {"spoilers"}},
            {"We took a coach to the station.", {}},
            {"Please call me tomorrow.", {}},
            {"Spring arrives after winter.", {}},
            {"The Java application uses the Spring framework.", {"Java", "Spring", "framework"}},
            {"This Spring Boot application is ready.", {"Spring Boot"}},
            {"The new plug-ins work with our front-end.", {"plug-ins", "front-end"}},
            {"Do not share the API token.", {"API", "token"}},
            {"Join the scheduled call.", {"call"}},
            {"Maria did not pay 200 dollars for the unboxing.", {"unboxing"}},
            {"Thanks.", {}},
            {"I live in Brazil.", {}},
            {"Push the door.", {}},
            {"The cloud is white.", {}},
            {"Please cover the pan.", {}}
        };
        unsigned failures = 0;
        for (const auto& [source, required] : cases) {
            engine.setGlossaryJSON("");
            const auto control = engine.translate(source);
            engine.setGlossaryJSON(seed.dump());
            try {
                const auto translated = engine.translate(source);
                bool pass = true;
                for (const auto& term : required) pass &= translated.text.find(term) != std::string::npos;
                if (required.empty()) pass = translated.text == control.text;
                const bool controlAlreadyPreserves = std::all_of(required.begin(), required.end(), [&](const auto& term) { return control.text.find(term) != std::string::npos; });
                if (controlAlreadyPreserves) pass &= translated.text == control.text;
                auto digits = [](const std::string& text) { std::string result; for (unsigned char c : text) if (std::isdigit(c)) result += static_cast<char>(c); return result; };
                const bool numbersUnchanged = digits(translated.text) == digits(control.text);
                const bool negationUnchanged = (translated.text.find("não") != std::string::npos) == (control.text.find("não") != std::string::npos);
                const bool nameUnchanged = source.find("Maria") == std::string::npos || translated.text.find("Maria") != std::string::npos;
                pass &= numbersUnchanged && negationUnchanged && nameUnchanged;
                if (!pass) ++failures;
                std::cout << nlohmann::json{{"source", source}, {"control", control.text}, {"translation", translated.text},
                    {"required", required}, {"pass", pass}, {"glossaryMs", translated.latencyMs}, {"controlMs", control.latencyMs},
                    {"numbersUnchanged", numbersUnchanged}, {"negationUnchanged", negationUnchanged}, {"nameUnchanged", nameUnchanged},
                    {"scope", "functional native translation; not E2E or pronunciation acceptance"}}.dump() << std::endl;
            } catch (const std::exception& error) {
                ++failures;
                std::cout << nlohmann::json{{"source", source}, {"control", control.text}, {"pass", false}, {"error", error.what()}}.dump() << std::endl;
            }
        }
        return failures ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}

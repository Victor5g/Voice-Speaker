#include "../../engine/translation/TranslationEngine.hpp"
#include <iostream>
#include <sys/resource.h>

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Usage: translation-benchmark MODEL_ROOT DIRECTION PRECISION CASES_JSON");
        const std::filesystem::path root(argv[1]);
        nlohmann::json catalog, cases;
        std::ifstream(root / "manifest.json") >> catalog;
        std::ifstream(argv[4]) >> cases;
        auto found = std::find_if(catalog["models"].begin(), catalog["models"].end(), [&](const auto& m) {
            return m.value("type", "") == "translation" && m.value("direction", "") == argv[2] && m.value("quantization", "") == argv[3];
        });
        if (found == catalog["models"].end()) throw std::runtime_error("Translation model missing from manifest");
        lvt::TranslationEngine engine(lvt::ModelBundle::fromJSON(root, *found), argv[2], argv[3]);
        std::stop_source stopped; stopped.request_stop();
        if (!engine.translate("must never be translated", stopped.get_token()).text.empty()) throw std::runtime_error("Cancellation failed");
        engine.translate(std::string(argv[2]) == "pt-en" ? "Bom dia." : "Good morning.");
        for (const auto& test : cases.at(argv[2])) {
            auto result = engine.translate(test.at("source"));
            rusage usage{}; getrusage(RUSAGE_SELF, &usage);
            std::cout << nlohmann::json{{"id", test.at("id")}, {"source", test.at("source")}, {"reference", test.at("reference")},
                {"translation", result.text}, {"direction", argv[2]}, {"precision", argv[3]}, {"latencyMs", result.latencyMs},
                {"peakRSSBytes", usage.ru_maxrss}}.dump() << '\n';
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

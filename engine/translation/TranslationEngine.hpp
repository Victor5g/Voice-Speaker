#pragma once
#include "../common/ModelAssets.hpp"
#include <memory>
#include <stop_token>

namespace lvt {
struct TranslatedText { std::string text; double latencyMs{}; };
// Owned by the translation worker. Both directions use CPU; no network runtime.
class TranslationEngine {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    TranslationEngine(const ModelBundle& model, std::string direction, std::string precision);
    ~TranslationEngine();
    void releaseCaches();
    void setGlossaryJSON(const std::string& json);
    TranslatedText translate(const std::string& text, std::stop_token stop = {});
};
}

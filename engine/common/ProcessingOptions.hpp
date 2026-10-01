#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace lvt {
// A value snapshot per route. Configure while stopped; workers capture by value.
struct ProcessingOptions {
    std::string sourceLanguage, targetLanguage, glossaryJSON;
    bool identifySpeakers{true}, speechOutput{true};
    unsigned voiceIndex{0}, endpointSilenceMs{300};
    double maxQueuedSpeechSeconds{6};
    void validate() const {
        auto language=[](const std::string& value){return value.empty()||value=="pt"||value=="en";};
        if(!language(sourceLanguage)||!language(targetLanguage)||
           (!sourceLanguage.empty()&&sourceLanguage==targetLanguage)||voiceIndex>2||
           endpointSilenceMs<200||endpointSilenceMs>1000||
           !std::isfinite(maxQueuedSpeechSeconds)||maxQueuedSpeechSeconds<3||maxQueuedSpeechSeconds>15||glossaryJSON.size()>4*1024*1024)
            throw std::invalid_argument("Configuração inválida: use PT-BR/EN, voz compatível e pausa entre 200 e 1000 ms.");
    }
    void validateForSourceLanguage(const std::string& legacySource) const {
        validate();
        if ((!legacySource.empty() && legacySource != "pt" && legacySource != "en") ||
            (!legacySource.empty() && !sourceLanguage.empty() && legacySource != sourceLanguage))
            throw std::invalid_argument("O idioma de origem da rota contradiz a configuração de processamento.");
        const auto& effectiveSource = sourceLanguage.empty() ? legacySource : sourceLanguage;
        if (!effectiveSource.empty() && effectiveSource == targetLanguage)
            throw std::invalid_argument("A origem e o destino da tradução devem ser diferentes.");
    }
    static ProcessingOptions fromJSON(const std::string& text) {
        if(text.size()>5*1024*1024)throw std::invalid_argument("Configuração muito grande.");
        const auto j=nlohmann::json::parse(text);
        if(!j.is_object())throw std::invalid_argument("Configuração deve ser um objeto.");
        // Validate the JSON representation before narrowing; get<unsigned>() alone
        // silently truncates fractions and wraps values outside unsigned's range.
        auto integer = [&](const char* key, unsigned fallback, unsigned minimum, unsigned maximum) {
            const auto it = j.find(key);
            if (it == j.end()) return fallback;
            if (!it->is_number_integer())
                throw std::invalid_argument(std::string(key) + " deve ser um inteiro.");
            if (it->is_number_unsigned()) {
                const auto value = it->get<std::uint64_t>();
                if (value >= minimum && value <= maximum) return static_cast<unsigned>(value);
            } else {
                const auto value = it->get<std::int64_t>();
                if (value >= minimum && value <= maximum) return static_cast<unsigned>(value);
            }
            throw std::invalid_argument(std::string(key) + " está fora da faixa permitida.");
        };
        ProcessingOptions p;
        p.sourceLanguage=j.value("sourceLanguage",""); p.targetLanguage=j.value("targetLanguage","");
        p.identifySpeakers=j.value("identifySpeakers",true); p.speechOutput=j.value("speechOutput",true);
        p.voiceIndex=integer("voiceIndex",0,0,2); p.endpointSilenceMs=integer("endpointSilenceMs",300,200,1000);
        p.maxQueuedSpeechSeconds=j.value("maxQueuedSpeechSeconds",6.0); p.glossaryJSON=j.value("glossaryJSON","");
        p.validate();return p;
    }
};
}

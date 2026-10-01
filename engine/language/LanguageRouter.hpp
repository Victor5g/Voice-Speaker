#pragma once
#include <string_view>

namespace lvt {
enum class Language { unsupported, portuguese, english };
struct TranslationDirection { Language source{}, target{}; bool eligible{}; };
class LanguageRouter {
public:
    static Language parse(std::string_view raw) {
        if (raw == "pt" || raw == "pt-BR") return Language::portuguese;
        if (raw == "en") return Language::english;
        return Language::unsupported;
    }
    static std::string_view code(Language language) {
        switch (language) {
            case Language::portuguese: return "pt-BR";
            case Language::english: return "en";
            default: return "unsupported";
        }
    }
    static TranslationDirection route(Language source, bool stable) {
        if (!stable || source == Language::unsupported) return {};
        return {source, source == Language::portuguese ? Language::english : Language::portuguese, true};
    }
};
}

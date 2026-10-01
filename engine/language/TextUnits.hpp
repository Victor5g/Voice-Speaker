#pragma once
#include <cctype>
#include <string>
#include <vector>
namespace lvt {
// UTF-8 content stays intact; boundaries use ASCII punctuation and whitespace.
// Decimal points and common title abbreviations do not end a translation unit.
inline std::vector<std::string> sentenceUnits(const std::string& text) {
    std::vector<std::string> units;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '.' && c != '!' && c != '?' && c != '\n') continue;
        if (i + 1 < text.size() && !std::isspace(static_cast<unsigned char>(text[i + 1]))) continue;
        auto lastSpace = text.find_last_of(" \t\n", i ? i - 1 : 0);
        const auto word = text.substr(lastSpace == std::string::npos ? 0 : lastSpace + 1, i - (lastSpace == std::string::npos ? 0 : lastSpace + 1));
        if (c == '.' && (word == "Dr" || word == "Dra" || word == "Mr" || word == "Mrs" || word == "Ms" || word == "Sr" || word == "Sra" || word.size() == 1)) continue;
        if (i >= start) units.push_back(text.substr(start, i - start + 1));
        start = i + 1;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
        i = start ? start - 1 : 0;
    }
    if (start < text.size()) units.push_back(text.substr(start));
    return units;
}
}

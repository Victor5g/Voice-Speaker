#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace lvt {
// Decoder probabilities are not calibrated semantic confidence. Preserve only
// the probability of the token that supplied each word's closing punctuation.
class BoundaryEvidence {
    struct Word { std::string text; float probability{}; };
    std::vector<Word> words_;
    Word current_;
    bool overflow_{};
    void flush() {
        if (current_.text.empty()) return;
        if (words_.size() >= 512) { overflow_ = true; current_ = {}; return; }
        words_.push_back(std::move(current_)); current_ = {};
    }
public:
    void token(const std::string& text, float probability) {
        if (overflow_) return;
        if (!std::isfinite(probability) || probability < 0 || probability > 1) probability = 0;
        for (unsigned char c : text) {
            if (std::isspace(c)) flush();
            else {
                if (current_.text.size() >= 4096) { overflow_ = true; return; }
                current_.text += char(c);
                current_.probability = c == '.' || c == '!' || c == '?' || c == ';' ? probability : 0;
            }
            if (overflow_) return;
        }
    }
    std::vector<float> aligned(const std::string& transcript) {
        flush();
        if (overflow_) return {};
        std::istringstream stream(transcript); std::string word;
        std::vector<float> result;
        for (const auto& expected : words_) {
            if (!(stream >> word) || word != expected.text) return {};
            result.push_back(expected.probability);
        }
        if (stream >> word) return {};
        return result;
    }
};
}

#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <CoreFoundation/CoreFoundation.h>

namespace lvt {
struct GlossaryMatch { std::size_t begin{}, end{}; std::string original, id; };
struct GlossaryTokenSpan { std::size_t begin{}, end{}; };
// Immutable per-session rules. No inference prompt includes the full glossary.
class Glossary {
    struct Rule {
        std::string id, mode;
        std::vector<std::string> forms, directions, context, excluded;
    };
    std::vector<Rule> rules_;
    struct CFText {
        CFStringRef value;
        explicit CFText(const std::string& text) : value(CFStringCreateWithBytes(kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(text.data()), text.size(), kCFStringEncodingUTF8, false)) {
            if (!value) throw std::runtime_error("Invalid glossary UTF-8");
        }
        ~CFText() { CFRelease(value); }
        CFText(const CFText&) = delete;
    };
    static std::string folded(std::string text) {
        CFText original(text);
        auto value = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, original.value);
        CFStringLowercase(value, nullptr);
        CFStringNormalize(value, kCFStringNormalizationFormC);
        std::string result(CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1, '\0');
        CFStringGetCString(value, result.data(), result.size(), kCFStringEncodingUTF8);
        CFRelease(value);
        result.resize(std::char_traits<char>::length(result.c_str()));
        return result;
    }
    static bool word(unsigned char c) { return c >= 128 || std::isalnum(c) || c == '_'; }
    static bool unicodeWord(const std::string& text, std::size_t at) {
        if (at >= text.size()) return false;
        auto c = static_cast<unsigned char>(text[at]);
        if (c < 128) return word(c);
        UTF32Char point = c & ((c < 0xE0) ? 0x1F : (c < 0xF0 ? 0x0F : 0x07));
        unsigned length = c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4);
        for (unsigned i = 1; i < length && at + i < text.size(); ++i) point = (point << 6) | (static_cast<unsigned char>(text[at + i]) & 0x3F);
        return CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetAlphaNumeric), point) ||
            CFCharacterSetIsLongCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetNonBase), point);
    }
    static bool bounded(const std::string& text, std::size_t begin, std::size_t end) {
        auto previous = begin;
        if (previous) { --previous; while (previous && (static_cast<unsigned char>(text[previous]) & 0xC0) == 0x80) --previous; }
        return (begin == 0 || !unicodeWord(text, previous)) && (end == text.size() || !unicodeWord(text, end));
    }
    static bool contains(const std::string& text, const std::string& term) {
        return !locations(text, term).empty();
    }
    static std::vector<std::pair<std::size_t, std::size_t>> locations(const std::string& text, const std::string& term) {
        CFText input(text), search(term);
        std::vector<std::pair<std::size_t, std::size_t>> result;
        const auto length = CFStringGetLength(input.value);
        for (CFIndex start = 0; start < length;) {
            CFRange found;
            if (!CFStringFindWithOptions(input.value, search.value, CFRangeMake(start, length - start),
                                        kCFCompareCaseInsensitive | kCFCompareNonliteral, &found)) break;
            CFIndex beginBytes = 0, endBytes = 0;
            CFStringGetBytes(input.value, CFRangeMake(0, found.location), kCFStringEncodingUTF8, 0, false, nullptr, 0, &beginBytes);
            CFStringGetBytes(input.value, CFRangeMake(0, found.location + found.length), kCFStringEncodingUTF8, 0, false, nullptr, 0, &endBytes);
            if (bounded(text, beginBytes, endBytes)) result.emplace_back(beginBytes, endBytes);
            start = found.location + std::max<CFIndex>(1, found.length);
        }
        return result;
    }
    static std::vector<std::string> strings(const nlohmann::json& j, const char* key, bool allowEmpty = true) {
        const auto& value = j.at(key);
        if (!value.is_array() || value.size() > 128 || (!allowEmpty && value.empty())) throw std::runtime_error("Invalid glossary list");
        std::vector<std::string> result;
        for (const auto& item : value) {
            if (!item.is_string()) throw std::runtime_error("Invalid glossary text");
            auto text = item.get<std::string>();
            if (text.empty() || text.size() > 160 || text.find_first_of("\r\n\t") != std::string::npos)
                throw std::runtime_error("Invalid glossary text length");
            result.push_back(text);
        }
        return result;
    }
public:
    static Glossary fromJSON(const std::string& encoded) {
        Glossary result;
        if (encoded.empty()) return result;
        if (encoded.size() > 2 * 1024 * 1024) throw std::runtime_error("Glossary exceeds 2 MiB");
        const auto json = nlohmann::json::parse(encoded);
        if (json.at("schemaVersion") != 1 || !json.at("enabled").is_boolean()) throw std::runtime_error("Invalid glossary schema");
        if (!json.at("categories").is_array() || json.at("categories").size() > 128 ||
            !json.at("entries").is_array() || json.at("entries").size() > 5000) throw std::runtime_error("Glossary capacity exceeded");
        std::set<std::string> categories, active, ids;
        for (const auto& category : json.at("categories")) {
            auto id = category.at("id").get<std::string>();
            if (id.empty() || !categories.insert(id).second || !category.at("enabled").is_boolean()) throw std::runtime_error("Invalid glossary category");
            if (category.at("enabled").get<bool>()) active.insert(id);
        }
        for (const auto& entry : json.at("entries")) {
            Rule rule;
            rule.id = entry.at("id").get<std::string>();
            rule.mode = entry.at("mode").get<std::string>();
            if (rule.id.empty() || !ids.insert(rule.id).second ||
                (rule.mode != "always" && rule.mode != "context" && rule.mode != "normal")) throw std::runtime_error("Invalid glossary rule");
            rule.forms = strings(entry, "forms");
            auto term = entry.at("term").get<std::string>();
            if (term.empty() || term.size() > 160 || term.find_first_of("\r\n\t") != std::string::npos) throw std::runtime_error("Invalid glossary term");
            rule.forms.push_back(term);
            for (auto& form : rule.forms) form = folded(form);
            rule.directions = strings(entry, "directions", false);
            for (const auto& direction : rule.directions) if (direction != "en-pt" && direction != "pt-en") throw std::runtime_error("Unsupported glossary direction");
            rule.context = strings(entry, "context");
            rule.excluded = strings(entry, "excludeContext");
            bool enabledCategory = false;
            for (const auto& category : strings(entry, "categories", false)) {
                if (!categories.contains(category)) throw std::runtime_error("Unknown glossary category");
                enabledCategory |= active.contains(category);
            }
            if (!entry.at("enabled").is_boolean()) throw std::runtime_error("Invalid glossary enabled state");
            if (json.at("enabled").get<bool>() && entry.at("enabled").get<bool>() && enabledCategory)
                result.rules_.push_back(std::move(rule));
        }
        return result;
    }
    std::vector<GlossaryMatch> matches(const std::string& text, const std::string& direction) const {
        const auto lower = folded(text);
        struct Candidate { GlossaryMatch match; bool preserve; bool always; };
        std::vector<Candidate> candidates;
        for (const auto& rule : rules_) {
            if (std::find(rule.directions.begin(), rule.directions.end(), direction) == rule.directions.end()) continue;
            for (const auto& form : rule.forms) {
                if (lower.find(form) == std::string::npos) continue;
                const auto occurrences = locations(text, form);
                for (std::size_t i = 0; i < occurrences.size(); ++i) {
                    const auto [at, end] = occurrences[i];
                    bool preserve = rule.mode == "always";
                    if (rule.mode == "context") {
                        auto left = text.find_last_of(".!?;,\n", at);
                        left = left == std::string::npos ? 0 : left + 1;
                        auto right = text.find_first_of(".!?;,\n", end);
                        right = right == std::string::npos ? text.size() : right;
                        if (i) left = std::max(left, (occurrences[i - 1].second + at) / 2);
                        if (i + 1 < occurrences.size()) right = std::min(right, (end + occurrences[i + 1].first) / 2);
                        auto cueApplies = [&](const auto& cue) {
                            // A cue naming this term must overlap this occurrence;
                            // otherwise evaluate only the local clause/nearest occurrence.
                            const bool anchored = std::any_of(rule.forms.begin(), rule.forms.end(),
                                [&](const auto& variant) { return contains(cue, variant); });
                            for (const auto& [a, b] : locations(text, cue))
                                if (anchored ? (a <= at && end <= b) : (a >= left && b <= right)) return true;
                            return false;
                        };
                        preserve = std::any_of(rule.context.begin(), rule.context.end(), cueApplies) &&
                            !std::any_of(rule.excluded.begin(), rule.excluded.end(), cueApplies);
                    }
                    candidates.push_back({{at, end, text.substr(at, end - at), rule.id}, preserve, rule.mode == "always"});
                }
            }
        }
        const auto allCandidates = candidates;
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const auto& item) {
            return !item.preserve && std::any_of(allCandidates.begin(), allCandidates.end(), [&](const auto& other) {
                return other.always && other.preserve && item.match.begin <= other.match.begin && other.match.end <= item.match.end;
            });
        }), candidates.end());
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            const auto al = a.match.end - a.match.begin, bl = b.match.end - b.match.begin;
            if (al != bl) return al > bl;
            if (a.match.begin != b.match.begin) return a.match.begin < b.match.begin;
            // An explicit normal rule wins over preservation of an identical form.
            return !a.preserve && b.preserve;
        });
        std::vector<Candidate> selected;
        for (const auto& item : candidates) {
            if (std::none_of(selected.begin(), selected.end(), [&](const auto& other) {
                return item.match.begin < other.match.end && other.match.begin < item.match.end;
            })) selected.push_back(item);
        }
        std::vector<GlossaryMatch> result;
        for (const auto& item : selected) if (item.preserve) result.push_back(item.match);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
        return result;
    }
    // Attention comes from native CTranslate2 for the complete sentence. Offsets
    // come from SentencePiece's byte spans; no substring replacement of translated words.
    static std::string restore(const std::string& output, const std::vector<GlossaryMatch>& matches,
                               const std::vector<GlossaryTokenSpan>& source,
                               const std::vector<GlossaryTokenSpan>& target,
                               const std::vector<std::vector<float>>& attention) {
        if (matches.empty()) return output;
        // A model-preserved term needs no rewrite. Require exact surface and a
        // complete occurrence count, so one existing term cannot hide omissions.
        std::vector<GlossaryMatch> pending;
        for (const auto& match : matches) {
            const auto required = std::count_if(matches.begin(), matches.end(), [&](const auto& item) { return item.original == match.original; });
            const auto found = locations(output, match.original);
            const auto exact = std::count_if(found.begin(), found.end(), [&](const auto& at) { return output.substr(at.first, at.second - at.first) == match.original; });
            if (exact < required) pending.push_back(match);
        }
        if (pending.empty()) return output;
        if (attention.size() < target.size()) throw std::runtime_error("Glossary alignment is unavailable");
        for (const auto& span : target) if (span.begin > span.end || span.end > output.size()) throw std::runtime_error("Invalid glossary target offsets");
        struct Replacement { std::size_t begin, end; std::string text; };
        std::vector<Replacement> replacements;
        for (const auto& match : pending) {
            std::vector<bool> sourceTerm(source.size());
            for (std::size_t s = 0; s < source.size(); ++s)
                sourceTerm[s] = source[s].begin < match.end && match.begin < source[s].end;
            std::size_t first = target.size(), last = 0;
            for (std::size_t t = 0; t < target.size(); ++t) {
                const auto& row = attention[t];
                if (row.size() != source.size() || row.empty() ||
                    std::any_of(row.begin(), row.end(), [](float value) { return !std::isfinite(value) || value < 0; }))
                    throw std::runtime_error("Invalid glossary attention");
                const auto best = static_cast<std::size_t>(std::max_element(row.begin(), row.end()) - row.begin());
                float mass = 0, total = 0;
                for (std::size_t s = 0; s < row.size(); ++s) {
                    if (source[s].end > source[s].begin) total += row[s]; // Exclude explicit language/EOS control tokens.
                    if (sourceTerm[s]) mass += row[s];
                }
                // These are conservative attention gates, not a calibrated semantic
                // confidence score. Ambiguous alignment withholds the translation.
                if (sourceTerm[best] && row[best] >= .15f && total > 0 && mass / total >= .60f && target[t].end > target[t].begin) {
                    first = std::min(first, t); last = std::max(last, t);
                }
            }
            if (first == target.size()) throw std::runtime_error("Protected term has no reliable translation alignment");
            auto begin = target[first].begin, end = target[last].end;
            if (begin >= end || end > output.size()) throw std::runtime_error("Invalid glossary target span");
            while (begin < end && std::isspace(static_cast<unsigned char>(output[begin]))) ++begin;
            while (end > begin && (std::isspace(static_cast<unsigned char>(output[end - 1])) || std::ispunct(static_cast<unsigned char>(output[end - 1])))) --end;
            while (begin && word(output[begin - 1])) --begin;
            while (end < output.size() && word(output[end])) ++end;
            const auto replacedText = output.substr(begin, end - begin);
            for (const auto* protectedWord : {"não", "nem", "nunca", "jamais", "sem", "not", "never", "no", "without", "nothing", "nobody"})
                if (contains(replacedText, protectedWord) && !contains(match.original, protectedWord))
                    throw std::runtime_error("Glossary alignment would change negation; translation withheld");
            auto digits = [](const std::string& text) {
                std::string result;
                for (unsigned char c : text) if (std::isdigit(c)) result += static_cast<char>(c);
                return result;
            };
            if (digits(replacedText) != digits(match.original)) throw std::runtime_error("Glossary alignment would change numbers; translation withheld");
            // Reject gaps aligned to unrelated source words: do not delete meaning.
            for (std::size_t t = first; t <= last; ++t) {
                const auto& row = attention[t];
                const auto best = static_cast<std::size_t>(std::max_element(row.begin(), row.end()) - row.begin());
                float mass = 0, total = 0;
                for (std::size_t s = 0; s < row.size(); ++s) {
                    if (source[s].end > source[s].begin) total += row[s];
                    if (sourceTerm[s]) mass += row[s];
                }
                if ((!sourceTerm[best] || total <= 0 || mass / total < .60f) && target[t].end > target[t].begin &&
                    std::any_of(output.begin() + target[t].begin, output.begin() + target[t].end, [](unsigned char c) { return word(c); }))
                    throw std::runtime_error("Ambiguous glossary alignment; translation withheld");
            }
            if (begin >= end) throw std::runtime_error("Empty glossary target span");
            for (const auto& previous : replacements)
                if (begin < previous.end && previous.begin < end) throw std::runtime_error("Overlapping glossary alignment");
            replacements.push_back({begin, end, match.original});
        }
        std::sort(replacements.begin(), replacements.end(), [](const auto& a, const auto& b) { return a.begin > b.begin; });
        auto restored = output;
        for (const auto& replacement : replacements) restored.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
        return restored;
    }
};
}

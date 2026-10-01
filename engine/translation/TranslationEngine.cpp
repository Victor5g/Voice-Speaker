#include "TranslationEngine.hpp"
#include <ctranslate2/translator.h>
#include <ctranslate2/logging.h>
#include <sentencepiece_processor.h>
#include <chrono>
#include "../language/TextUnits.hpp"
#include "../common/MemoryBudget.hpp"
#include "Glossary.hpp"

namespace lvt {
struct TranslationEngine::Impl {
    MemoryBudget::Ticket memory;
    std::unique_ptr<ctranslate2::Translator> translator;
    sentencepiece::SentencePieceProcessor source, target;
    std::string prefix;
    std::string direction;
    Glossary glossary;
};
TranslationEngine::TranslationEngine(const ModelBundle& bundle, std::string direction, std::string precision)
    : impl_(std::make_unique<Impl>()) {
    if (direction != "pt-en" && direction != "en-pt") throw std::runtime_error("Unsupported translation direction");
    if (precision != "float32" && precision != "int8") throw std::runtime_error("Unsupported translation precision");
    bundle.verify();
    impl_->memory = MemoryBudget::shared().claim(std::filesystem::file_size(bundle.path(bundle.primary)) * 3 + (64ULL << 20));
    auto relative = std::filesystem::path(bundle.primary).parent_path();
    for (const char* name : {"config.json", "shared_vocabulary.json", "source.spm", "target.spm"})
        bundle.verifiedMember((relative / name).string());
    if (!impl_->source.Load(bundle.path((relative / "source.spm").string()).string()).ok() ||
        !impl_->target.Load(bundle.path((relative / "target.spm").string()).string()).ok())
        throw std::runtime_error("SentencePiece tokenizer failed to load");
    ctranslate2::set_log_level(ctranslate2::LogLevel::Off);
    ctranslate2::ReplicaPoolConfig config;
    config.num_threads_per_replica = 4;
    config.max_queued_batches = 1;
    impl_->translator = std::make_unique<ctranslate2::Translator>(bundle.path(bundle.primary).parent_path().string(),
        ctranslate2::Device::CPU, precision == "int8" ? ctranslate2::ComputeType::INT8 : ctranslate2::ComputeType::FLOAT32,
        std::vector<int>{0}, false, config);
    if (direction == "en-pt") impl_->prefix = ">>pob<<";
    impl_->direction = std::move(direction);
}
TranslationEngine::~TranslationEngine() = default;
void TranslationEngine::releaseCaches() { impl_->translator->clear_cache(); }
void TranslationEngine::setGlossaryJSON(const std::string& json) { impl_->glossary = Glossary::fromJSON(json); }
TranslatedText TranslationEngine::translate(const std::string& text, std::stop_token stop) {
    if (stop.stop_requested()) return {};
    auto blank = [](const std::string& value) { return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); }); };
    if (blank(text) || text.size() > 8192) throw std::runtime_error("Translation input is empty or too long");
    auto workingMemory = MemoryBudget::shared().claim(64ULL << 20);
    const auto begin = std::chrono::steady_clock::now();
    std::string combined;
    for (const auto& unit : sentenceUnits(text)) {
    if (stop.stop_requested()) return {};
    std::vector<std::string> tokens;
    const auto protectedTerms = impl_->glossary.matches(unit, impl_->direction);
    if (!impl_->source.Encode(unit, &tokens).ok()) throw std::runtime_error("Source tokenization failed");
    if (!impl_->prefix.empty()) tokens.insert(tokens.begin(), impl_->prefix);
    tokens.emplace_back("</s>");
    if (tokens.size() > 512) throw std::runtime_error("Translation unit exceeds 512 tokens");
    ctranslate2::TranslationOptions options;
    options.beam_size = 4;
    options.max_input_length = 512;
    options.max_decoding_length = 512;
    options.return_end_token = true;
    options.return_attention = !protectedTerms.empty();
    auto inference = impl_->translator->translate_batch({tokens}, options).front();
    auto result = inference.output();
    // Beam search cannot be interrupted inside CTranslate2. The bounded unit finishes
    // on this worker; invalidated work is discarded before anything can be spoken.
    if (stop.stop_requested()) return {};
    if (result.empty() || result.back() != "</s>") throw std::runtime_error("Translation did not reach an end token");
    result.pop_back();
    std::string output;
    if (!impl_->target.Decode(result, &output).ok() || blank(output)) throw std::runtime_error("Target decoding failed");
    if (!protectedTerms.empty()) {
        const auto sourceProto = impl_->source.EncodeAsImmutableProto(unit);
        const auto targetProto = impl_->target.DecodePiecesAsImmutableProto(result);
        std::vector<GlossaryTokenSpan> sourceSpans, targetSpans;
        if (!impl_->prefix.empty()) sourceSpans.push_back({0, 0});
        for (const auto& piece : sourceProto.pieces()) sourceSpans.push_back({piece.begin(), piece.end()});
        sourceSpans.push_back({0, 0}); // Explicit source EOS.
        for (const auto& piece : targetProto.pieces()) targetSpans.push_back({piece.begin(), piece.end()});
        if (sourceSpans.size() != tokens.size() || targetSpans.size() != result.size() ||
            targetProto.text() != output || inference.attention.empty())
            throw std::runtime_error("Glossary tokenizer alignment is unavailable");
        output = Glossary::restore(output, protectedTerms, sourceSpans, targetSpans, inference.attention.front());
    }
    if (!combined.empty()) combined += ' ';
    combined += output;
    }
    return {std::move(combined), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count()};
}
}

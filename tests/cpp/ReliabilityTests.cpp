#include "../../engine/audio/AudioCore.hpp"
#include "../../engine/audio/WavReader.hpp"
#include "../../engine/translation/TranslationEngine.hpp"
#include "../../engine/tts/TTSEngine.hpp"
#include "../../engine/common/OnnxModel.hpp"
#include <iostream>
#include <future>
#include <iomanip>
int passed = 0;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++passed; std::cout << "PASS " << message << std::endl;
}
template<class F> bool throws(F&& work) { try { work(); } catch (const std::exception&) { return true; } return false; }
int main() {
    try {
        nlohmann::json catalog, selection;
        std::ifstream("models/manifest.json") >> catalog;
        std::ifstream("models/selection.json") >> selection;
        auto entry = [&](const char* type) { return *std::find_if(catalog["models"].begin(), catalog["models"].end(), [&](const auto& e) { return e["type"] == type; }); };
        auto voice = lvt::ModelBundle::fromJSON("models", entry("tts"));
        auto absent = voice; absent.files.front().filename = "optional/nonexistent-required-model";
        check(throws([&] { lvt::TTSEngine engine(absent); }), "Missing model produces a native error");
        auto corrupt = voice; corrupt.files.front().sha256.assign(64, '0');
        check(throws([&] { lvt::TTSEngine engine(corrupt); }), "Corrupt auxiliary asset is rejected before inference");
        {
            const std::string invalid = "invalid ONNX development fixture";
            std::filesystem::create_directories("build/reliability-model");
            std::ofstream("build/reliability-model/invalid.onnx", std::ios::binary) << invalid;
            unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(invalid.data(), CC_LONG(invalid.size()), digest);
            std::ostringstream hash; for (auto byte : digest) hash << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
            lvt::ModelBundle invalidModel{"build/reliability-model", {{"invalid.onnx", hash.str(), invalid.size()}}, "invalid.onnx"};
            check(throws([&] { lvt::OnnxModel model(invalidModel); }), "Verified but incompatible ONNX model raises an error without terminating the process");
        }
        auto& budget = lvt::MemoryBudget::shared();
        budget.setPressure(2);
        bool denied = false;
        try { lvt::TTSEngine engine(voice); } catch (const lvt::MemoryBudget::Unavailable&) { denied = true; }
        budget.setPressure(0);
        check(denied, "Critical memory pressure rejects native model admission");
        {
            lvt::TTSEngine tts(voice);
            budget.setPressure(2);
            check(throws([&] { tts.synthesize("Teste de pressão."); }), "Loaded TTS still requires admission for new inference");
            budget.setPressure(0);
            check(throws([&] { tts.synthesize(std::string(2000, 'x')); }), "Oversized synthesis is rejected before native allocation");
            std::stop_source cancellation;
            std::promise<void> entering; auto entered = entering.get_future();
            auto inference = std::async(std::launch::async, [&] { entering.set_value(); return tts.synthesize("Eu preciso conversar sobre esta reunião amanhã. Ainda temos muitos detalhes para organizar antes do encontro com todos os participantes.", cancellation.get_token()); });
            entered.wait(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cancellation.request_stop();
            check(inference.get().samples.empty(), "Cancelled TTS inference cannot return obsolete audio");
        }
        {
            auto translatedEntry = *std::find_if(catalog["models"].begin(), catalog["models"].end(), [](const auto& e) { return e["type"] == "translation" && e["direction"] == "pt-en" && e["quantization"] == "int8"; });
            lvt::TranslationEngine translator(lvt::ModelBundle::fromJSON("models", translatedEntry), "pt-en", "int8");
            budget.setPressure(2);
            check(throws([&] { translator.translate("Teste de pressão."); }), "Loaded translation still requires admission for new inference");
            budget.setPressure(0);
            std::stop_source cancellation;
            auto inference = std::async(std::launch::async, [&] { return translator.translate("Eu não autorizei o pagamento de duzentos reais e preciso que o gerente confira os números antes de continuar com a operação amanhã.", cancellation.get_token()); });
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); cancellation.request_stop();
            check(inference.get().text.empty(), "Cancelled translation cannot enter the TTS queue");
        }
        auto pcm = lvt::readWav16k("tests/data/generated/ptbr.wav");
        double maxStopRequestMs = 0, maxTeardownMs = 0;
        for (int cycle = 0; cycle < 12; ++cycle) {
            lvt::AudioCore core;
            core.configureASR((std::filesystem::path("models") / selection.at("filename").get<std::string>()).string(), selection.at("sha256"));
            core.configureConversation("models", 48000); core.start();
            if (cycle % 4 == 0) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (core.snapshot().asr.state == 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                check(core.snapshot().asr.state == 2, "Restart initializes production ASR and diarization");
                for (std::size_t i = 0; i + 320 <= std::min(pcm.size(), std::size_t(32000)); i += 320) {
                    core.capture(pcm.data() + i, 320, 16000, mach_absolute_time());
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto begin = std::chrono::steady_clock::now(); core.requestStop();
            maxStopRequestMs = std::max(maxStopRequestMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
            std::vector<float> out(960, 1); core.render(out.data(), out.size(), 48000);
            check(std::all_of(out.begin(), out.end(), [](float x) { return x == 0; }), "Stop immediately silences playback during initialization or recognition");
            auto teardown = std::async(std::launch::async, [&] { core.stop(); }); teardown.get();
            maxTeardownMs = std::max(maxTeardownMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
            lvt::ConversationEvent late;
            check(!core.popConversationEvent(late), "Stopped session exposes no late conversation event");
            check(budget.snapshot().reserved == 0, "Stopped session releases every model reservation");
        }
        check(maxStopRequestMs < 20, "Stop request remains below one 20 ms audio block");
        std::cout << nlohmann::json{{"result","PASS"},{"passed",passed},{"cycles",12},{"maxStopRequestMs",maxStopRequestMs},{"maxBackgroundTeardownMs",maxTeardownMs}}.dump() << std::endl;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}

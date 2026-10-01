#include "../../engine/tts/TTSEngine.hpp"
#include <sherpa-onnx/c-api/c-api.h>
#include <iostream>
#include <sys/resource.h>
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: tts-benchmark MODEL_ROOT OUTPUT_DIRECTORY");
        nlohmann::json voices;
        const std::filesystem::path root(argv[1]), output(argv[2]);
        std::ifstream(root / "tts-assets.json") >> voices;
        std::filesystem::create_directories(output);
        for (const auto& entry : voices) {
            lvt::TTSEngine engine(lvt::ModelBundle::fromJSON(root, entry));
            std::stop_source stopped; stopped.request_stop();
            if (!engine.synthesize("cancel", stopped.get_token()).samples.empty()) throw std::runtime_error("TTS cancellation failed");
            std::string text = entry["language"] == "pt-BR" ? "Eu não autorizei o pagamento. O ônibus chega amanhã às três horas." :
                "I did not authorize the payment. The bus arrives tomorrow at three o'clock.";
            auto audio = engine.synthesize(text);
            auto filename = output / (entry.at("voice").get<std::string>() + ".wav");
            if (!SherpaOnnxWriteWave(audio.samples.data(), int(audio.samples.size()), audio.sampleRate, filename.c_str()))
                throw std::runtime_error("Cannot write development audio artifact");
            rusage usage{}; getrusage(RUSAGE_SELF, &usage);
            std::cout << nlohmann::json{{"voice", entry["voice"]}, {"text", text}, {"latencyMs", audio.latencyMs},
                {"sampleRate", audio.sampleRate}, {"durationMs", 1000.0 * audio.samples.size() / audio.sampleRate},
                {"peakRSSBytes", usage.ru_maxrss}}.dump() << std::endl;
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../../engine/asr/WhisperASR.hpp"
#include "../../engine/audio/WavReader.hpp"
#include <iostream>
#include <sys/resource.h>
#include <mach/mach.h>
#include <chrono>

int main(int argc, char **argv) {
    @autoreleasepool {
        int first = 1;
        std::string backend, resources;
        if (argc >= 5 && std::string(argv[1]) == "--backend" && std::string(argv[3]) == "--resources") {
            backend = argv[2]; resources = argv[4]; first = 5;
        }
        const bool reuse = !(argc > first && std::string(argv[first]) == "--no-reuse");
        if (!reuse) ++first;
        if (argc < first + 4) { std::cerr << "usage: asr-benchmark [--backend metal|coreml-ane|coreml-all|coreml-gpu --resources models] model sha256 repeats wav...\n"; return 2; }
        try {
            const auto begin = std::chrono::steady_clock::now();
            auto configuration = lvt::asrConfiguration(resources, argv[first], argv[first + 1], backend);
            configuration.reuseEncoder = reuse;
            lvt::WhisperASR asr(argv[first], argv[first + 1], configuration);
            const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            const int repeats = std::clamp(std::stoi(argv[first + 2]), 1, 20);
            for (int a = first + 3; a < argc; ++a) {
                auto pcm = lvt::readWav16k(argv[a]);
                const auto warmup = asr.recognize(pcm); // Excluded from measured runs.
                for (int i = 0; i < repeats; ++i) {
                    auto result = asr.recognize(pcm);
                    struct rusage usage{}; getrusage(RUSAGE_SELF, &usage);
                    task_vm_info_data_t vm{}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
                    task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count);
                    NSDictionary *row = @{
                        @"model": @(argv[first]), @"fixture": @(argv[a]), @"iteration": @(i),
                        @"backend": @(int(asr.backend())), @"modelLoadMs": @(loadMs), @"warmupMs": @(warmup.latencyMs),
                        @"encoderReuse": @(reuse),
                        @"melMs": @(lvt::hostclock::milliseconds(result.timing.inferenceStarted, result.timing.melFinished).value_or(0)),
                        @"languageMs": @(lvt::hostclock::milliseconds(result.timing.melFinished, result.timing.languageFinished).value_or(0)),
                        @"decodeMs": @(lvt::hostclock::milliseconds(result.timing.languageFinished, result.timing.inferenceFinished).value_or(0)),
                        @"language": @(result.detectedLanguage.c_str()), @"sourceText": @(result.text.c_str()),
                        @"ptProbability": @(result.ptProbability), @"enProbability": @(result.enProbability),
                        @"asrLatencyMs": @(result.latencyMs), @"audioDurationMs": @(result.durationMs),
                        @"rtf": @(result.latencyMs / result.durationMs), @"physicalFootprint": @(vm.phys_footprint),
                        @"peakRSS": @(usage.ru_maxrss), @"metal": @(asr.metalActive()),
                        @"metalAllocatedBytes": @(MTLCreateSystemDefaultDevice().currentAllocatedSize),
                        @"thermalState": @(NSProcessInfo.processInfo.thermalState)
                    };
                    NSData *json = [NSJSONSerialization dataWithJSONObject:row options:NSJSONWritingSortedKeys error:nil];
                    std::cout << std::string((const char *)json.bytes, json.length) << std::endl;
                }
            }
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    return 0;
}

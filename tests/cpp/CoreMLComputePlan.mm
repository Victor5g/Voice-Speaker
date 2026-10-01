#import <Foundation/Foundation.h>
#import <CoreML/CoreML.h>
#include "../../engine/asr/ASRBackend.hpp"
#include <iostream>

// Development diagnostic only. A preferred device is a plan, NOT runtime proof.
static std::string deviceName(id<MLComputeDeviceProtocol> device) {
    if (!device) return "unknown";
    if ([device isKindOfClass:MLNeuralEngineComputeDevice.class]) return "ane";
    if ([device isKindOfClass:MLGPUComputeDevice.class]) return "gpu";
    if ([device isKindOfClass:MLCPUComputeDevice.class]) return "cpu";
    return NSStringFromClass([(NSObject *)device class]).UTF8String;
}

static void inventory(MLComputePlan *plan, MLModelStructureProgramBlock *block,
                      const std::string& path, nlohmann::json& rows) {
    unsigned index = 0;
    for (MLModelStructureProgramOperation *op in block.operations) {
        auto usage = [plan computeDeviceUsageForMLProgramOperation:op];
        auto cost = [plan estimatedCostOfMLProgramOperation:op];
        auto supported = nlohmann::json::array();
        for (id<MLComputeDeviceProtocol> device in usage.supportedComputeDevices)
            supported.push_back(deviceName(device));
        auto outputs = nlohmann::json::array();
        for (MLModelStructureProgramNamedValueType *output in op.outputs)
            outputs.push_back(std::string(output.name.UTF8String));
        const auto location = path + "/" + std::to_string(index++);
        rows.push_back({{"path", location}, {"operator", op.operatorName.UTF8String},
            {"outputs", outputs}, {"preferredDevice", deviceName(usage.preferredComputeDevice)},
            {"supportedDevices", supported},
            {"estimatedCostWeight", cost ? nlohmann::json(cost.weight) : nlohmann::json(nullptr)}});
        unsigned child = 0;
        for (MLModelStructureProgramBlock *nested in op.blocks)
            inventory(plan, nested, location + "/block" + std::to_string(child++), rows);
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            if (argc != 5) throw std::runtime_error("Usage: coreml-compute-plan resources model sha backend");
            if (@available(macOS 14.4, *)) {
                const auto config = lvt::asrConfiguration(argv[1], argv[2], argv[3], argv[4]);
                if (config.backend == lvt::ASRBackend::metal)
                    throw std::runtime_error("Metal does not use a Core ML compute plan");
                config.verify();
                MLModelConfiguration *ml = [MLModelConfiguration new];
                switch (config.backend) {
                    case lvt::ASRBackend::coreMLANE: ml.computeUnits = MLComputeUnitsCPUAndNeuralEngine; break;
                    case lvt::ASRBackend::coreMLAll: ml.computeUnits = MLComputeUnitsAll; break;
                    case lvt::ASRBackend::coreMLGPU: ml.computeUnits = MLComputeUnitsCPUAndGPU; break;
                    default: throw std::runtime_error("Invalid Core ML mode");
                }
                const auto path = config.encoder.path(config.encoderDirectory).string();
                dispatch_semaphore_t ready = dispatch_semaphore_create(0);
                __block MLComputePlan *plan = nil;
                __block NSError *failure = nil;
                [MLComputePlan loadContentsOfURL:[NSURL fileURLWithPath:@(path.c_str())]
                    configuration:ml completionHandler:^(MLComputePlan *result, NSError *error) {
                        plan = result; failure = error; dispatch_semaphore_signal(ready);
                    }];
                if (dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 120 * NSEC_PER_SEC)))
                    throw std::runtime_error("Compute plan timed out");
                if (!plan || failure) throw std::runtime_error(failure ? failure.localizedDescription.UTF8String : "Missing compute plan");
                if (!plan.modelStructure.program) throw std::runtime_error("Expected ML Program encoder");
                nlohmann::json rows = nlohmann::json::array();
                auto functions = plan.modelStructure.program.functions;
                for (NSString *name in [[functions allKeys] sortedArrayUsingSelector:@selector(compare:)])
                    inventory(plan, functions[name].block, name.UTF8String, rows);
                nlohmann::json counts = nlohmann::json::object();
                for (const auto& row : rows) {
                    const auto key = row.at("preferredDevice").get<std::string>();
                    counts[key] = counts.value(key, 0) + 1;
                }
                std::cout << nlohmann::json({{"backend", argv[4]}, {"modelSHA256", argv[3]},
                    {"encoderDirectory", path}, {"evidence", "anticipated compute plan; not measured hardware execution"},
                    {"preferredDeviceCounts", counts}, {"operations", rows}}).dump(2) << '\n';
            } else throw std::runtime_error("macOS 14.4 required");
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n'; return 1;
        }
    }
}

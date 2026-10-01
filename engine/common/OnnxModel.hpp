#pragma once
#include "ModelAssets.hpp"
#include "MemoryBudget.hpp"
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <span>
#include <stop_token>

namespace lvt {
struct OnnxOutput { std::vector<float> values; std::vector<std::int64_t> shape; };
class OnnxModel {
    static Ort::Env& environment() {
        static Ort::Env env(ORT_LOGGING_LEVEL_FATAL, "LocalVoiceTranslator",
            [](void*, OrtLoggingLevel, const char*, const char*, const char*, const char*) {}, nullptr);
        static const bool disabled = [&] { env.DisableTelemetryEvents(); return true; }();
        (void)disabled; return env;
    }
    MemoryBudget::Ticket memory_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_, output_;
public:
    explicit OnnxModel(const ModelBundle& bundle, int threads = 2, bool optimize = true) {
        bundle.verify();
        memory_ = MemoryBudget::shared().claim(std::filesystem::file_size(bundle.path(bundle.primary)) * 3 + (64ULL << 20));
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(threads); options.SetInterOpNumThreads(1);
        options.SetLogSeverityLevel(4); options.DisableCpuMemArena(); options.DisableMemPattern();
        if (!optimize) options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
        session_ = std::make_unique<Ort::Session>(environment(), bundle.path(bundle.primary).c_str(), options);
        if (session_->GetInputCount() != 1 || session_->GetOutputCount() != 1 ||
            session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            session_->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("Unsupported ONNX tensor contract");
        Ort::AllocatorWithDefaultOptions allocator;
        input_ = session_->GetInputNameAllocated(0, allocator).get();
        output_ = session_->GetOutputNameAllocated(0, allocator).get();
    }
    std::vector<std::int64_t> inputShape() const { return session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); }
    std::string metadata(const char* key) const {
        Ort::AllocatorWithDefaultOptions allocator;
        auto value = session_->GetModelMetadata().LookupCustomMetadataMapAllocated(key, allocator);
        return value ? value.get() : "";
    }
    OnnxOutput run(std::span<float> input, std::span<const std::int64_t> shape, std::size_t maxOutput, std::stop_token stop = {}) {
        if (stop.stop_requested()) return {};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor(memory, input.data(), input.size(), shape.data(), shape.size());
        Ort::RunOptions options;
        std::stop_callback cancel(stop, [&] { options.SetTerminate(); });
        const char* inputName = input_.c_str(); const char* outputName = output_.c_str();
        std::vector<Ort::Value> outputs;
        try { outputs = session_->Run(options, &inputName, &tensor, 1, &outputName, 1); }
        catch (...) { if (stop.stop_requested()) return {}; throw std::runtime_error("Local ONNX inference failed"); }
        if (stop.stop_requested()) return {};
        const auto info = outputs.front().GetTensorTypeAndShapeInfo();
        const auto count = info.GetElementCount();
        if (!count || count > maxOutput) throw std::runtime_error("Invalid ONNX output length");
        const float* values = outputs.front().GetTensorData<float>();
        return {{values, values + count}, info.GetShape()};
    }
};
}

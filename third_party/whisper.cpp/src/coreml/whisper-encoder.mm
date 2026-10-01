#if !__has_feature(objc_arc)
#error This file must be compiled with automatic reference counting enabled (-fobjc-arc)
#endif

#import "whisper-encoder.h"
#import "whisper-encoder-impl.h"

#import <CoreML/CoreML.h>

#include <stdlib.h>
#include <cmath>

#if __cplusplus
extern "C" {
#endif

struct whisper_coreml_context {
    const void * data;
};

struct whisper_coreml_context * whisper_coreml_init(const char * path_model, int compute_units) {
    @autoreleasepool {
    NSString * path_model_str = [[NSString alloc] initWithUTF8String:path_model];

    NSURL * url_model = [NSURL fileURLWithPath: path_model_str];

    // select which device to run the Core ML model on
    MLModelConfiguration *config = [[MLModelConfiguration alloc] init];
    switch (compute_units) {
        case 1: config.computeUnits = MLComputeUnitsCPUAndNeuralEngine; break;
        case 2: config.computeUnits = MLComputeUnitsAll; break;
        case 3: config.computeUnits = MLComputeUnitsCPUAndGPU; break;
        default: return NULL;
    }

    const void * data = CFBridgingRetain([[whisper_encoder_impl alloc] initWithContentsOfURL:url_model configuration:config error:nil]);

    if (data == NULL) {
        return NULL;
    }

    whisper_coreml_context * ctx = new whisper_coreml_context;

    ctx->data = data;

    return ctx;
    }
}

void whisper_coreml_free(struct whisper_coreml_context * ctx) {
    CFRelease(ctx->data);
    delete ctx;
}

int whisper_coreml_encode(
        const whisper_coreml_context * ctx,
                             int64_t   n_ctx,
                             int64_t   n_mel,
                               float * mel,
                               float * out,
                               int64_t out_count) {
    @autoreleasepool {
    MLMultiArray * inMultiArray = [
        [MLMultiArray alloc] initWithDataPointer: mel
                                           shape: @[@1, @(n_mel), @(n_ctx)]
                                        dataType: MLMultiArrayDataTypeFloat32
                                         strides: @[@(n_ctx*n_mel), @(n_ctx), @1]
                                     deallocator: nil
                                           error: nil
    ];

        if (!inMultiArray) return 0;
        whisper_encoder_implOutput * outCoreML = [(__bridge id) ctx->data predictionFromLogmel_data:inMultiArray error:nil];
        MLMultiArray * output = outCoreML.output;
        if (!output || output.dataType != MLMultiArrayDataTypeFloat32 || output.count != out_count) return 0;
        // Reject a changed output layout before copying into the ggml buffer.
        NSInteger stride = 1;
        for (NSInteger i = output.shape.count - 1; i >= 0; --i) {
            if (output.strides[i].integerValue != stride) return 0;
            stride *= output.shape[i].integerValue;
        }
        const float * values = (const float *)output.dataPointer;
        for (int64_t i = 0; i < out_count; ++i) if (!std::isfinite(values[i])) return 0;
        memcpy(out, values, out_count * sizeof(float));
        return 1;
    }
}

#if __cplusplus
}
#endif

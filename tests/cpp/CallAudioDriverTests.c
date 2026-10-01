#include <CoreAudio/AudioServerPlugIn.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL driver contract line %d\n", __LINE__); return 1; } } while(0)
int main(int argc, char** argv) {
    CHECK(argc == 3);
    AudioServerPlugInDriverRef drivers[2]; void* libraries[2];
    CFStringRef uids[2];
    for (int d = 0; d < 2; ++d) {
        libraries[d] = dlopen(argv[d+1], RTLD_NOW | RTLD_LOCAL); CHECK(libraries[d]);
        void* (*create)(CFAllocatorRef, CFUUIDRef) = dlsym(libraries[d], "BlackHole_Create"); CHECK(create);
        drivers[d] = create(NULL, kAudioServerPlugInTypeUUID); CHECK(drivers[d]);
        AudioObjectPropertyAddress address = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        UInt32 size = sizeof(CFStringRef);
        CHECK((*drivers[d])->GetPropertyData(drivers[d], 3, 0, &address, 0, NULL, size, &size, &uids[d]) == noErr);
        address.mSelector = kAudioDevicePropertyStreams;
        for (int direction = 0; direction < 2; ++direction) {
            address.mScope = direction ? kAudioObjectPropertyScopeOutput : kAudioObjectPropertyScopeInput;
            CHECK((*drivers[d])->GetPropertyDataSize(drivers[d], 3, 0, &address, 0, NULL, &size) == noErr && size == sizeof(AudioObjectID));
        }
        CHECK((*drivers[d])->StartIO(drivers[d], 3, 1) == noErr);
    }
    CHECK(!CFEqual(uids[0], uids[1]));
    // Exercise the delivered bundles in-process; no HAL installation/physical I/O.
    for (unsigned pass = 0; pass < 20; ++pass) {
        AudioServerPlugInIOCycleInfo cycle = {0};
        cycle.mInputTime.mSampleTime = cycle.mOutputTime.mSampleTime = 65500 + pass * 128;
        Float32 input[256], output[256];
        for (int d = 0; d < 2; ++d) {
            for (int i = 0; i < 256; ++i) input[i] = (d ? -.25f : .5f) * ((i % 2) ? 1 : .5f);
            CHECK((*drivers[d])->DoIOOperation(drivers[d], 3, 7, 1, kAudioServerPlugInIOOperationWriteMix, 128, &cycle, input, NULL) == noErr);
        }
        for (int d = 0; d < 2; ++d) {
            CHECK((*drivers[d])->DoIOOperation(drivers[d], 3, 4, 1, kAudioServerPlugInIOOperationReadInput, 128, &cycle, output, NULL) == noErr);
            for (int i = 0; i < 256; ++i) CHECK(output[i] == (d ? -.25f : .5f) * ((i % 2) ? 1 : .5f));
        }
        cycle.mInputTime.mSampleTime += 256;
        for (int d = 0; d < 2; ++d) {
            CHECK((*drivers[d])->DoIOOperation(drivers[d], 3, 4, 1, kAudioServerPlugInIOOperationReadInput, 128, &cycle, output, NULL) == noErr);
            for (int i = 0; i < 256; ++i) CHECK(output[i] == 0);
        }
    }
    for (int d = 0; d < 2; ++d) { CHECK((*drivers[d])->StopIO(drivers[d], 3, 1) == noErr); CFRelease(uids[d]); dlclose(libraries[d]); }
    puts("PASS delivered driver bundles: independent UIDs, bidirectional stereo PCM, ring wrap, silence without producer and stop; HAL runtime pending");
}

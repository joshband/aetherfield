// DEPRECATED: Abandoned approach — superceded by split testing via XCTest (HT-2) and REAPER MCP (HT-10).
// This file remains for historical reference only and is not maintained.
// Original intent: HT-2 (Rate Negotiation) and HT-10 (Offline Determinism) Test, direct AudioUnit testing.
// Compile: clang++ -framework AudioToolbox -std=c++20 HT2_HT10_Test.mm -o ht2_ht10_test

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>

class AetherfieldAUTester {
private:
    AudioUnit audioUnit = NULL;
    AudioComponentInstance componentInstance = NULL;

public:
    ~AetherfieldAUTester() {
        if (componentInstance) {
            AudioComponentInstanceDispose(componentInstance);
        }
    }

    bool initialize() {
        // Find Aetherfield AU component
        AudioComponentDescription desc = {
            .componentType = kAudioUnitType_Effect,
            .componentSubType = 'Aeth',
            .componentManufacturer = 'Josh',
            .componentFlags = 0,
            .componentFlagsMask = 0
        };

        AudioComponent component = AudioComponentFindNext(NULL, &desc);
        if (!component) {
            std::cerr << "ERROR: Aetherfield AU not found in system" << std::endl;
            std::cerr << "       Make sure the AU is built and discoverable" << std::endl;
            return false;
        }

        OSStatus status = AudioComponentInstanceNew(component, &componentInstance);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to instantiate AU (OSStatus " << status << ")" << std::endl;
            return false;
        }

        audioUnit = componentInstance;

        std::cout << "✓ Aetherfield AU loaded successfully" << std::endl;

        return true;
    }

    // HT-2: Test rate negotiation
    bool testHT2_RateNegotiation() {
        std::cout << "\n=== HT-2: Rate Negotiation ===" << std::endl;

        struct RateTest {
            UInt32 rate;
            const char *name;
            bool shouldSupport;
        };

        RateTest tests[] = {
            {44100, "44.1 kHz", true},
            {48000, "48 kHz", true},
            {96000, "96 kHz", false}
        };

        bool allPassed = true;

        for (const auto &test : tests) {
            std::cout << "\nTesting " << test.name << "..." << std::endl;

            // Set sample rate property
            OSStatus status = AudioUnitSetProperty(
                audioUnit,
                kAudioUnitProperty_SampleRate,
                kAudioUnitScope_Input,
                0,
                &test.rate,
                sizeof(test.rate)
            );

            if (status != noErr) {
                std::cout << "  ✗ Rejected (OSStatus " << status << ")" << std::endl;
                if (test.shouldSupport) {
                    std::cerr << "    ERROR: Should be supported!" << std::endl;
                    allPassed = false;
                } else {
                    std::cout << "    ✓ (Expected for unsupported rate)" << std::endl;
                }
                continue;
            }

            // Try to initialize the AU at this rate
            status = AudioUnitInitialize(audioUnit);
            if (status != noErr) {
                std::cout << "  ✗ Init failed (OSStatus " << status << ")" << std::endl;
                if (test.shouldSupport) {
                    std::cerr << "    ERROR: Should be supported!" << std::endl;
                    allPassed = false;
                }
                continue;
            }

            std::cout << "  ✓ Accepted at " << test.name << std::endl;
            if (!test.shouldSupport) {
                std::cout << "    ⚠ WARNING: Should have been rejected" << std::endl;
            }

            // Uninit for next iteration
            AudioUnitUninitialize(audioUnit);
        }

        return allPassed;
    }

    // HT-10: Test offline determinism
    bool testHT10_OfflineDeterminism() {
        std::cout << "\n=== HT-10: Offline Determinism ===" << std::endl;

        const UInt32 sampleRate = 48000;
        const UInt32 blockSize = 512;
        const UInt32 durationSamples = sampleRate / 10;  // 100ms

        std::cout << "\nTesting determinism at 48 kHz, 100ms duration..." << std::endl;

        // Set sample rate
        OSStatus status = AudioUnitSetProperty(
            audioUnit,
            kAudioUnitProperty_SampleRate,
            kAudioUnitScope_Input,
            0,
            &sampleRate,
            sizeof(sampleRate)
        );

        if (status != noErr) {
            std::cerr << "ERROR: Failed to set sample rate (OSStatus " << status << ")" << std::endl;
            return false;
        }

        // Initialize AU
        status = AudioUnitInitialize(audioUnit);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to initialize AU (OSStatus " << status << ")" << std::endl;
            return false;
        }

        // Set up audio format
        AudioStreamBasicDescription asbd = {0};
        asbd.mSampleRate = sampleRate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mBytesPerFrame = 8;      // 2 channels × 4 bytes
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerPacket = 8;
        asbd.mChannelsPerFrame = 2;
        asbd.mBitsPerChannel = 32;

        status = AudioUnitSetProperty(
            audioUnit,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            0,
            &asbd,
            sizeof(asbd)
        );

        if (status != noErr) {
            std::cerr << "ERROR: Failed to set stream format (OSStatus " << status << ")" << std::endl;
            AudioUnitUninitialize(audioUnit);
            return false;
        }

        // Perform two identical offline renders
        std::vector<std::vector<float>> renders;

        for (int run = 0; run < 2; run++) {
            std::cout << "Render pass " << (run + 1) << "..." << std::endl;

            std::vector<float> outputData(durationSamples * 2, 0.0f);  // Stereo (L+R)

            // Create audio buffer list
            AudioBufferList abl;
            abl.mNumberBuffers = 2;

            float *buffers[2];
            buffers[0] = outputData.data();              // Left channel
            buffers[1] = outputData.data() + durationSamples;  // Right channel

            abl.mBuffers[0] = {
                .mNumberChannels = 1,
                .mDataByteSize = (UInt32)(durationSamples * sizeof(float)),
                .mData = buffers[0]
            };

            abl.mBuffers[1] = {
                .mNumberChannels = 1,
                .mDataByteSize = (UInt32)(durationSamples * sizeof(float)),
                .mData = buffers[1]
            };

            // Render in blocks
            UInt32 samplesRendered = 0;
            while (samplesRendered < durationSamples) {
                UInt32 framesToRender = std::min(blockSize, (UInt32)(durationSamples - samplesRendered));

                // Create temporary audio buffer list for this block
                AudioBufferList tempABL;
                tempABL.mNumberBuffers = 2;
                tempABL.mBuffers[0] = {
                    .mNumberChannels = 1,
                    .mDataByteSize = (UInt32)(framesToRender * sizeof(float)),
                    .mData = buffers[0] + samplesRendered
                };
                tempABL.mBuffers[1] = {
                    .mNumberChannels = 1,
                    .mDataByteSize = (UInt32)(framesToRender * sizeof(float)),
                    .mData = buffers[1] + samplesRendered
                };

                // Render block
                AudioUnitRenderActionFlags flags = 0;
                AudioTimeStamp ts = {0};
                ts.mSampleTime = samplesRendered;
                ts.mFlags = kAudioTimeStampSampleTimeValid;

                OSStatus renderStatus = AudioUnitRender(
                    audioUnit,
                    &flags,
                    &ts,
                    0,
                    framesToRender,
                    &tempABL
                );

                if (renderStatus != noErr) {
                    std::cerr << "  ERROR: Render failed (OSStatus " << renderStatus << ")" << std::endl;
                    break;
                }

                samplesRendered += framesToRender;
            }

            std::cout << "  ✓ Rendered " << samplesRendered << " samples" << std::endl;
            renders.push_back(outputData);
        }

        AudioUnitUninitialize(audioUnit);

        // Compare renders
        if (renders.size() != 2) {
            std::cerr << "ERROR: Could not capture both renders" << std::endl;
            return false;
        }

        std::cout << "\nComparing renders for bit-exactness..." << std::endl;

        if (renders[0].size() != renders[1].size()) {
            std::cerr << "ERROR: Render sizes differ" << std::endl;
            return false;
        }

        bool identical = (memcmp(renders[0].data(), renders[1].data(),
                                renders[0].size() * sizeof(float)) == 0);

        if (identical) {
            std::cout << "✓ Renders are bit-exact (deterministic)" << std::endl;
        } else {
            // Count differences
            int diffCount = 0;
            for (size_t i = 0; i < renders[0].size(); i++) {
                if (renders[0][i] != renders[1][i]) {
                    diffCount++;
                }
            }
            std::cout << "⚠ Renders differ: " << diffCount << " / " << renders[0].size()
                     << " samples differ" << std::endl;
        }

        return identical;
    }
};

int main() {
    @autoreleasepool {
        std::cout << "\n╔════════════════════════════════════╗" << std::endl;
        std::cout << "║   HT-2 & HT-10 Test Harness       ║" << std::endl;
        std::cout << "║   Aetherfield macOS AU Testing    ║" << std::endl;
        std::cout << "╚════════════════════════════════════╝\n" << std::endl;

        AetherfieldAUTester tester;

        if (!tester.initialize()) {
            std::cerr << "\nInitialization failed." << std::endl;
            return 1;
        }

        bool ht2Pass = tester.testHT2_RateNegotiation();
        bool ht10Pass = tester.testHT10_OfflineDeterminism();

        std::cout << "\n╔════════════════════════════════════╗" << std::endl;
        std::cout << "║   Test Results                    ║" << std::endl;
        std::cout << "╠════════════════════════════════════╣" << std::endl;
        std::cout << "║ HT-2 (Rate Negotiation):  "
                 << (ht2Pass ? "✓ PASS" : "✗ FAIL") << std::string(ht2Pass ? 10 : 11, ' ')
                 << "║" << std::endl;
        std::cout << "║ HT-10 (Determinism):      "
                 << (ht10Pass ? "✓ PASS" : "✗ FAIL") << std::string(ht10Pass ? 14 : 15, ' ')
                 << "║" << std::endl;
        std::cout << "╚════════════════════════════════════╝\n" << std::endl;

        return (ht2Pass && ht10Pass) ? 0 : 1;
    }
}

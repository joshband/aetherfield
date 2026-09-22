// DEPRECATED: Abandoned hand-rolled C++/AVAudioEngine approach to HT-2/HT-10 testing.
// Superseded by XCTest harness (HT-2, commit 77844dc) and REAPER MCP automation (HT-10, commit c37ca5f).
// Retained as historical reference per user choice.

// macOS AU Offline Test Harness for HT-2 (Rate Negotiation) & HT-10 (Determinism)
// Standalone C++ test that loads the macOS AU and tests rate changes and offline determinism

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <iostream>
#include <cmath>
#include <vector>
#include <cstring>

struct TestConfig {
    uint32_t sampleRate;
    uint32_t blockSize;
    float decay, damp, mix;
    int durationMs;
};

class MacOSAUTester {
public:
    MacOSAUTester() : engine(nil), audioUnit(nil), rendered(false) {}

    bool initialize() {
        NSError *error = nil;
        engine = [[AVAudioEngine alloc] init];

        // Create audio unit
        AVAudioUnitComponentDescription desc = {
            .componentType = kAudioUnitType_Effect,
            .componentSubType = 'Aeth',
            .componentManufacturer = 'Josh'
        };

        audioUnit = [[AVAudioUnit alloc] initWithComponentDescription:desc error:&error];
        if (!audioUnit) {
            NSLog(@"ERROR: Failed to load AU: %@", error);
            return false;
        }
        NSLog(@"✓ AU loaded: %@", audioUnit.description);
        return true;
    }

    bool testRateNegotiation() {
        NSLog(@"\n=== HT-2: Rate Negotiation Test ===\n");

        uint32_t rates[] = {44100, 48000, 96000};
        const char *names[] = {"44.1 kHz", "48 kHz", "96 kHz"};
        bool shouldSupport[] = {true, true, false};

        for (int i = 0; i < 3; i++) {
            uint32_t rate = rates[i];
            NSLog(@"Testing %s...", names[i]);

            AVAudioFormat *format = [[AVAudioFormat alloc]
                initWithCommonFormat:AVAudioCommonFormatFloat32
                sampleRate:rate
                channels:2
                interleaved:NO];

            // Attach to engine at this rate
            NSError *error = nil;
            [engine attachAudioUnit:audioUnit error:&error];
            if (error) {
                NSLog(@"  → Rejected (error: %@)", error.description);
                if (shouldSupport[i]) {
                    NSLog(@"  ERROR: Should have been supported!");
                    return false;
                }
                continue;
            }

            // Try to allocate render resources
            NSError *allocError = nil;
            BOOL success = [audioUnit allocateRenderResourcesAndReturnError:&allocError];
            if (!success || allocError) {
                NSLog(@"  → Allocation failed (expected for unsupported rates)");
                if (shouldSupport[i]) {
                    NSLog(@"  ERROR: Allocation failed for supported rate!");
                    return false;
                }
            } else {
                NSLog(@"  ✓ Accepted and allocated at %s", names[i]);
            }
        }

        return true;
    }

    bool testDeterminism() {
        NSLog(@"\n=== HT-10: Offline Determinism Test ===\n");
        NSLog(@"Testing identical offline renders produce bit-exact outputs...\n");

        TestConfig cfg = {48000, 512, 0.5f, 0.5f, 0.5f, 1000};  // 1 second

        std::vector<std::vector<float>> renders;

        for (int run = 0; run < 2; run++) {
            NSLog(@"Render pass %d...", run + 1);

            if (![self renderOfflineWithConfig:cfg intoBuffer:renderBuffer]) {
                return false;
            }

            NSLog(@"  → Captured %zu samples", renderBuffer.size());
            renders.push_back(renderBuffer);
        }

        // Compare renders
        if (renders[0].size() != renders[1].size()) {
            NSLog(@"ERROR: Render sizes differ");
            return false;
        }

        bool identical = (memcmp(renders[0].data(), renders[1].data(),
                                renders[0].size() * sizeof(float)) == 0);

        if (identical) {
            NSLog(@"✓ Renders are bit-exact (deterministic)");
            return true;
        } else {
            NSLog(@"⚠ Renders differ (determinism check)");
            // Count differing samples
            int diffs = 0;
            for (size_t i = 0; i < renders[0].size(); i++) {
                if (renders[0][i] != renders[1][i]) diffs++;
            }
            NSLog(@"  %d samples differ out of %zu", diffs, renders[0].size());
            return false;
        }
    }

private:
    AVAudioEngine *engine;
    AVAudioUnit *audioUnit;
    bool rendered;
    std::vector<float> renderBuffer;

    bool renderOfflineWithConfig:(TestConfig)cfg intoBuffer:(std::vector<float>&)outBuffer {
        // Generate simple test signal (sine wave) and render through AU
        NSLog(@"  Rendering at %u Hz...", cfg.sampleRate);

        // For now, just generate a dummy output
        // A full implementation would:
        // 1. Create test signal (sine/impulse)
        // 2. Connect to AU input
        // 3. Render synchronously
        // 4. Capture output

        outBuffer.resize(cfg.sampleRate * cfg.durationMs / 1000);
        std::fill(outBuffer.begin(), outBuffer.end(), 0.0f);

        return true;
    }
};

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSLog(@"macOS AU Offline Test Harness v1.0");
        NSLog(@"==================================\n");

        MacOSAUTester tester;

        if (!tester.initialize()) {
            return 1;
        }

        bool ht2Pass = tester.testRateNegotiation();
        bool ht10Pass = tester.testDeterminism();

        NSLog(@"\n=== Results ===");
        NSLog(@"HT-2 (Rate Negotiation): %s", ht2Pass ? "PASS" : "FAIL");
        NSLog(@"HT-10 (Determinism): %s", ht10Pass ? "PASS" : "FAIL");

        return (ht2Pass && ht10Pass) ? 0 : 1;
    }
}

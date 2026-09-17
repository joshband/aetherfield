#include "dsp/DelayLine.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

std::size_t allocationCount = 0;

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

// Runs an impulse-at-index-0 check against a delay line freshly prepared
// (or already prepared) with maxDelaySamples == delaySamples, processed in
// a single block of delaySamples + 1 samples. Returns true on success.
bool checkImpulseAtZero(aetherfield::dsp::DelayLine& line, std::size_t delaySamples) {
    std::vector<float> input(delaySamples + 1, 0.0F);
    std::vector<float> output(delaySamples + 1, 0.0F);
    input[0] = 1.0F;

    line.process(input.data(), output.data(), input.size());

    for (std::size_t index = 0; index < output.size(); ++index) {
        const float expected = (index == delaySamples) ? 1.0F : 0.0F;
        if (output[index] != expected) {
            return false;
        }
    }
    return true;
}

} // namespace

// Global operator new/delete overrides for testAllocationOccursOnlyDuringPreparation.
// Test-only instrumentation, not part of DelayLine.
void* operator new(std::size_t size) {
    ++allocationCount;
    return std::malloc(size);
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

namespace {

int testExactImpulsePosition() {
    aetherfield::dsp::DelayLine line;
    if (!line.prepare(48000.0, 5)) {
        return fail("prepare(48000.0, 5) should succeed");
    }
    if (!checkImpulseAtZero(line, 5)) {
        return fail("impulse at index 0 did not reappear exactly at index 5");
    }
    return 0;
}

int testWraparoundAcrossMultipleCycles() {
    constexpr std::size_t delaySamples = 5;
    constexpr std::size_t totalLength = 21; // >= 3*N + 1, with room for the last echo
    const std::array<std::size_t, 5> blockSizes {3, 4, 3, 6, 5}; // sums to 21; none divide 5 evenly except by chance

    aetherfield::dsp::DelayLine line;
    if (!line.prepare(48000.0, delaySamples)) {
        return fail("prepare(48000.0, 5) should succeed");
    }

    std::vector<float> input(totalLength, 0.0F);
    std::vector<float> output(totalLength, 0.0F);
    const std::array<std::size_t, 3> impulseIndices {0, 5, 11};
    for (std::size_t index : impulseIndices) {
        input[index] = 1.0F;
    }

    std::size_t offset = 0;
    for (std::size_t blockSize : blockSizes) {
        line.process(input.data() + offset, output.data() + offset, blockSize);
        offset += blockSize;
    }
    if (offset != totalLength) {
        return fail("test setup error: block sizes do not sum to totalLength");
    }

    for (std::size_t index = 0; index < totalLength; ++index) {
        bool isEcho = false;
        for (std::size_t impulseIndex : impulseIndices) {
            if (index == impulseIndex + delaySamples) {
                isEcho = true;
                break;
            }
        }
        const float expected = isEcho ? 1.0F : 0.0F;
        if (output[index] != expected) {
            return fail("wraparound run produced an echo at the wrong index, or missed one");
        }
    }
    return 0;
}

int testPrepareRejectsZeroMaxDelay() {
    aetherfield::dsp::DelayLine line;
    if (line.prepare(48000.0, 0)) {
        return fail("prepare with maxDelaySamples == 0 should return false");
    }
    return 0;
}

int testPrepareRejectsNonPositiveOrNonFiniteSampleRate() {
    aetherfield::dsp::DelayLine zero;
    if (zero.prepare(0.0, 64)) {
        return fail("prepare with sampleRate == 0.0 should return false");
    }

    aetherfield::dsp::DelayLine negative;
    if (negative.prepare(-48000.0, 64)) {
        return fail("prepare with a negative sampleRate should return false");
    }

    aetherfield::dsp::DelayLine notFinite;
    if (notFinite.prepare(std::nan(""), 64)) {
        return fail("prepare with a NaN sampleRate should return false");
    }
    return 0;
}

int testInvalidReprepareDoesNotDisturbPriorState() {
    aetherfield::dsp::DelayLine line;
    if (!line.prepare(48000.0, 5)) {
        return fail("prepare(48000.0, 5) should succeed");
    }
    if (!checkImpulseAtZero(line, 5)) {
        return fail("initial impulse-position check failed before the invalid re-prepare");
    }

    if (line.prepare(48000.0, 0)) {
        return fail("prepare with maxDelaySamples == 0 should return false, even when already prepared");
    }

    // The prior 5-sample preparation must remain valid and usable.
    line.reset();
    if (!checkImpulseAtZero(line, 5)) {
        return fail("prior 5-sample preparation was disturbed by the rejected re-prepare");
    }
    return 0;
}

int testResetIsRepeatable() {
    aetherfield::dsp::DelayLine line;
    if (!line.prepare(48000.0, 4)) {
        return fail("prepare(48000.0, 4) should succeed");
    }

    auto runResetSequence = [&line]() -> std::vector<float> {
        std::vector<float> nonZero {1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
        std::vector<float> discard(nonZero.size(), 0.0F);
        line.process(nonZero.data(), discard.data(), nonZero.size());

        line.reset();

        std::vector<float> zeroInput(4, 0.0F);
        std::vector<float> output(4, 0.0F);
        line.process(zeroInput.data(), output.data(), zeroInput.size());
        return output;
    };

    const std::vector<float> firstRun = runResetSequence();
    for (float sample : firstRun) {
        if (sample != 0.0F) {
            return fail("silence-in did not produce silence-out immediately after reset");
        }
    }

    const std::vector<float> secondRun = runResetSequence();
    if (firstRun != secondRun) {
        return fail("repeated reset/process sequences were not byte-identical");
    }
    return 0;
}

int testBufferExtentsRespected() {
    constexpr float sentinel = 123.0F;

    // count < maxDelaySamples
    {
        aetherfield::dsp::DelayLine line;
        if (!line.prepare(48000.0, 8)) {
            return fail("prepare(48000.0, 8) should succeed");
        }
        constexpr std::size_t count = 3;
        std::vector<float> input(count + 1, 1.0F);
        std::vector<float> output(count + 1, 0.0F);
        input[count] = sentinel;
        output[count] = sentinel;

        line.process(input.data(), output.data(), count);

        if (input[count] != sentinel) {
            return fail("process() read past the requested count in the input buffer");
        }
        if (output[count] != sentinel) {
            return fail("process() wrote past the requested count in the output buffer");
        }
    }

    // count > maxDelaySamples
    {
        aetherfield::dsp::DelayLine line;
        if (!line.prepare(48000.0, 3)) {
            return fail("prepare(48000.0, 3) should succeed");
        }
        constexpr std::size_t count = 8;
        std::vector<float> input(count + 1, 1.0F);
        std::vector<float> output(count + 1, 0.0F);
        input[count] = sentinel;
        output[count] = sentinel;

        line.process(input.data(), output.data(), count);

        if (input[count] != sentinel) {
            return fail("process() read past the requested count in the input buffer");
        }
        if (output[count] != sentinel) {
            return fail("process() wrote past the requested count in the output buffer");
        }
    }
    return 0;
}

int testZeroFrameCallSafe() {
    aetherfield::dsp::DelayLine line;

    // Before prepare(): must be a safe no-op even though the object is unprepared.
    line.process(nullptr, nullptr, 0);

    if (!line.prepare(48000.0, 5)) {
        return fail("prepare(48000.0, 5) should succeed");
    }

    // After prepare(): must also be a safe no-op, and must not disturb state.
    line.process(nullptr, nullptr, 0);

    if (!checkImpulseAtZero(line, 5)) {
        return fail("zero-frame call after prepare() disturbed subsequent processing");
    }
    return 0;
}

int testAllocationOccursOnlyDuringPreparation() {
    const std::size_t beforeConstruct = allocationCount;
    aetherfield::dsp::DelayLine line;
    if (allocationCount != beforeConstruct) {
        return fail("construction allocated");
    }

    const std::size_t beforeFirstPrepare = allocationCount;
    if (!line.prepare(48000.0, 8)) {
        return fail("prepare(48000.0, 8) should succeed");
    }
    if (allocationCount == beforeFirstPrepare) {
        return fail("prepare() did not allocate");
    }

    const std::size_t beforeReset = allocationCount;
    line.reset();
    if (allocationCount != beforeReset) {
        return fail("reset() allocated");
    }

    // Multi-block process() run of several thousand samples across varied block sizes.
    constexpr std::size_t totalSamples = 4096;
    std::vector<float> input(totalSamples, 0.25F);
    std::vector<float> output(totalSamples, 0.0F);
    const std::array<std::size_t, 4> blockSizes {64, 128, 256, 512};

    const std::size_t beforeProcessLoop = allocationCount;
    std::size_t offset = 0;
    std::size_t blockIndex = 0;
    while (offset < totalSamples) {
        std::size_t blockSize = blockSizes[blockIndex % blockSizes.size()];
        if (blockSize > totalSamples - offset) {
            blockSize = totalSamples - offset;
        }
        line.process(input.data() + offset, output.data() + offset, blockSize);
        offset += blockSize;
        ++blockIndex;
    }
    if (allocationCount != beforeProcessLoop) {
        return fail("process() allocated during a multi-block run");
    }

    // Re-prepare, simulating a sample-rate change: must allocate again.
    const std::size_t beforeReprepare = allocationCount;
    if (!line.prepare(44100.0, 16)) {
        return fail("re-prepare(44100.0, 16) should succeed");
    }
    if (allocationCount == beforeReprepare) {
        return fail("re-prepare() did not allocate");
    }

    return 0;
}

} // namespace

int main() {
    if (int result = testExactImpulsePosition(); result != 0) {
        return result;
    }
    if (int result = testWraparoundAcrossMultipleCycles(); result != 0) {
        return result;
    }
    if (int result = testPrepareRejectsZeroMaxDelay(); result != 0) {
        return result;
    }
    if (int result = testPrepareRejectsNonPositiveOrNonFiniteSampleRate(); result != 0) {
        return result;
    }
    if (int result = testInvalidReprepareDoesNotDisturbPriorState(); result != 0) {
        return result;
    }
    if (int result = testResetIsRepeatable(); result != 0) {
        return result;
    }
    if (int result = testBufferExtentsRespected(); result != 0) {
        return result;
    }
    if (int result = testZeroFrameCallSafe(); result != 0) {
        return result;
    }
    if (int result = testAllocationOccursOnlyDuringPreparation(); result != 0) {
        return result;
    }
    return 0;
}

#include "dsp/DiffusionStereoPath.h"
#include "dsp/FeedbackDelayNetwork.h"
#include "dsp/ParameterAutomation.h"
#include "dsp/SchroederAllpass.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <limits>
#include <new>

namespace {

std::atomic_size_t gAllocationCount {0};

void* allocateOrThrow(std::size_t size) {
    if (void* allocation = std::malloc(size)) {
        gAllocationCount.fetch_add(1, std::memory_order_relaxed);
        return allocation;
    }
    throw std::bad_alloc {};
}

} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace {

using aetherfield::dsp::DiffusionStereoConfig;
using aetherfield::dsp::DiffusionStereoPath;
using aetherfield::dsp::FeedbackDelayNetwork;
using aetherfield::dsp::ParameterAutomation;
using aetherfield::dsp::SchroederAllpass;

constexpr double kTMin = 0.027;
constexpr double kTMax = 0.081;
constexpr double kDMaxFixture = 48.0;
constexpr double kGoldenCoefficient = 0.6180339887498948482;

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

DiffusionStereoConfig validConfig(double sampleRate) {
    return {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = kTMin,
        .fdnMaxDelaySeconds = kTMax,
        .t60ZeroSeconds = 4.0,
        .t60PiSeconds = 4.0,
        .dMaxDb = kDMaxFixture,
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = kGoldenCoefficient,
    };
}

bool matchesDelays(const DiffusionStereoPath& path,
                   const std::array<std::size_t, 4>& input,
                   const std::array<std::size_t, 2>& left,
                   const std::array<std::size_t, 2>& right) {
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (path.inputDelaySamples(index) != input[index]) return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (path.leftOutputDelaySamples(index) != left[index]) return false;
        if (path.rightOutputDelaySamples(index) != right[index]) return false;
    }
    return true;
}

bool baselineIsIntact(const DiffusionStereoPath& path, float coefficient) {
    return path.isPrepared()
        && matchesDelays(path, {47, 103, 223, 479}, {191, 307}, {241, 383})
        && path.allpassCoefficient() == coefficient
        && path.nonFiniteCount() == 0
        && !path.nonFiniteLatched()
        && !path.resetPending();
}

int testUnpreparedLifecycle() {
    DiffusionStereoPath path;
    path.reset();
    if (path.isPrepared() || path.nonFiniteCount() != 0 || path.nonFiniteLatched()
        || path.resetPending() || path.allpassCoefficient() != 0.0F) {
        return fail("unprepared reset changed wrapper lifecycle state");
    }
    return 0;
}

int testPreparationAtBothFixtureRates() {
    struct Case {
        double rate;
        std::array<std::size_t, 4> input;
        std::array<std::size_t, 2> left;
        std::array<std::size_t, 2> right;
    };
    const std::array<Case, 2> cases {{
        {48000.0, {47, 103, 223, 479}, {191, 307}, {241, 383}},
        {44100.0, {43, 97, 199, 439}, {173, 281}, {223, 353}},
    }};

    for (const Case& testCase : cases) {
        DiffusionStereoPath path;
        if (!path.prepare(validConfig(testCase.rate))) return fail("valid fixture preparation rejected");
        if (!path.isPrepared() || !matchesDelays(path, testCase.input, testCase.left, testCase.right)) {
            return fail("time-domain allpass preparation derived an unexpected delay");
        }
        if (path.allpassCoefficient() != static_cast<float>(kGoldenCoefficient)
            || path.nonFiniteCount() != 0 || path.nonFiniteLatched() || path.resetPending()) {
            return fail("valid preparation did not establish clean aggregate state");
        }
        path.reset();
        if (!path.isPrepared() || !matchesDelays(path, testCase.input, testCase.left, testCase.right)
            || path.nonFiniteCount() != 0 || path.nonFiniteLatched() || path.resetPending()) {
            return fail("reset did not preserve configuration and clean aggregate state");
        }
    }
    return 0;
}

int testInvalidPreparationRollsBackLiveConfiguration() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("baseline preparation failed");
    const std::array<std::size_t, 4> input {47, 103, 223, 479};
    const std::array<std::size_t, 2> left {191, 307};
    const std::array<std::size_t, 2> right {241, 383};
    const float coefficient = path.allpassCoefficient();

    std::array<DiffusionStereoConfig, 5> invalid {
        validConfig(48000.0), validConfig(48000.0), validConfig(48000.0),
        validConfig(48000.0), validConfig(48000.0),
    };
    invalid[0].sampleRate = std::numeric_limits<double>::quiet_NaN();
    invalid[1].inputDelaySeconds[2] = 0.0;
    invalid[2].allpassCoefficient = 0.7;
    invalid[3].lineCount = 3;
    invalid[4].dMaxDb = std::numeric_limits<double>::quiet_NaN();
    for (const DiffusionStereoConfig& config : invalid) {
        if (path.prepare(config)) return fail("invalid preparation accepted");
        if (!path.isPrepared() || !matchesDelays(path, input, left, right)
            || path.allpassCoefficient() != coefficient || path.nonFiniteCount() != 0
            || path.nonFiniteLatched() || path.resetPending()) {
            return fail("invalid preparation disturbed the live wrapper configuration");
        }
    }
    return 0;
}

int testTopologyValidationRollsBackAfterCandidatePreparation() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("baseline preparation failed");
    const float coefficient = path.allpassCoefficient();

    std::array<DiffusionStereoConfig, 4> invalid {
        validConfig(48000.0), validConfig(48000.0), validConfig(48000.0), validConfig(48000.0),
    };
    invalid[0].inputDelaySeconds[1] = invalid[0].inputDelaySeconds[0];
    invalid[1].rightOutputDelaySeconds[0] = 0.003;
    invalid[2].lineCount = 4;
    invalid[2].fdnMinDelaySeconds = 0.001;
    invalid[2].fdnMaxDelaySeconds = 0.010;
    invalid[3].lineCount = 4;
    invalid[3].fdnMinDelaySeconds = 0.005;
    invalid[3].fdnMaxDelaySeconds = 0.020;

    for (const DiffusionStereoConfig& config : invalid) {
        if (path.prepare(config)) return fail("invalid realized diffusion topology accepted");
        if (!baselineIsIntact(path, coefficient)) {
            return fail("post-candidate topology rejection disturbed live configuration");
        }
    }
    return 0;
}

int testFdnTargetDomainRejectsBeforeCandidateAllocation() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("baseline preparation failed");
    const float coefficient = path.allpassCoefficient();
    DiffusionStereoConfig invalid = validConfig(48000.0);
    invalid.fdnMaxDelaySeconds = std::numeric_limits<double>::max();

    const std::size_t allocationsBefore = gAllocationCount.load(std::memory_order_relaxed);
    if (path.prepare(invalid)) return fail("out-of-domain FDN target accepted");
    const std::size_t allocationsAfter = gAllocationCount.load(std::memory_order_relaxed);
    if (allocationsAfter != allocationsBefore) {
        return fail("out-of-domain FDN target allocated before wrapper rejection");
    }
    if (!baselineIsIntact(path, coefficient)) {
        return fail("out-of-domain FDN rejection disturbed live configuration");
    }
    return 0;
}

int testWrapperExposesReadOnlyPreStepTaps() {
    DiffusionStereoPath path;
    const auto unprepared = path.preStepTapSums();
    if (unprepared.even != 0.0F || unprepared.odd != 0.0F) {
        return fail("unprepared wrapper pre-step taps were not silent");
    }
    if (!path.prepare(validConfig(48000.0))) return fail("wrapper pre-step tap fixture preparation failed");
    const DiffusionStereoPath& view = path;
    const auto first = view.preStepTapSums();
    const auto repeated = view.preStepTapSums();
    if (first.even != 0.0F || first.odd != 0.0F
        || repeated.even != first.even || repeated.odd != first.odd) {
        return fail("wrapper pre-step taps changed prepared reset state");
    }
    return 0;
}

struct OrderedReferencePath {
    std::array<SchroederAllpass, 4> input;
    std::array<SchroederAllpass, 2> leftOutput;
    std::array<SchroederAllpass, 2> rightOutput;
    FeedbackDelayNetwork network;
    ParameterAutomation automation;

    bool prepare(const DiffusionStereoConfig& config) {
        const float coefficient = static_cast<float>(config.allpassCoefficient);
        for (std::size_t index = 0; index < input.size(); ++index) {
            if (!input[index].prepare(config.sampleRate, config.inputDelaySeconds[index], coefficient)) return false;
        }
        for (std::size_t index = 0; index < leftOutput.size(); ++index) {
            if (!leftOutput[index].prepare(config.sampleRate, config.leftOutputDelaySeconds[index], coefficient)
                || !rightOutput[index].prepare(config.sampleRate, config.rightOutputDelaySeconds[index], coefficient)) {
                return false;
            }
        }
        return network.prepare(config.sampleRate, config.lineCount, config.fdnMinDelaySeconds,
                               config.fdnMaxDelaySeconds, config.t60ZeroSeconds, config.t60PiSeconds)
            && automation.prepare(network, config.sampleRate, config.dMaxDb);
    }

    aetherfield::dsp::StereoSample processSample(float mono) {
        automation.checkForNewTargets();
        const auto mix = automation.advance(network);

        float diffused = std::isfinite(mono) ? mono : 0.0F;
        for (auto& section : input) diffused = section.processSample(diffused).value;

        const float inputScale = 1.0F / std::sqrt(static_cast<float>(network.lineCount()));
        const float injection = diffused * inputScale;
        const auto taps = network.preStepTapSums();
        network.processSample(injection);

        const float tapScale = 1.0F / std::sqrt(static_cast<float>(network.lineCount() / 2));
        float left = taps.even * tapScale;
        float right = taps.odd * tapScale;
        for (auto& section : leftOutput) left = section.processSample(left).value;
        for (auto& section : rightOutput) right = section.processSample(right).value;
        return {mix.dry * mono + mix.wet * left, mix.dry * mono + mix.wet * right, false};
    }
};

int testProcessSampleMatchesExactOrderedReference() {
    DiffusionStereoPath path;
    OrderedReferencePath reference;
    const DiffusionStereoConfig config = validConfig(48000.0);
    if (!path.prepare(config) || !reference.prepare(config)) {
        return fail("ordered-routing fixture preparation failed");
    }

    constexpr std::size_t kSamples = 4096;
    for (std::size_t index = 0; index < kSamples; ++index) {
        const float input = index == 0 ? 1.0F : 0.0F;
        const auto actual = path.processSample(input);
        const auto expected = reference.processSample(input);
        if (actual.left != expected.left || actual.right != expected.right || actual.nonFinite != expected.nonFinite) {
            std::cerr << "first mismatch at sample " << index
                      << std::hexfloat << ": actual=(" << actual.left << ", " << actual.right << ")"
                      << " expected=(" << expected.left << ", " << expected.right << ")\n" << std::defaultfloat;
            return fail("wrapper processSample did not preserve the required diffusion/FDN/tap/Mix order");
        }
    }
    return 0;
}

int testProcessSampleDoesNotAllocate() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("allocation fixture preparation failed");

    constexpr std::size_t kWarmupSamples = 128;
    for (std::size_t index = 0; index < kWarmupSamples; ++index) {
        static_cast<void>(path.processSample(index == 0 ? 1.0F : 0.0F));
    }
    const std::size_t allocationsBefore = gAllocationCount.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < 1024; ++index) {
        static_cast<void>(path.processSample(0.0F));
    }
    if (gAllocationCount.load(std::memory_order_relaxed) != allocationsBefore) {
        return fail("wrapper processSample allocated on the render path");
    }

    std::array<float, 1024> input {};
    std::array<float, 1024> left {};
    std::array<float, 1024> right {};
    const std::size_t blockAllocationsBefore = gAllocationCount.load(std::memory_order_relaxed);
    path.process(input.data(), left.data(), right.data(), input.size());
    if (gAllocationCount.load(std::memory_order_relaxed) != blockAllocationsBefore) {
        return fail("wrapper process allocated on the render path");
    }
    return 0;
}

int testFixedParameterBlockPartitionsAreBitIdentical() {
    constexpr std::size_t kFrames = 4099;
    std::array<float, kFrames> input {};
    input[0] = 1.0F;
    input[173] = -0.25F;
    input[2048] = 0.125F;

    DiffusionStereoPath whole;
    DiffusionStereoPath partitioned;
    if (!whole.prepare(validConfig(48000.0)) || !partitioned.prepare(validConfig(48000.0))) {
        return fail("partition fixture preparation failed");
    }

    std::array<float, kFrames> wholeLeft {};
    std::array<float, kFrames> wholeRight {};
    std::array<float, kFrames> partitionedLeft {};
    std::array<float, kFrames> partitionedRight {};
    whole.process(input.data(), wholeLeft.data(), wholeRight.data(), input.size());

    constexpr std::array<std::size_t, 5> partitions {1, 13, 64, 512, 3};
    std::size_t offset = 0;
    std::size_t partitionIndex = 0;
    while (offset < input.size()) {
        const std::size_t count = std::min(partitions[partitionIndex], input.size() - offset);
        partitioned.process(input.data() + offset, partitionedLeft.data() + offset,
                            partitionedRight.data() + offset, count);
        offset += count;
        partitionIndex = (partitionIndex + 1) % partitions.size();
    }

    if (wholeLeft != partitionedLeft || wholeRight != partitionedRight) {
        return fail("fixed-parameter block partition changed diffusion/stereo output");
    }
    return 0;
}

int testBlockFaultAggregationAndDeferredRecovery() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("fault recovery fixture preparation failed");

    const float faultingInput[] {std::numeric_limits<float>::quiet_NaN()};
    float left = 0.0F;
    float right = 0.0F;
    path.process(faultingInput, &left, &right, 1);
    const std::size_t countAfterFault = path.nonFiniteCount();
    if (countAfterFault != 1 || !path.nonFiniteLatched() || !path.resetPending()) {
        return fail("wrapper head NaN was not aggregated and deferred to block recovery");
    }

    path.process(nullptr, nullptr, nullptr, 0);
    if (path.nonFiniteCount() != countAfterFault || !path.nonFiniteLatched() || !path.resetPending()) {
        return fail("zero-frame block consumed a pending reset");
    }

    const float silentInput[] {0.0F};
    path.process(silentInput, &left, &right, 1);
    if (left != 0.0F || right != 0.0F || path.nonFiniteCount() != countAfterFault
        || path.nonFiniteLatched() || path.resetPending()) {
        return fail("next nonempty block did not reset the full path before rendering silence");
    }

    path.process(silentInput, &left, &right, 1);
    if (left != 0.0F || right != 0.0F || path.nonFiniteCount() != countAfterFault
        || path.nonFiniteLatched() || path.resetPending()) {
        return fail("repeated post-recovery block changed recovered aggregate state");
    }
    return 0;
}

int testInputAllpassFaultIsObservedBeforeFdnPropagation() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("input allpass fault fixture preparation failed");

    constexpr std::size_t kFrames = 48;
    std::array<float, kFrames> input {};
    input.fill(std::numeric_limits<float>::max());
    std::array<float, kFrames> left {};
    std::array<float, kFrames> right {};
    path.process(input.data(), left.data(), right.data(), input.size());
    if (path.nonFiniteCount() == 0 || !path.nonFiniteLatched() || !path.resetPending()) {
        return fail("input allpass fault was not observed at the wrapper boundary");
    }
    return 0;
}

int testSaturatedFdnLatchTransitionAddsOneAggregateObservation() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("saturated FDN fixture preparation failed");
    path.setFdnNonFiniteStateForTest(std::numeric_limits<std::size_t>::max(), false);

    constexpr std::size_t kFrames = 48;
    std::array<float, kFrames> input {};
    input.fill(std::numeric_limits<float>::max());
    std::array<float, kFrames> left {};
    std::array<float, kFrames> right {};
    path.process(input.data(), left.data(), right.data(), input.size());

    // The first recursive allpass overflow propagates through all four input
    // stages. The already-saturated FDN cannot expose an increment, so its
    // false-to-true latch is one additional, non-exact aggregate observation.
    if (path.nonFiniteCount() != 5 || !path.nonFiniteLatched() || !path.resetPending()) {
        return fail("saturated FDN latch transition was not aggregated exactly once");
    }
    return 0;
}

} // namespace

int main() {
    if (testUnpreparedLifecycle() != 0) return 1;
    if (testPreparationAtBothFixtureRates() != 0) return 1;
    if (testInvalidPreparationRollsBackLiveConfiguration() != 0) return 1;
    if (testTopologyValidationRollsBackAfterCandidatePreparation() != 0) return 1;
    if (testFdnTargetDomainRejectsBeforeCandidateAllocation() != 0) return 1;
    if (testWrapperExposesReadOnlyPreStepTaps() != 0) return 1;
    if (testProcessSampleMatchesExactOrderedReference() != 0) return 1;
    if (testProcessSampleDoesNotAllocate() != 0) return 1;
    if (testFixedParameterBlockPartitionsAreBitIdentical() != 0) return 1;
    if (testBlockFaultAggregationAndDeferredRecovery() != 0) return 1;
    if (testInputAllpassFaultIsObservedBeforeFdnPropagation() != 0) return 1;
    if (testSaturatedFdnLatchTransitionAddsOneAggregateObservation() != 0) return 1;
    std::cout << "DiffusionStereoPath Task 1/2b/3 tests passed\n";
    return 0;
}

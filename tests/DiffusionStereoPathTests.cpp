#include "dsp/DiffusionStereoPath.h"
#include "dsp/FeedbackDelayNetwork.h"
#include "dsp/ParameterAutomation.h"
#include "dsp/SchroederAllpass.h"

#include "DiffusionStereoAnalysis.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <vector>

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

// ---- Task 4a bracket infrastructure: ADR-006 (c)'s generalized rule built
// directly from SchroederAllpass, never through DiffusionStereoConfig (which
// stays fixed at K_in=4/K_out=2). Shared by the DS-1/2/3/5/6/12 bracket tests
// below. ----

using aetherfield::dsp_test::Complex;
using aetherfield::dsp_test::geometricTargets;
using aetherfield::dsp_test::hasMinimumWelchSegments;
using aetherfield::dsp_test::hasNonzeroSample;
using aetherfield::dsp_test::isPrime;
using aetherfield::dsp_test::nextPowerOfTwo;
using aetherfield::dsp_test::radix2Fft;

constexpr std::array<double, 2> kBracketRates {48000.0, 44100.0};
constexpr double kInputWindowMinSeconds = 0.001;
constexpr double kInputWindowMaxSeconds = 0.010;
constexpr double kOutputWindowMinSeconds = 0.004;
constexpr double kOutputWindowMaxSeconds = 0.008;
constexpr std::array<std::size_t, 4> kBracketKIn {2, 3, 4, 5};
constexpr std::array<std::size_t, 2> kBracketKOut {1, 2};

struct CascadeSample {
    float value;
    bool nonFinite;
};

// Builds and prepares a series cascade of `targetSeconds.size()` sections at
// the given geometrically spaced time-domain targets. Returns an empty
// vector if any section's preparation fails.
std::vector<SchroederAllpass> buildCascade(double sampleRate, const std::vector<double>& targetSeconds,
                                            double coefficient) {
    std::vector<SchroederAllpass> cascade(targetSeconds.size());
    for (std::size_t index = 0; index < targetSeconds.size(); ++index) {
        if (!cascade[index].prepare(sampleRate, targetSeconds[index], coefficient)) return {};
    }
    return cascade;
}

std::vector<std::size_t> cascadeDelays(const std::vector<SchroederAllpass>& cascade) {
    std::vector<std::size_t> delays(cascade.size());
    for (std::size_t index = 0; index < cascade.size(); ++index) delays[index] = cascade[index].delaySamples();
    return delays;
}

CascadeSample runCascadeSample(std::vector<SchroederAllpass>& cascade, float input) noexcept {
    float value = input;
    bool nonFinite = false;
    for (auto& section : cascade) {
        const auto sample = section.processSample(value);
        value = sample.value;
        nonFinite = nonFinite || sample.nonFinite;
    }
    return {value, nonFinite};
}

bool buildFdnFixture(double sampleRate, FeedbackDelayNetwork& fdn) {
    return fdn.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 4.0);
}

// The 2*K_out output-window targets, in increasing-time order, split even
// (channel L) / odd (channel R) exactly as ADR-006 (c) specifies. K_out == 1
// degenerates to one point per channel, as ADR-006 (c) notes it must.
struct OutputWindow {
    std::vector<double> left;
    std::vector<double> right;
};

OutputWindow buildOutputWindow(std::size_t kOut) {
    const std::vector<double> all = geometricTargets(kOutputWindowMinSeconds, kOutputWindowMaxSeconds, 2 * kOut);
    OutputWindow window;
    for (std::size_t index = 0; index < all.size(); ++index) {
        (index % 2 == 0 ? window.left : window.right).push_back(all[index]);
    }
    return window;
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

// ---------------------------------------------------------------------------
// Task 4a: anti-vacuity infrastructure.
// ---------------------------------------------------------------------------

int testAntiVacuityInfrastructure() {
    const std::vector<float> silence(4096, 0.0F);
    if (hasNonzeroSample(silence)) return fail("anti-vacuity guard accepted an all-silent fixture");
    std::vector<float> withSignal(4096, 0.0F);
    withSignal[2048] = 1.0e-3F;
    if (!hasNonzeroSample(withSignal)) return fail("anti-vacuity guard rejected a fixture with a real nonzero sample");

    if (hasMinimumWelchSegments(0) || hasMinimumWelchSegments(1)) {
        return fail("anti-vacuity guard accepted a single-segment coherence estimate");
    }
    if (!hasMinimumWelchSegments(2) || !hasMinimumWelchSegments(3)) {
        return fail("anti-vacuity guard rejected a valid multi-segment coherence estimate");
    }
    std::cout << "Anti-vacuity infrastructure: rejects all-silent fixtures and single-segment "
                 "coherence estimates; accepts a real signal and >=2-segment estimates\n";
    return 0;
}

// ---------------------------------------------------------------------------
// DS-1 — coefficient and pole bound, across the full bracket.
// ---------------------------------------------------------------------------

int testDs1CoefficientAndPoleBoundAcrossBracket() {
    double maxPoleRadius = 0.0;
    std::size_t sectionsChecked = 0;

    auto checkChain = [&](std::vector<SchroederAllpass>& chain) -> bool {
        if (chain.empty()) return false;
        for (auto& section : chain) {
            const double g = static_cast<double>(section.coefficient());
            if (!std::isfinite(g) || g < 0.0 || g > 0.9) return false;
            const double poleRadius = std::pow(g, 1.0 / static_cast<double>(section.delaySamples()));
            if (!(poleRadius < 1.0)) return false;
            maxPoleRadius = std::max(maxPoleRadius, poleRadius);
            ++sectionsChecked;
        }
        return true;
    };

    for (const double rate : kBracketRates) {
        for (const std::size_t kIn : kBracketKIn) {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            if (!checkChain(cascade)) return fail("DS-1 input cascade violated the coefficient/pole bound");
        }
        for (const std::size_t kOut : kBracketKOut) {
            const auto window = buildOutputWindow(kOut);
            auto left = buildCascade(rate, window.left, kGoldenCoefficient);
            auto right = buildCascade(rate, window.right, kGoldenCoefficient);
            if (!checkChain(left) || !checkChain(right)) {
                return fail("DS-1 output cascade violated the coefficient/pole bound");
            }
        }
    }
    std::cout << std::setprecision(10) << "DS-1 bracket: " << sectionsChecked
              << " sections checked (g_ap=" << kGoldenCoefficient
              << ", in [0,0.9]); largest pole radius g_ap^(1/d)=" << maxPoleRadius << " (< 1)\n";
    return 0;
}

// ---------------------------------------------------------------------------
// DS-2 — allpass magnitude falsification, per full cascade, across the
// bracket. Dense grid: fftSize >= 131072 gives >= 65537 bins on [0, pi].
// ---------------------------------------------------------------------------

int testDs2CascadeMagnitudeFlatnessAcrossBracket() {
    constexpr std::size_t kMinimumFftSize = 131072;
    double maxMagnitudeError = 0.0;
    std::size_t cascadesChecked = 0;

    auto checkCascade = [&](std::vector<SchroederAllpass>& cascade) -> bool {
        if (cascade.empty()) return false;
        std::size_t delaySum = 0;
        for (auto& section : cascade) delaySum += section.delaySamples();
        const std::size_t outputCount = 128 * std::max<std::size_t>(delaySum, 1);
        const std::size_t fftSize = nextPowerOfTwo(std::max<std::size_t>(kMinimumFftSize, outputCount));
        std::vector<Complex> response(fftSize, Complex(0.0, 0.0));
        for (std::size_t n = 0; n < outputCount; ++n) {
            const auto sample = runCascadeSample(cascade, n == 0 ? 1.0F : 0.0F);
            if (sample.nonFinite) return false;
            response[n] = Complex(static_cast<double>(sample.value), 0.0);
        }
        radix2Fft(response);
        for (std::size_t bin = 0; bin <= fftSize / 2; ++bin) {
            maxMagnitudeError = std::max(maxMagnitudeError, std::abs(std::abs(response[bin]) - 1.0));
        }
        ++cascadesChecked;
        return true;
    };

    for (const double rate : kBracketRates) {
        for (const std::size_t kIn : kBracketKIn) {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            if (!checkCascade(cascade)) return fail("DS-2 input cascade magnitude check failed");
        }
        for (const std::size_t kOut : kBracketKOut) {
            const auto window = buildOutputWindow(kOut);
            auto left = buildCascade(rate, window.left, kGoldenCoefficient);
            auto right = buildCascade(rate, window.right, kGoldenCoefficient);
            if (!checkCascade(left) || !checkCascade(right)) return fail("DS-2 output cascade magnitude check failed");
        }
    }
    std::cout << std::setprecision(10) << "DS-2 bracket: " << cascadesChecked
              << " full cascades checked (>= 65537 bins each), max ||A(e^jw)|-1| error=" << maxMagnitudeError << '\n';
    return maxMagnitudeError <= 1e-6 ? 0 : fail("DS-2 cascade magnitude error exceeded 1e-6");
}

// ---------------------------------------------------------------------------
// DS-3 — delay-length derivation, across the bracket, plus a direct
// re-derivation of ADR-006 (c)'s four claimed tie-break firings.
// ---------------------------------------------------------------------------

int testDs3DelayLengthDerivationAcrossBracket() {
    std::size_t configsChecked = 0;
    for (const double rate : kBracketRates) {
        FeedbackDelayNetwork fdn;
        if (!buildFdnFixture(rate, fdn)) return fail("DS-3 FDN fixture preparation failed");
        const std::size_t fdnMin = fdn.delaySamples(0);

        for (const std::size_t kIn : kBracketKIn) {
            auto inputCascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                              kGoldenCoefficient);
            if (inputCascade.empty()) return fail("DS-3 input cascade preparation failed");
            const auto inputDelays = cascadeDelays(inputCascade);
            for (std::size_t i = 0; i < inputDelays.size(); ++i) {
                if (!isPrime(inputDelays[i])) return fail("DS-3 input delay was not prime");
                if (i > 0 && inputDelays[i - 1] >= inputDelays[i]) return fail("DS-3 input window was not strictly increasing");
            }

            for (const std::size_t kOut : kBracketKOut) {
                const auto window = buildOutputWindow(kOut);
                auto left = buildCascade(rate, window.left, kGoldenCoefficient);
                auto right = buildCascade(rate, window.right, kGoldenCoefficient);
                if (left.empty() || right.empty()) return fail("DS-3 output cascade preparation failed");
                const auto leftDelays = cascadeDelays(left);
                const auto rightDelays = cascadeDelays(right);
                std::vector<std::size_t> outputWindowDelays(2 * kOut);
                for (std::size_t i = 0; i < kOut; ++i) {
                    outputWindowDelays[2 * i] = leftDelays[i];
                    outputWindowDelays[2 * i + 1] = rightDelays[i];
                }
                for (std::size_t i = 0; i < outputWindowDelays.size(); ++i) {
                    if (!isPrime(outputWindowDelays[i])) return fail("DS-3 output delay was not prime");
                    if (i > 0 && outputWindowDelays[i - 1] >= outputWindowDelays[i]) {
                        return fail("DS-3 output window was not strictly increasing");
                    }
                }

                std::vector<std::size_t> diffusion(inputDelays);
                diffusion.insert(diffusion.end(), outputWindowDelays.begin(), outputWindowDelays.end());
                for (std::size_t i = 0; i < diffusion.size(); ++i) {
                    if (diffusion[i] >= fdnMin) return fail("DS-3 diffusion delay was not below FDN m_min");
                    for (std::size_t j = i + 1; j < diffusion.size(); ++j) {
                        if (std::gcd(diffusion[i], diffusion[j]) != 1) return fail("DS-3 diffusion delays were not pairwise co-prime");
                    }
                    for (std::size_t line = 0; line < fdn.lineCount(); ++line) {
                        if (std::gcd(diffusion[i], fdn.delaySamples(line)) != 1) {
                            return fail("DS-3 diffusion delay shared a factor with an FDN line");
                        }
                    }
                }
                ++configsChecked;
            }
        }
    }
    std::cout << "DS-3 bracket: " << configsChecked
              << " (K_in,K_out,rate) combinations checked: every diffusion delay prime, strictly "
                 "increasing within its window, pairwise co-prime, co-prime with every FDN line, "
                 "and below FDN m_min\n";
    return 0;
}

// Independently re-derives (never trusts) whether round(f_s*t) sits exactly
// equidistant between two primes for a given target, and if so which prime
// ADR-005's rule selects (the smaller one).
bool nearestPrimeIsTied(double sampleRate, double seconds, std::size_t& lowerOut, std::size_t& upperOut) {
    const auto target = static_cast<std::size_t>(std::llround(sampleRate * seconds));
    if (isPrime(target)) { lowerOut = upperOut = target; return false; }
    std::size_t lower = target;
    while (lower > 1 && !isPrime(--lower)) {}
    std::size_t upper = target;
    while (!isPrime(++upper)) {}
    lowerOut = lower;
    upperOut = upper;
    return (target - lower) == (upper - target);
}

int testDs3TieBreakFiresFourTimes() {
    struct Case {
        double rate;
        double seconds;
        const char* label;
    };
    const std::array<Case, 4> cases {{
        {44100.0, 0.00464159, "input stage 3 @ 44.1kHz"},
        {44100.0, 0.010, "input stage 4 @ 44.1kHz"},
        {48000.0, 0.004, "output stage 1 (L index 0) @ 48kHz"},
        {44100.0, 0.004, "output stage 1 (L index 0) @ 44.1kHz"},
    }};
    for (const Case& testCase : cases) {
        std::size_t lower = 0;
        std::size_t upper = 0;
        if (!nearestPrimeIsTied(testCase.rate, testCase.seconds, lower, upper)) {
            return fail("expected ADR-006 tie-break configuration was not actually tied");
        }
        SchroederAllpass section;
        if (!section.prepare(testCase.rate, testCase.seconds, kGoldenCoefficient)) {
            return fail("tie-break fixture preparation failed");
        }
        if (section.delaySamples() != lower) return fail("tie-break did not select the smaller prime");
        std::cout << "DS-3 tie-break confirmed at " << testCase.label << ": " << lower << " vs " << upper
                  << " (selected " << section.delaySamples() << ")\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DS-5 — peak (l1) headroom, full-scale impulse and adversarial worst-case,
// across the bracket, reported against sqrt5^K per cascade.
// ---------------------------------------------------------------------------

int testDs5AdversarialPeakAcrossBracket() {
    double measuredPeakOverall = 0.0;
    double largestBoundChecked = 0.0;

    auto checkCascade = [&](std::size_t sectionCount, const std::vector<double>& targets, double rate) -> bool {
        auto forward = buildCascade(rate, targets, kGoldenCoefficient);
        if (forward.empty()) return false;
        std::size_t delaySum = 0;
        for (auto& section : forward) delaySum += section.delaySamples();
        const std::size_t windowLength = 64 * std::max<std::size_t>(delaySum, 1);

        std::vector<double> impulseResponse(windowLength, 0.0);
        double fullScalePeak = 0.0;
        for (std::size_t n = 0; n < windowLength; ++n) {
            const auto sample = runCascadeSample(forward, n == 0 ? 1.0F : 0.0F);
            if (sample.nonFinite) return false;
            impulseResponse[n] = static_cast<double>(sample.value);
            fullScalePeak = std::max(fullScalePeak, std::abs(impulseResponse[n]));
        }
        if (!hasNonzeroSample(impulseResponse)) return false; // anti-vacuity guard

        auto adversarial = buildCascade(rate, targets, kGoldenCoefficient);
        double adversarialPeak = 0.0;
        for (std::size_t n = 0; n < windowLength; ++n) {
            const double reversed = impulseResponse[windowLength - 1 - n];
            const float input = reversed > 0.0 ? 1.0F : (reversed < 0.0 ? -1.0F : 0.0F);
            const auto sample = runCascadeSample(adversarial, input);
            if (sample.nonFinite) return false;
            adversarialPeak = std::max(adversarialPeak, std::abs(static_cast<double>(sample.value)));
        }

        const double bound = std::pow(std::sqrt(5.0), static_cast<double>(sectionCount));
        measuredPeakOverall = std::max({measuredPeakOverall, fullScalePeak, adversarialPeak});
        largestBoundChecked = std::max(largestBoundChecked, bound);
        return adversarialPeak <= bound + 1e-5;
    };

    for (const double rate : kBracketRates) {
        for (const std::size_t kIn : kBracketKIn) {
            if (!checkCascade(kIn, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn), rate)) {
                return fail("DS-5 input cascade adversarial peak exceeded its sqrt5^K_in bound");
            }
        }
        for (const std::size_t kOut : kBracketKOut) {
            const auto window = buildOutputWindow(kOut);
            if (!checkCascade(kOut, window.left, rate) || !checkCascade(kOut, window.right, rate)) {
                return fail("DS-5 output cascade adversarial peak exceeded its sqrt5^K_out bound");
            }
        }
    }
    std::cout << std::setprecision(10) << "DS-5 bracket: largest measured peak=" << measuredPeakOverall
              << ", largest checked bound=" << largestBoundChecked
              << " (fixed K_in=4 bound=" << std::pow(std::sqrt(5.0), 4.0)
              << ", fixed K_out=2 bound=" << std::pow(std::sqrt(5.0), 2.0) << "); overshoot reported, never clipped\n";
    return 0;
}

// ---------------------------------------------------------------------------
// DS-6 — echo density, recorded (not gated) against the idealized lattice
// count, at >= 3 times spanning 1-30ms plus the network's first arrival.
// ---------------------------------------------------------------------------

int testDs6EchoDensityRecorded() {
    struct Checkpoint {
        double seconds;
        const char* label;
    };
    const std::array<Checkpoint, 4> checkpoints {{
        {0.001, "1ms"}, {0.010, "10ms"}, {0.027, "27ms (t_min)"}, {0.030, "30ms"},
    }};

    for (const double rate : kBracketRates) {
        for (const std::size_t kIn : kBracketKIn) {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            if (cascade.empty()) return fail("DS-6 input cascade preparation failed");
            double delayProduct = 1.0;
            for (auto& section : cascade) delayProduct *= static_cast<double>(section.delaySamples());

            const auto renderLength = static_cast<std::size_t>(std::ceil(0.031 * rate)) + 1;
            std::vector<float> response(renderLength);
            for (std::size_t n = 0; n < renderLength; ++n) {
                const auto sample = runCascadeSample(cascade, n == 0 ? 1.0F : 0.0F);
                if (sample.nonFinite) return fail("DS-6 render raised a fault");
                response[n] = sample.value;
            }
            if (!hasNonzeroSample(response)) return fail("DS-6 anti-vacuity: rendered fixture was all silence");

            std::size_t crossingCount = 0;
            float previousNonzero = response[0];
            std::vector<std::size_t> crossingsAtCheckpoint(checkpoints.size(), 0);
            std::size_t checkpointIndex = 0;
            for (std::size_t n = 1; n < response.size(); ++n) {
                if (previousNonzero != 0.0F && response[n] != 0.0F
                    && (previousNonzero > 0.0F) != (response[n] > 0.0F)) {
                    ++crossingCount;
                }
                if (response[n] != 0.0F) previousNonzero = response[n];
                while (checkpointIndex < checkpoints.size()
                       && static_cast<double>(n) >= checkpoints[checkpointIndex].seconds * rate) {
                    crossingsAtCheckpoint[checkpointIndex] = crossingCount;
                    ++checkpointIndex;
                }
            }
            while (checkpointIndex < checkpoints.size()) {
                crossingsAtCheckpoint[checkpointIndex] = crossingCount;
                ++checkpointIndex;
            }

            std::cout << std::setprecision(8) << "DS-6 K_in=" << kIn << " @ " << rate << "Hz: ";
            for (std::size_t i = 0; i < checkpoints.size(); ++i) {
                const double n = checkpoints[i].seconds * rate;
                const double idealized = rate * std::pow(n, static_cast<double>(kIn) - 1.0)
                                        / (std::tgamma(static_cast<double>(kIn)) * delayProduct);
                const double measuredRate = static_cast<double>(crossingsAtCheckpoint[i]) / checkpoints[i].seconds;
                std::cout << checkpoints[i].label << "(idealized=" << idealized << "/s, measured=" << measuredRate << "/s) ";
            }
            std::cout << '\n';
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DS-10 — numerical safety extension: substitution at the input-chain head,
// repeated-reset idempotency, whole-path silence-in/silence-out, and the
// per-section S_j/D_j proof template plus a separately measured silence
// sample count (never compared to the historical additive bound).
// ---------------------------------------------------------------------------

int testDs10NonFiniteSubstitutionAtInputHead() {
    DiffusionStereoPath faulting;
    DiffusionStereoPath clean;
    if (!faulting.prepare(validConfig(48000.0)) || !clean.prepare(validConfig(48000.0))) {
        return fail("DS-10 substitution fixture preparation failed");
    }
    constexpr std::size_t kFrames = 4096;
    std::vector<float> faultingInput(kFrames, 0.0F);
    faultingInput[0] = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> cleanInput(kFrames, 0.0F);

    std::vector<float> faultingLeft(kFrames);
    std::vector<float> faultingRight(kFrames);
    std::vector<float> cleanLeft(kFrames);
    std::vector<float> cleanRight(kFrames);
    faulting.process(faultingInput.data(), faultingLeft.data(), faultingRight.data(), kFrames);
    clean.process(cleanInput.data(), cleanLeft.data(), cleanRight.data(), kFrames);

    if (faulting.nonFiniteCount() != 1 || !faulting.nonFiniteLatched() || !faulting.resetPending()) {
        return fail("DS-10 NaN head sample was not recorded as exactly one wrapper fault");
    }
    if (clean.nonFiniteCount() != 0) return fail("DS-10 all-zero control fixture unexpectedly faulted");
    for (std::size_t n = 1; n < kFrames; ++n) {
        if (faultingLeft[n] != cleanLeft[n] || faultingRight[n] != cleanRight[n]) {
            return fail("DS-10 substitution at the input-chain head did not reproduce the all-zero control from sample 1 onward");
        }
    }
    std::cout << "DS-10 substitution-at-head: " << (kFrames - 1)
              << " post-fault samples were bit-identical to an all-zero control (recovery is deferred to the "
                 "next block, so this isolates the head-substitution property from block-boundary reset timing)\n";
    return 0;
}

int testDs10RepeatedResetIsIdempotent() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("DS-10 idempotent-reset fixture preparation failed");

    const auto script = aetherfield::dsp_test::makeNoiseScript(2048, 0x1DEA5EEDU);
    std::vector<float> left(script.size());
    std::vector<float> right(script.size());
    path.process(script.data(), left.data(), right.data(), script.size());
    const std::size_t countBeforeReset = path.nonFiniteCount();

    path.reset();
    const bool preparedAfterOneReset = path.isPrepared();
    const float coefficientAfterOneReset = path.allpassCoefficient();
    const std::size_t countAfterOneReset = path.nonFiniteCount();
    const bool latchedAfterOneReset = path.nonFiniteLatched();
    const bool pendingAfterOneReset = path.resetPending();
    std::vector<float> leftAfterOneReset(script.size());
    std::vector<float> rightAfterOneReset(script.size());
    path.process(script.data(), leftAfterOneReset.data(), rightAfterOneReset.data(), script.size());

    DiffusionStereoPath path2;
    if (!path2.prepare(validConfig(48000.0))) return fail("DS-10 idempotent-reset control fixture preparation failed");
    path2.process(script.data(), left.data(), right.data(), script.size());
    path2.reset();
    path2.reset();
    if (path2.isPrepared() != preparedAfterOneReset || path2.allpassCoefficient() != coefficientAfterOneReset
        || path2.nonFiniteCount() != countAfterOneReset || path2.nonFiniteLatched() != latchedAfterOneReset
        || path2.resetPending() != pendingAfterOneReset) {
        return fail("DS-10 reset();reset() diverged from a single reset() in observable lifecycle state");
    }
    std::vector<float> leftAfterTwoResets(script.size());
    std::vector<float> rightAfterTwoResets(script.size());
    path2.process(script.data(), leftAfterTwoResets.data(), rightAfterTwoResets.data(), script.size());
    if (leftAfterOneReset != leftAfterTwoResets || rightAfterOneReset != rightAfterTwoResets) {
        return fail("DS-10 reset();reset() produced different subsequent audio than a single reset()");
    }
    std::cout << "DS-10 reset();reset() is indistinguishable from a single reset() (" << countBeforeReset
              << " cumulative faults observed before reset, preserved identically across both)\n";
    return 0;
}

int testDs10SilenceInSilenceOutBitExact() {
    DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("DS-10 silence fixture preparation failed");
    constexpr std::size_t kFrames = 8192;
    const std::vector<float> input(kFrames, 0.0F);
    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    path.process(input.data(), left.data(), right.data(), kFrames);
    for (std::size_t n = 0; n < kFrames; ++n) {
        if (left[n] != 0.0F || right[n] != 0.0F) return fail("DS-10 silence-in did not produce exact silence-out");
    }
    if (path.nonFiniteCount() != 0 || path.nonFiniteLatched()) return fail("DS-10 silence fixture unexpectedly faulted");
    std::cout << "DS-10 silence-in/silence-out: " << kFrames << " zero samples were bit-exact zero on both channels\n";
    return 0;
}

int testDs10ProofTemplateAndMeasuredSilence() {
    struct Section {
        const char* label;
        std::size_t delay;
        double stateBound;
    };

    for (const double rate : kBracketRates) {
        DiffusionStereoPath measure;
        if (!measure.prepare(validConfig(rate))) return fail("DS-10 proof-template fixture preparation failed");

        // Measured peak tap-normalized amplitude actually observed arriving
        // at the output diffuser cascades for a full-scale impulse, over a
        // window spanning several T60s (the fixture's t60ZeroSeconds=4s), so
        // it captures the FDN's build-up and decay rather than only an early
        // transient. This stands in for a full analytic propagation of the
        // peak-signal bound through the FDN (ADR-006 supplies no closed form
        // for that); it is a measured bound for THIS fixture, not a general
        // proof, and is reported as such.
        constexpr std::size_t kMeasureFrames = 300000;
        double measuredTapPeak = 0.0;
        const float tapScale = 1.0F / std::sqrt(4.0F); // lineCount/2 == 4 for this N=8 fixture
        for (std::size_t n = 0; n < kMeasureFrames; ++n) {
            const auto taps = measure.preStepTapSums();
            measuredTapPeak = std::max({measuredTapPeak, std::abs(static_cast<double>(taps.even * tapScale)),
                                        std::abs(static_cast<double>(taps.odd * tapScale))});
            static_cast<void>(measure.processSample(n == 0 ? 1.0F : 0.0F));
        }
        if (measuredTapPeak <= 0.0) return fail("DS-10 anti-vacuity: tap-peak measurement fixture never went nonzero");

        const double g = kGoldenCoefficient;
        const double sqrt5 = std::sqrt(5.0);
        const std::array<std::size_t, 4> inputDelays {
            measure.inputDelaySamples(0), measure.inputDelaySamples(1),
            measure.inputDelaySamples(2), measure.inputDelaySamples(3),
        };
        const std::array<std::size_t, 2> leftDelays {measure.leftOutputDelaySamples(0), measure.leftOutputDelaySamples(1)};
        const std::array<std::size_t, 2> rightDelays {measure.rightOutputDelaySamples(0), measure.rightOutputDelaySamples(1)};

        std::vector<Section> sections;
        for (std::size_t j = 0; j < inputDelays.size(); ++j) {
            // S_j: the input chain's own peak-signal bound, sqrt5^(j) at the
            // input to section j+1 (sqrt5^0=1 at the chain head, a full-scale
            // impulse), propagated through v_j = x_j + g*s_j's geometric
            // self-consistent bound, |v_j|_max <= |x_j|_max / (1 - g).
            sections.push_back({"input", inputDelays[j], std::pow(sqrt5, static_cast<double>(j)) / (1.0 - g)});
        }
        for (std::size_t j = 0; j < leftDelays.size(); ++j) {
            sections.push_back({"outputL", leftDelays[j], measuredTapPeak * std::pow(sqrt5, static_cast<double>(j)) / (1.0 - g)});
        }
        for (std::size_t j = 0; j < rightDelays.size(); ++j) {
            sections.push_back({"outputR", rightDelays[j], measuredTapPeak * std::pow(sqrt5, static_cast<double>(j)) / (1.0 - g)});
        }

        std::size_t maxDrain = 0;
        std::cout << std::setprecision(10) << "DS-10 proof template @ " << rate << "Hz (q_j=" << g
                  << ", measured tap peak=" << measuredTapPeak << "):\n";
        for (const Section& section : sections) {
            std::size_t k = 1;
            while (std::pow(g, static_cast<double>(k)) * section.stateBound >= 1e-20) ++k;
            const std::size_t drain = (k + 1) * section.delay;
            maxDrain = std::max(maxDrain, drain);
            std::cout << "  " << section.label << " d_j=" << section.delay << " S_j=" << section.stateBound
                      << " k_j=" << k << " D_j=(k_j+1)*d_j=" << drain << '\n';
        }

        // Separately measured actual full-path silence -- NOT compared to any
        // historical additive timeout (ADR-006 correction note C3 forbids
        // that comparison), just reported alongside the proof-template drain.
        // The FDN's own decay dominates the diffusion sections' D_j here (its
        // NS-8 measurement recorded ~1,177,358 samples to sustained exact
        // silence at this same T60_0=4s fixture), so the window must clear
        // that, not just the much smaller diffusion drain.
        constexpr std::size_t kMinimumSilenceRenderFrames = 1400000;
        const std::size_t renderLength = std::max<std::size_t>(maxDrain + 20000, kMinimumSilenceRenderFrames);
        std::vector<float> input(renderLength, 0.0F);
        input[0] = 1.0F;
        std::vector<float> left(renderLength);
        std::vector<float> right(renderLength);
        DiffusionStereoPath silencePath;
        if (!silencePath.prepare(validConfig(rate))) return fail("DS-10 measured-silence fixture preparation failed");
        silencePath.process(input.data(), left.data(), right.data(), renderLength);
        if (silencePath.nonFiniteCount() != 0) return fail("DS-10 measured-silence fixture unexpectedly faulted");

        bool sawNonzero = false;
        std::size_t lastNonzero = 0;
        for (std::size_t n = 0; n < renderLength; ++n) {
            if (left[n] != 0.0F || right[n] != 0.0F) { lastNonzero = n; sawNonzero = true; }
        }
        if (!sawNonzero) return fail("DS-10 anti-vacuity: full-scale impulse produced no nonzero wet sample");
        std::cout << std::setprecision(10) << "DS-10 @ " << rate << "Hz: measured full-path exact silence from sample "
                  << (lastNonzero + 1) << " onward, through the end of a " << renderLength
                  << "-sample render (proof-template max D_j=" << maxDrain << "; not compared to any historical "
                     "additive bound)\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DS-11 — determinism and block partitions, extending the existing
// {1,13,64,512,3} fixed-partition coverage with repeated-script determinism
// and the `ragged` pattern DS-A's own bit-identity test uses.
// ---------------------------------------------------------------------------

int testDs11DeterminismAndRaggedPartition() {
    constexpr std::size_t kFrames = 8192;
    const auto script = aetherfield::dsp_test::makeNoiseScript(kFrames, 0xF00DFACEU);

    auto renderWhole = [&]() -> std::vector<float> {
        DiffusionStereoPath path;
        if (!path.prepare(validConfig(48000.0))) return {};
        std::vector<float> left(kFrames);
        std::vector<float> right(kFrames);
        path.process(script.data(), left.data(), right.data(), kFrames);
        std::vector<float> interleaved(2 * kFrames);
        for (std::size_t n = 0; n < kFrames; ++n) {
            interleaved[2 * n] = left[n];
            interleaved[2 * n + 1] = right[n];
        }
        return interleaved;
    };

    const auto baseline = renderWhole();
    if (baseline.empty()) return fail("DS-11 determinism fixture preparation failed");
    if (!hasNonzeroSample(baseline)) return fail("DS-11 anti-vacuity: rendered fixture was all silence");
    if (renderWhole() != baseline) return fail("DS-11 repeated identical script changed float bits");

    DiffusionStereoPath partitioned;
    if (!partitioned.prepare(validConfig(48000.0))) return fail("DS-11 ragged fixture preparation failed");
    constexpr std::array<std::size_t, 5> kRagged {7, 29, 3, 211, 5};
    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    std::size_t offset = 0;
    std::size_t index = 0;
    while (offset < kFrames) {
        const std::size_t count = std::min(kRagged[index++ % kRagged.size()], kFrames - offset);
        partitioned.process(script.data() + offset, left.data() + offset, right.data() + offset, count);
        offset += count;
    }
    std::vector<float> raggedInterleaved(2 * kFrames);
    for (std::size_t n = 0; n < kFrames; ++n) {
        raggedInterleaved[2 * n] = left[n];
        raggedInterleaved[2 * n + 1] = right[n];
    }
    if (raggedInterleaved != baseline) return fail("DS-11 ragged block partition changed diffusion/stereo output");

    std::cout << "DS-11: repeated identical scripts and a ragged {7,29,3,211,5} block partition are float-bit "
                 "identical to the whole-buffer render (extends the existing {1,13,64,512,3} fixed-partition "
                 "coverage in testFixedParameterBlockPartitionsAreBitIdentical)\n";
    return 0;
}

// ---------------------------------------------------------------------------
// DS-12 — cost and realtime safety: bracket configurations are runnable
// standalone cascades, allocate nothing during processing, and a reported
// (not budgeted) per-sample cost with and without the diffusion stages.
// ---------------------------------------------------------------------------

int testDs12BracketConfigurationsAreRunnable() {
    std::size_t configsChecked = 0;
    for (const double rate : kBracketRates) {
        for (const std::size_t kIn : kBracketKIn) {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            if (cascade.empty()) return fail("DS-12 bracket input cascade did not prepare");
            for (std::size_t n = 0; n < 256; ++n) {
                if (runCascadeSample(cascade, n == 0 ? 1.0F : 0.0F).nonFinite) {
                    return fail("DS-12 bracket input cascade raised a fault on a finite fixture");
                }
            }
            ++configsChecked;
        }
        for (const std::size_t kOut : kBracketKOut) {
            const auto window = buildOutputWindow(kOut);
            auto left = buildCascade(rate, window.left, kGoldenCoefficient);
            auto right = buildCascade(rate, window.right, kGoldenCoefficient);
            if (left.empty() || right.empty()) return fail("DS-12 bracket output cascade did not prepare");
            for (std::size_t n = 0; n < 256; ++n) {
                if (runCascadeSample(left, n == 0 ? 1.0F : 0.0F).nonFinite
                    || runCascadeSample(right, n == 0 ? 1.0F : 0.0F).nonFinite) {
                    return fail("DS-12 bracket output cascade raised a fault on a finite fixture");
                }
            }
            ++configsChecked;
        }
    }
    std::cout << "DS-12 bracket: " << configsChecked
              << " K_in in {2,3,4,5} / K_out in {1,2} standalone cascades built directly from SchroederAllpass "
                 "(not via DiffusionStereoConfig) prepared and ran successfully at both fixture rates\n";
    return 0;
}

int testDs12BracketCascadesDoNotAllocateDuringProcessing() {
    auto cascade = buildCascade(48000.0, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, 5),
                                kGoldenCoefficient);
    if (cascade.empty()) return fail("DS-12 allocation fixture preparation failed");
    for (std::size_t n = 0; n < 128; ++n) static_cast<void>(runCascadeSample(cascade, n == 0 ? 1.0F : 0.0F));
    const std::size_t before = gAllocationCount.load(std::memory_order_relaxed);
    for (std::size_t n = 0; n < 4096; ++n) static_cast<void>(runCascadeSample(cascade, 0.0F));
    if (gAllocationCount.load(std::memory_order_relaxed) != before) {
        return fail("DS-12 bracket cascade allocated during processing");
    }
    std::cout << "DS-12 bracket allocation delta through a standalone K_in=5 cascade: 0\n";
    return 0;
}

int testDs12CostWithAndWithoutDiffusion() {
    DiffusionStereoPath withDiffusion;
    FeedbackDelayNetwork bareFdn;
    if (!withDiffusion.prepare(validConfig(48000.0)) || !bareFdn.prepare(48000.0, 8, kTMin, kTMax, 4.0, 4.0)) {
        return fail("DS-12 cost fixture preparation failed");
    }
    constexpr std::size_t kWarmup = 4096;
    constexpr std::size_t kMeasured = 200000;
    for (std::size_t n = 0; n < kWarmup; ++n) {
        static_cast<void>(withDiffusion.processSample(n == 0 ? 1.0F : 0.0F));
        static_cast<void>(bareFdn.processSample(n == 0 ? 1.0F : 0.0F));
    }

    const auto wrapperStart = std::chrono::steady_clock::now();
    for (std::size_t n = 0; n < kMeasured; ++n) static_cast<void>(withDiffusion.processSample(0.0F));
    const auto wrapperEnd = std::chrono::steady_clock::now();

    const auto fdnStart = std::chrono::steady_clock::now();
    for (std::size_t n = 0; n < kMeasured; ++n) static_cast<void>(bareFdn.processSample(0.0F));
    const auto fdnEnd = std::chrono::steady_clock::now();

    const double wrapperNsPerSample = std::chrono::duration<double, std::nano>(wrapperEnd - wrapperStart).count()
                                     / static_cast<double>(kMeasured);
    const double fdnNsPerSample = std::chrono::duration<double, std::nano>(fdnEnd - fdnStart).count()
                                 / static_cast<double>(kMeasured);
    std::cout << std::setprecision(6)
              << "DS-12 measured cost (wall-clock ns/sample; a count, not cycles, not a CPU budget): full wrapper "
                 "(diffusion+FDN)=" << wrapperNsPerSample << " ns/sample, bare FDN alone=" << fdnNsPerSample
              << " ns/sample\n";
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

    if (testAntiVacuityInfrastructure() != 0) return 1;
    if (testDs1CoefficientAndPoleBoundAcrossBracket() != 0) return 1;
    if (testDs2CascadeMagnitudeFlatnessAcrossBracket() != 0) return 1;
    if (testDs3DelayLengthDerivationAcrossBracket() != 0) return 1;
    if (testDs3TieBreakFiresFourTimes() != 0) return 1;
    if (testDs5AdversarialPeakAcrossBracket() != 0) return 1;
    if (testDs6EchoDensityRecorded() != 0) return 1;
    if (testDs10NonFiniteSubstitutionAtInputHead() != 0) return 1;
    if (testDs10RepeatedResetIsIdempotent() != 0) return 1;
    if (testDs10SilenceInSilenceOutBitExact() != 0) return 1;
    if (testDs10ProofTemplateAndMeasuredSilence() != 0) return 1;
    if (testDs11DeterminismAndRaggedPartition() != 0) return 1;
    if (testDs12BracketConfigurationsAreRunnable() != 0) return 1;
    if (testDs12BracketCascadesDoNotAllocateDuringProcessing() != 0) return 1;
    if (testDs12CostWithAndWithoutDiffusion() != 0) return 1;
    std::cout << "DiffusionStereoPath Task 4a (DS-1,2,3,5,6,10,11,12 + anti-vacuity) measurements passed\n";
    return 0;
}

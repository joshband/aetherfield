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
#include <string>
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
// below. The shared-header names follow; Task 4b's DS-4/7/8/9 measurements
// draw on the same list. ----

using aetherfield::dsp_test::CoherenceSummary;
using aetherfield::dsp_test::Complex;
using aetherfield::dsp_test::forEachBracketCascade;
using aetherfield::dsp_test::geometricTargets;
using aetherfield::dsp_test::hasMinimumFitPoints;
using aetherfield::dsp_test::hasMinimumRetainedBins;
using aetherfield::dsp_test::hasMinimumWelchSegments;
using aetherfield::dsp_test::hasNonzeroSample;
using aetherfield::dsp_test::isPrime;
using aetherfield::dsp_test::magnitudeSquaredCoherence;
using aetherfield::dsp_test::makeNoiseScript;
using aetherfield::dsp_test::maxShortLagCorrelation;
using aetherfield::dsp_test::nextPowerOfTwo;
using aetherfield::dsp_test::pearsonCorrelationAtLag;
using aetherfield::dsp_test::radix2Fft;
using aetherfield::dsp_test::welchCrossSpectra;

constexpr std::array<double, 2> kBracketRates {48000.0, 44100.0};
constexpr double kInputWindowMinSeconds = 0.001;
constexpr double kInputWindowMaxSeconds = 0.010;
constexpr double kOutputWindowMinSeconds = 0.004;
constexpr double kOutputWindowMaxSeconds = 0.008;
constexpr std::array<std::size_t, 4> kBracketKIn {2, 3, 4, 5};
constexpr std::array<std::size_t, 2> kBracketKOut {1, 2};

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

// Reuses SchroederAllpass::Sample (the per-stage {value, nonFinite} result)
// instead of a near-identical local struct: a cascade's end-to-end result
// carries exactly the same two fields as one section's result.
SchroederAllpass::Sample runCascadeSample(std::vector<SchroederAllpass>& cascade, float input) noexcept {
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

    // Task 4b needs the pre-Mix wet channels and this sample's Mix gains,
    // which the production wrapper deliberately does not expose (DS-8's
    // dry/wet covariance and DS-9's wet-only arrival/centroid are defined on
    // exactly those intermediate values). They are surfaced here, on the
    // reference implementation, rather than by adding an accessor to
    // DiffusionStereoPath. processSample() below is this function with the
    // extra fields dropped -- the same expressions in the same order on the
    // same values -- so the bit-exactness asserted by
    // testProcessSampleMatchesExactOrderedReference() is unaffected.
    struct DetailedSample {
        float left;
        float right;
        float wetLeft;
        float wetRight;
        float dryGain;
        float wetGain;
    };

    DetailedSample processSampleDetailed(float mono) {
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
        return {mix.dry * mono + mix.wet * left, mix.dry * mono + mix.wet * right, left, right, mix.dry, mix.wet};
    }

    aetherfield::dsp::StereoSample processSample(float mono) {
        const auto detailed = processSampleDetailed(mono);
        return {detailed.left, detailed.right, false};
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

    // Task 4b's two additional guards, covered directly here for the same
    // reason 4a covered its own: they are shared header helpers, so a call
    // site happening to exercise them is not the same as testing them.
    if (hasMinimumRetainedBins(0) || hasMinimumRetainedBins(1)) {
        return fail("anti-vacuity guard accepted a coherence summary over fewer than two retained bins");
    }
    if (!hasMinimumRetainedBins(2)) {
        return fail("anti-vacuity guard rejected a coherence summary with enough retained bins");
    }
    if (hasMinimumFitPoints(0) || hasMinimumFitPoints(2) || hasMinimumFitPoints(7)) {
        return fail("anti-vacuity guard accepted a decay fit through too few points");
    }
    if (!hasMinimumFitPoints(8) || !hasMinimumFitPoints(600)) {
        return fail("anti-vacuity guard rejected a decay fit with enough points");
    }

    std::cout << "Anti-vacuity infrastructure: rejects all-silent fixtures, single-segment coherence "
                 "estimates, coherence summaries over fewer than two retained bins and decay fits through "
                 "fewer than eight points; accepts a real signal and valid estimates of each kind\n";
    return 0;
}

// geometricTargets(a, b, 1) is documented in DiffusionStereoAnalysis.h to
// degenerate to {a}, but no bracket call site here ever passes K == 1 (the
// bracket itself never uses K < 2), so nothing previously exercised this
// branch. geometricTargets is a shared header helper, not a private detail of
// this file, so this direct unit test covers it before a future caller
// relies on the degenerate case.
int testGeometricTargetsSingleElementDegeneracy() {
    const auto targets = geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, 1);
    if (targets.size() != 1 || targets[0] != kInputWindowMinSeconds) {
        return fail("geometricTargets(a, b, 1) did not degenerate to the single-element result {a}");
    }
    std::cout << "geometricTargets(a, b, 1) degenerates to {a} as documented\n";
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

    const int bracketResult = forEachBracketCascade(
        kBracketRates, kBracketKIn, kBracketKOut,
        [&](double rate, std::size_t kIn) -> int {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            return checkChain(cascade) ? 0 : fail("DS-1 input cascade violated the coefficient/pole bound");
        },
        [&](double rate, std::size_t kOut) -> int {
            const auto window = buildOutputWindow(kOut);
            auto left = buildCascade(rate, window.left, kGoldenCoefficient);
            auto right = buildCascade(rate, window.right, kGoldenCoefficient);
            return (checkChain(left) && checkChain(right))
                ? 0 : fail("DS-1 output cascade violated the coefficient/pole bound");
        });
    if (bracketResult != 0) return bracketResult;

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

    const int bracketResult = forEachBracketCascade(
        kBracketRates, kBracketKIn, kBracketKOut,
        [&](double rate, std::size_t kIn) -> int {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            return checkCascade(cascade) ? 0 : fail("DS-2 input cascade magnitude check failed");
        },
        [&](double rate, std::size_t kOut) -> int {
            const auto window = buildOutputWindow(kOut);
            auto left = buildCascade(rate, window.left, kGoldenCoefficient);
            auto right = buildCascade(rate, window.right, kGoldenCoefficient);
            return (checkCascade(left) && checkCascade(right))
                ? 0 : fail("DS-2 output cascade magnitude check failed");
        });
    if (bracketResult != 0) return bracketResult;

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

    const int bracketResult = forEachBracketCascade(
        kBracketRates, kBracketKIn, kBracketKOut,
        [&](double rate, std::size_t kIn) -> int {
            return checkCascade(kIn, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn), rate)
                ? 0 : fail("DS-5 input cascade adversarial peak exceeded its sqrt5^K_in bound");
        },
        [&](double rate, std::size_t kOut) -> int {
            const auto window = buildOutputWindow(kOut);
            return (checkCascade(kOut, window.left, rate) && checkCascade(kOut, window.right, rate))
                ? 0 : fail("DS-5 output cascade adversarial peak exceeded its sqrt5^K_out bound");
        });
    if (bracketResult != 0) return bracketResult;

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
        std::string label;
    };

    for (const double rate : kBracketRates) {
        FeedbackDelayNetwork fdn;
        if (!buildFdnFixture(rate, fdn)) return fail("DS-6 FDN first-arrival fixture preparation failed");
        const std::size_t firstFdnArrival = fdn.delaySamples(0);
        const std::vector<Checkpoint> checkpoints {
            {0.001, "1ms"},
            {0.010, "10ms"},
            {static_cast<double>(firstFdnArrival) / rate,
             "first FDN arrival m_0=" + std::to_string(firstFdnArrival) + " samples"},
            {0.030, "30ms"},
        };
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

            double peak = 0.0;
            for (const float sample : response) peak = std::max(peak, std::abs(static_cast<double>(sample)));
            const double amplitudeThreshold = peak * 1e-6;
            if (!(amplitudeThreshold > 0.0)) return fail("DS-6 amplitude-density threshold was vacuous");

            // `crossingCount` is a sign-flip (zero-crossing) count between
            // consecutive nonzero samples of the rendered impulse response --
            // it does NOT count same-sign consecutive arrivals the way the
            // idealized lattice echo-density formula below does. The two are
            // structurally different quantities (a zero-crossing rate is a
            // proxy for, not a measurement of, arrival density), so printing
            // them side by side is expected to show a divergence; that is not
            // itself evidence of a bug.
            std::size_t crossingCount = 0;
            std::size_t thresholdedSampleCount = std::abs(static_cast<double>(response[0])) >= amplitudeThreshold ? 1 : 0;
            float previousNonzero = response[0];
            std::vector<std::size_t> crossingsAtCheckpoint(checkpoints.size(), 0);
            std::vector<std::size_t> thresholdedSamplesAtCheckpoint(checkpoints.size(), 0);
            std::size_t checkpointIndex = 0;
            for (std::size_t n = 1; n < response.size(); ++n) {
                if (previousNonzero != 0.0F && response[n] != 0.0F
                    && (previousNonzero > 0.0F) != (response[n] > 0.0F)) {
                    ++crossingCount;
                }
                if (response[n] != 0.0F) previousNonzero = response[n];
                if (std::abs(static_cast<double>(response[n])) >= amplitudeThreshold) {
                    ++thresholdedSampleCount;
                }
                while (checkpointIndex < checkpoints.size()
                       && static_cast<double>(n) >= checkpoints[checkpointIndex].seconds * rate) {
                    crossingsAtCheckpoint[checkpointIndex] = crossingCount;
                    thresholdedSamplesAtCheckpoint[checkpointIndex] = thresholdedSampleCount;
                    ++checkpointIndex;
                }
            }
            while (checkpointIndex < checkpoints.size()) {
                crossingsAtCheckpoint[checkpointIndex] = crossingCount;
                thresholdedSamplesAtCheckpoint[checkpointIndex] = thresholdedSampleCount;
                ++checkpointIndex;
            }

            std::cout << std::setprecision(8) << "DS-6 K_in=" << kIn << " @ " << rate << "Hz: ";
            for (std::size_t i = 0; i < checkpoints.size(); ++i) {
                const double n = checkpoints[i].seconds * rate;
                const double idealized = rate * std::pow(n, static_cast<double>(kIn) - 1.0)
                                        / (std::tgamma(static_cast<double>(kIn)) * delayProduct);
                const double measuredRate = static_cast<double>(crossingsAtCheckpoint[i]) / checkpoints[i].seconds;
                const double amplitudeAwareRate =
                    static_cast<double>(thresholdedSamplesAtCheckpoint[i]) / checkpoints[i].seconds;
                std::cout << checkpoints[i].label << "(idealized=" << idealized
                          << "/s, amplitude-aware sample density=" << amplitudeAwareRate
                          << "/s at |x| >= " << amplitudeThreshold
                          << ", zero-crossing proxy=" << measuredRate << "/s) ";
            }
            std::cout << " [input-diffuser-only response; FDN first-arrival checkpoint is a timing marker]\n";
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
    // This comparison deliberately excludes sample index 0, but not because of
    // block-boundary reset timing (recovery is deferred to the next block
    // either way, so that timing is identical for every sample here). The
    // real reason: DiffusionStereoPath::processOne (src/dsp/DiffusionStereoPath.cpp)
    // computes its dry-mix term as `mix.dry * mono` using the raw,
    // unsubstituted `mono` parameter -- by ADR-006 (g) design, the dry path is
    // not part of the wet diffusion/FDN chain this ADR governs, so it is never
    // routed through the head-substitution this test is checking. With
    // `mono == NaN` at index 0, `mix.dry * NaN` is legitimately NaN in both
    // `faulting` and (via the untouched dry term) would-be comparisons, so
    // index 0 is excluded because it is a dry-path artifact, not because
    // wet-path substitution is untested there. Do not "fix" this test to
    // start at n=0: the wet-path substitution guarantee this test targets
    // does not extend to (and was never meant to extend to) the dry path.
    for (std::size_t n = 1; n < kFrames; ++n) {
        if (faultingLeft[n] != cleanLeft[n] || faultingRight[n] != cleanRight[n]) {
            return fail("DS-10 substitution at the input-chain head did not reproduce the all-zero control from sample 1 onward");
        }
    }
    std::cout << "DS-10 substitution-at-head: " << (kFrames - 1)
              << " post-fault samples were bit-identical to an all-zero control (sample 0 is excluded because "
                 "processOne's dry-mix term uses the raw, unsubstituted mono input by ADR-006 (g) design, not "
                 "because of block-boundary reset timing)\n";
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
        // at the output diffuser cascades for a full-scale impulse over this
        // fixed finite window. The wrapper's default automation controls govern
        // this render. This is a fixture illustration rather than a propagated
        // analytic cessation bound through the FDN.
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

        const float storedCoefficient = measure.allpassCoefficient();
        const double g = static_cast<double>(storedCoefficient);
        if (storedCoefficient != static_cast<float>(kGoldenCoefficient)) {
            return fail("DS-10 proof template fixture did not retain the expected stored float q_j");
        }
        FeedbackDelayNetwork controlNetwork;
        ParameterAutomation defaultControls;
        if (!buildFdnFixture(rate, controlNetwork) || !defaultControls.prepare(controlNetwork, rate, kDMaxFixture)) {
            return fail("DS-10 default-control fixture preparation failed");
        }
        const double realizedDefaultT60 = std::sqrt(defaultControls.t60Min() * defaultControls.t60Max());
        // For stored q >= 0, the allpass impulse l1 gain is
        // q + (1-q*q)/(1-q) = 1 + 2q. The analytic input-cessation S_j
        // propagation below uses this same stored q, not ideal sqrt(5).
        const double sectionPeakGain = 1.0 + 2.0 * g;
        const std::array<std::size_t, 4> inputDelays {
            measure.inputDelaySamples(0), measure.inputDelaySamples(1),
            measure.inputDelaySamples(2), measure.inputDelaySamples(3),
        };
        const std::array<std::size_t, 2> leftDelays {measure.leftOutputDelaySamples(0), measure.leftOutputDelaySamples(1)};
        const std::array<std::size_t, 2> rightDelays {measure.rightOutputDelaySamples(0), measure.rightOutputDelaySamples(1)};

        std::vector<Section> sections;
        for (std::size_t j = 0; j < inputDelays.size(); ++j) {
            // S_j is the stored recursive-memory bound at input cessation:
            // earlier sections can increase their output peak by at most
            // (1+2q)^j, and v_j = x_j + q*s_j gives
            // |s_j| <= |x_j|/(1-q). This is a real-arithmetic analytic
            // template using the stored float q widened above.
            sections.push_back({"input", inputDelays[j],
                                std::pow(sectionPeakGain, static_cast<double>(j)) / (1.0 - g)});
        }
        for (std::size_t j = 0; j < leftDelays.size(); ++j) {
            sections.push_back({"outputL", leftDelays[j],
                                measuredTapPeak * std::pow(sectionPeakGain, static_cast<double>(j)) / (1.0 - g)});
        }
        for (std::size_t j = 0; j < rightDelays.size(); ++j) {
            sections.push_back({"outputR", rightDelays[j],
                                measuredTapPeak * std::pow(sectionPeakGain, static_cast<double>(j)) / (1.0 - g)});
        }

        std::size_t maxDrain = 0;
        std::cout << std::setprecision(10) << "DS-10 proof-template illustration @ " << rate
                  << "Hz (stored float q_j=" << storedCoefficient
                  << ", wrapper defaults: Decay=0.5, Damp=0, Mix=1, realized T60_0="
                  << realizedDefaultT60 << "s; stored-q peak gain 1+2q=" << sectionPeakGain
                  << "; measured-window tap peak=" << measuredTapPeak << "):\n";
        for (const Section& section : sections) {
            std::size_t k = 1;
            while (std::pow(g, static_cast<double>(k)) * section.stateBound >= static_cast<double>(1e-20F)) ++k;
            const std::size_t drain = (k + 1) * section.delay;
            maxDrain = std::max(maxDrain, drain);
            const bool isInput = std::string(section.label) == "input";
            std::cout << "  " << section.label << " d_j=" << section.delay << " q_j=" << storedCoefficient
                      << " S_j=" << section.stateBound << (isInput ? " (analytic input-cessation bound)"
                                                                   : " (measured-window illustration; not a proved cessation bound)")
                      << " k_j=" << k << " D_j=(k_j+1)*d_j=" << drain << '\n';
        }
        std::cout << "  C3 qualification: no whole-chain cessation-state bound is established; output-section "
                     "rows are measured-window illustrations, not a propagated proof.\n";

        // Separately measured actual full-path silence -- NOT compared to any
        // historical additive timeout (ADR-006 correction note C3 forbids
        // that comparison), just reported alongside the proof-template drain.
        // This fixed finite horizon is deliberately not derived from a
        // whole-chain proof. It is long enough to demand an observed suffix
        // rather than extrapolating beyond the rendered record.
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
        constexpr std::size_t kRequiredObservedSilentSuffix = 20000;
        if (lastNonzero + 1 >= renderLength) {
            return fail("DS-10 measured-silence fixture ended before any exact silent sample was observed");
        }
        const std::size_t observedSilentSuffix = renderLength - (lastNonzero + 1);
        if (observedSilentSuffix < kRequiredObservedSilentSuffix) {
            return fail("DS-10 measured-silence fixture did not observe the required trailing exact-silence interval");
        }
        std::cout << std::setprecision(10) << "DS-10 @ " << rate << "Hz: measured full-path exact silence from sample "
                  << (lastNonzero + 1) << " onward, through the end of a " << renderLength
                  << "-sample render (observed exact-silence suffix=" << observedSilentSuffix
                  << " samples; required >= " << kRequiredObservedSilentSuffix
                  << "; proof-template max D_j=" << maxDrain << "; not compared to any historical "
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
    const int bracketResult = forEachBracketCascade(
        kBracketRates, kBracketKIn, kBracketKOut,
        [&](double rate, std::size_t kIn) -> int {
            auto cascade = buildCascade(rate, geometricTargets(kInputWindowMinSeconds, kInputWindowMaxSeconds, kIn),
                                        kGoldenCoefficient);
            if (cascade.empty()) return fail("DS-12 bracket input cascade did not prepare");
            for (std::size_t n = 0; n < 256; ++n) {
                if (runCascadeSample(cascade, n == 0 ? 1.0F : 0.0F).nonFinite) {
                    return fail("DS-12 bracket input cascade raised a fault on a finite fixture");
                }
            }
            ++configsChecked;
            return 0;
        },
        [&](double rate, std::size_t kOut) -> int {
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
            return 0;
        });
    if (bracketResult != 0) return bracketResult;

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

// ---------------------------------------------------------------------------
// Task 4b: DS-4, DS-7, DS-8, DS-9 -- full-wet-path measurements.
//
// These four cases are measurements of the wrapper's fixed K_in=4 / K_out=2
// configuration ("the full wet path", "out_L/out_R"), not of the
// K_in x K_out bracket that DS-1/2/5/12 sweep, so they deliberately do not
// go through forEachBracketCascade().
//
// Every one of them needs Decay/Damp/Mix control, and DiffusionStereoPath
// exposes none (it owns its ParameterAutomation privately and correctly so).
// OrderedReferencePath above -- the manual reference implementation whose
// sample-for-sample bit-exactness against the production wrapper is asserted
// by testProcessSampleMatchesExactOrderedReference() -- holds its automation
// as a public member, and is therefore the supported route to a controlled
// fixture. No accessor was added to src/.
// ---------------------------------------------------------------------------

// DS-A's own stated relative-energy limit (docs/testing.md), reused unchanged
// so a cascade result is directly comparable with the section result it
// extends.
constexpr double kEnergyToleranceRelative = 1e-5;

// Welch settings, declared here once and reported at every DS-7/DS-8 call
// site (ADR-006 correction note C2 requires the settings on record beside the
// number). The hop is always half a segment: the standard 50 % overlap at
// which consecutive periodic Hann segments sum to a constant.
//
// DS-7 estimates every sweep point at TWO segment lengths rather than one.
// The reason is measured, not stylistic: a Welch coherence estimate
// under-reports MSC when the system's impulse response is longer than the
// analysis segment, because energy that entered during one segment leaves
// during a later one. This fixture's impulse response is seconds long, so a
// single short-segment number would read as "the channels are incoherent"
// when it actually reads "the segment was short". 4096 samples (85.3 ms at
// 48 kHz, 11.72 Hz bins) and 16384 samples (341.3 ms, 2.93 Hz bins) bracket
// that effect, and testDs7CoherenceSegmentLengthSensitivity() below extends
// the same fixture to 65536 to show where the trend goes.
constexpr std::array<std::size_t, 2> kWelchSegmentLengths {4096, 16384};

// The single segment length DS-8's spectrum report uses. DS-8 compares the
// mono sum against each channel in the same record, so both sides of that
// ratio carry the same bias and it cancels; the shorter segment is kept there
// for bin-count economy.
constexpr std::size_t kWelchSegmentLength = 4096;
constexpr std::size_t kWelchHopSize = kWelchSegmentLength / 2;

// Analysis record length shared by DS-7, DS-8 and DS-9. 131072 samples is
// 2.73 s at 48 kHz: long enough to hold 63 complete 4096-sample segments or
// 15 complete 16384-sample ones, both far above C2's two-segment minimum.
constexpr std::size_t kAnalysisRecordSamples = 131072;

// Silent-bin floor: a bin is retained only if both auto-spectra reach 1e-12
// (-120 dB) of the record's peak auto-spectrum. Below that a bin carries the
// render's float round-off rather than its signal.
constexpr double kSilentBinFloorFraction = 1e-12;

// Declared symmetric short-lag range for the cross-correlation report: 5 ms.
// Chosen because it spans the mechanism that could displace an interchannel
// correlation peak away from lag zero -- the two output diffusion chains'
// total delays differ by 2.63 ms at 48 kHz / 2.77 ms at 44.1 kHz (ADR-006
// (f)) -- with margin on both sides. It is a declared window, not a bound.
constexpr double kShortLagSeconds = 0.005;

// The three T60_0 values DS-7 sweeps, in seconds. They are requested as times
// and converted to the normalized Decay the automation actually accepts,
// because T60_0 is what ADR-006 DS-7 names. They span a decade and a bit
// around the fixture's mid-decay setting and all three lie inside
// [T60_min, T60_max] at both fixture rates.
constexpr std::array<double, 3> kCoherenceT60Seconds {0.25, 1.0, 3.0};

// Decay-fit settings. The stride is NS-6's own (every 64th sample); the floor
// is two decades above the 1e-20 recursive-memory cutoff, below which the
// envelope is being truncated by that cutoff rather than decaying, so log10
// of it stops measuring the decay law.
constexpr std::size_t kDecayFitStride = 64;
constexpr double kDecayFitFloor = 1e-18;

// The comparability fixture's decay target: 1.0 s with damping bypassed, the
// same network decay NS-6's bare-FDN fixture uses, so the two fitted slopes
// printed side by side differ only by the path and not by the target.
constexpr double kComparabilityT60Seconds = 1.0;

// The DS-7 measurements below are only as trustworthy as the estimator that
// produces them, and a silently wrong Welch/MSC implementation would make
// every coherence number in this file meaningless without failing anything.
// This is the same discipline ADR-006 DS-2 applies to the allpass magnitude
// response -- measure the thing that is exact in theory rather than assume
// it -- turned on the analysis code itself. Three cases with known answers:
// identical channels (MSC = 1 exactly), one channel a pure delay of the other
// (MSC = 1 in theory, since a delay is a unit-magnitude transfer function),
// and two independent noise sequences (MSC is not 0 but ~1/segmentCount, the
// known bias of an S-segment estimate).
int testWelchCoherenceEstimatorSelfCheck() {
    constexpr std::size_t kRecord = 131072;
    constexpr std::size_t kSegment = 4096;
    constexpr std::size_t kDelay = 17;

    const std::vector<float> scriptA = makeNoiseScript(kRecord, 0x5D1E2C7BU);
    const std::vector<float> scriptB = makeNoiseScript(kRecord, 0x91A4F30DU);
    std::vector<double> a(kRecord);
    std::vector<double> b(kRecord);
    std::vector<double> delayed(kRecord, 0.0);
    for (std::size_t n = 0; n < kRecord; ++n) {
        a[n] = static_cast<double>(scriptA[n]);
        b[n] = static_cast<double>(scriptB[n]);
        if (n >= kDelay) delayed[n] = a[n - kDelay];
    }

    const auto identical = magnitudeSquaredCoherence(
        welchCrossSpectra(a, a, kSegment, kSegment / 2), kSilentBinFloorFraction);
    const auto shifted = magnitudeSquaredCoherence(
        welchCrossSpectra(a, delayed, kSegment, kSegment / 2), kSilentBinFloorFraction);
    const auto independent = magnitudeSquaredCoherence(
        welchCrossSpectra(a, b, kSegment, kSegment / 2), kSilentBinFloorFraction);
    if (!hasMinimumWelchSegments(identical.segmentCount) || !hasMinimumRetainedBins(identical.retainedBins)
        || !hasMinimumRetainedBins(shifted.retainedBins) || !hasMinimumRetainedBins(independent.retainedBins)) {
        return fail("Welch estimator self-check produced a vacuous estimate");
    }

    // A pure delay moves the correlation peak to exactly that lag and leaves
    // its magnitude at ~1; at lag 0 two shifted noise records are nearly
    // uncorrelated. This also pins maxShortLagCorrelation's lag convention,
    // which DS-7 reports.
    const double selfCorrelation = pearsonCorrelationAtLag(a, a, 0);
    const double shiftedAtZero = pearsonCorrelationAtLag(a, delayed, 0);
    const auto shiftedPeak = maxShortLagCorrelation(a, delayed, 240);

    std::cout << std::setprecision(10) << "Welch/MSC estimator self-check (" << identical.segmentCount
              << " x " << kSegment << " periodic-Hann segments, 50% overlap): identical channels MSC min="
              << identical.minimum << " mean=" << identical.mean << "; " << kDelay
              << "-sample pure delay MSC min=" << shifted.minimum << " median=" << shifted.median
              << " mean=" << shifted.mean << "; independent noise MSC mean=" << independent.mean
              << " (1/segmentCount=" << 1.0 / static_cast<double>(independent.segmentCount)
              << ") max=" << independent.maximum << "; Pearson rho(a,a,0)=" << selfCorrelation
              << ", rho(a,delayed,0)=" << shiftedAtZero << ", peak |rho|=" << shiftedPeak.magnitude
              << " at lag " << shiftedPeak.lag << '\n';

    if (identical.minimum < 1.0 - 1e-9 || identical.maximum > 1.0 + 1e-9) {
        return fail("Welch MSC of a channel against itself was not 1");
    }
    if (shifted.median < 0.95) return fail("Welch MSC of a pure delay was far below its theoretical 1");
    if (independent.mean > 0.2) return fail("Welch MSC of independent noise was implausibly high");
    if (std::abs(selfCorrelation - 1.0) > 1e-9) return fail("Pearson correlation of a record with itself was not 1");
    if (shiftedPeak.lag != static_cast<long long>(kDelay) || shiftedPeak.magnitude < 0.99) {
        return fail("short-lag correlation did not find a pure delay at its own lag");
    }
    return 0;
}

// Every DS-7/DS-8 call site above (and every case in the self-check just
// above this one) uses kAnalysisRecordSamples/kRecord, both exact multiples
// of every segment length x hop they're paired with, so welchCrossSpectra's
// `offset + segmentLength <= usable` loop bound has never actually stopped
// short of a would-be-partial trailing segment in any test in this file --
// the "drop the partial trailing segment rather than zero-pad it" branch its
// own comment describes has no coverage. This closes that gap with a record
// deliberately NOT a multiple of the hop: 3*segmentLength + segmentLength/4
// samples at the standard 50% hop. Full segments fit at offsets
// 0, hop, 2*hop, ...; the last offset satisfying offset + segmentLength <=
// usable is floor((usable - segmentLength) / hop), so the segment count is
// floor((usable - segmentLength) / hop) + 1. For segmentLength=4096,
// hop=2048, usable=13312: floor((13312-4096)/2048)+1 = floor(9216/2048)+1 =
// 4+1 = 5, i.e. the trailing 13312 - (4*2048 + 4096) = 1024 samples (a
// quarter of one segment) are dropped rather than zero-padded.
int testWelchPartialTrailingSegmentIsDropped() {
    constexpr std::size_t kSegment = 4096;
    constexpr std::size_t kHop = kSegment / 2;
    constexpr std::size_t kRecord = 3 * kSegment + kSegment / 4;  // 13312

    const std::vector<float> script = makeNoiseScript(kRecord, 0x2B6C91EFU);
    std::vector<double> samples(kRecord);
    for (std::size_t n = 0; n < kRecord; ++n) samples[n] = static_cast<double>(script[n]);

    const auto spectra = welchCrossSpectra(samples, samples, kSegment, kHop);
    std::cout << "Welch partial-trailing-segment boundary check: record=" << kRecord << " segment=" << kSegment
              << " hop=" << kHop << " -> segmentCount=" << spectra.segmentCount << " (expected 5)\n";
    if (spectra.segmentCount != 5) {
        return fail("Welch segment count over a non-hop-multiple record did not match the dropped-trailing-"
                    "segment formula");
    }
    return 0;
}

template <std::size_t N>
std::vector<double> toTargets(const std::array<double, N>& seconds) {
    return std::vector<double>(seconds.begin(), seconds.end());
}

// ---- full-path fixture harness ----

// Applies a normalized Decay/Damp/Mix triple so that it is in force from
// sample 0 with no in-flight 20 ms ramp. ParameterAutomation.h's contract:
// set*() publishes a target, checkForNewTargets() adopts it as a NEW ramp
// target starting from the ramp's current value, and reset() then snaps every
// smoother onto its target and cancels the ramp. reset() deliberately does
// not touch the network, so the network is reset separately, exactly as that
// comment requires. `reference` must already be prepared.
bool applyReferenceControls(OrderedReferencePath& reference, double decayNormalized, double damp, double mix) {
    if (!reference.automation.setDecay(decayNormalized) || !reference.automation.setDamp(damp)
        || !reference.automation.setMix(mix)) {
        return false;
    }
    reference.automation.checkForNewTargets();
    reference.automation.reset();
    reference.network.reset();
    for (auto& section : reference.input) section.reset();
    for (auto& section : reference.leftOutput) section.reset();
    for (auto& section : reference.rightOutput) section.reset();
    return true;
}

// Inverts ParameterAutomation's geometric Decay mapping so a fixture can be
// requested as a T60_0 in seconds. Clamped to [0,1] like the control itself.
double normalizedDecayForT60(const ParameterAutomation& automation, double t60Seconds) {
    const double low = automation.t60Min();
    const double high = automation.t60Max();
    if (!(low > 0.0) || !(high > low) || !(t60Seconds > 0.0)) return 0.0;
    return std::clamp(std::log(t60Seconds / low) / std::log(high / low), 0.0, 1.0);
}

// Mirrors ParameterAutomation::publish()'s mapping, including its two
// exactly-assigned endpoints, so a report states the T60_0 the automation
// actually realized rather than the one the fixture asked for.
double realizedT60Zero(const ParameterAutomation& automation, double decayNormalized) {
    if (decayNormalized <= 0.0) return automation.t60Min();
    if (decayNormalized >= 1.0) return automation.t60Max();
    return automation.t60Min() * std::pow(automation.t60Max() / automation.t60Min(), decayNormalized);
}

struct FullPathRender {
    std::vector<double> dry;      // the mono input actually presented
    std::vector<double> left;     // out_L, i.e. dry(m)*x + wet(m)*y_L'
    std::vector<double> right;    // out_R
    std::vector<double> wetLeft;  // y_L', pre-Mix, post-output-diffuser
    std::vector<double> wetRight; // y_R'
    double dryGain = 0.0;
    double wetGain = 0.0;
};

FullPathRender renderFullPath(OrderedReferencePath& reference, const std::vector<float>& script,
                              std::size_t skipSamples) {
    FullPathRender render;
    const std::size_t kept = script.size() > skipSamples ? script.size() - skipSamples : 0;
    render.dry.reserve(kept);
    render.left.reserve(kept);
    render.right.reserve(kept);
    render.wetLeft.reserve(kept);
    render.wetRight.reserve(kept);
    for (std::size_t n = 0; n < script.size(); ++n) {
        const auto sample = reference.processSampleDetailed(script[n]);
        render.dryGain = static_cast<double>(sample.dryGain);
        render.wetGain = static_cast<double>(sample.wetGain);
        if (n < skipSamples) continue;
        render.dry.push_back(static_cast<double>(script[n]));
        render.left.push_back(static_cast<double>(sample.left));
        render.right.push_back(static_cast<double>(sample.right));
        render.wetLeft.push_back(static_cast<double>(sample.wetLeft));
        render.wetRight.push_back(static_cast<double>(sample.wetRight));
    }
    return render;
}

double meanSquare(const std::vector<double>& signal) {
    if (signal.empty()) return 0.0;
    double sum = 0.0;
    for (const double value : signal) sum += value * value;
    return sum / static_cast<double>(signal.size());
}

double meanProduct(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t count = std::min(a.size(), b.size());
    if (count == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t n = 0; n < count; ++n) sum += a[n] * b[n];
    return sum / static_cast<double>(count);
}

double meanOf(const std::vector<double>& signal) {
    if (signal.empty()) return 0.0;
    double sum = 0.0;
    for (const double value : signal) sum += value;
    return sum / static_cast<double>(signal.size());
}

double covarianceOf(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t count = std::min(a.size(), b.size());
    if (count < 2) return 0.0;
    const double meanA = meanOf(a);
    const double meanB = meanOf(b);
    double sum = 0.0;
    for (std::size_t n = 0; n < count; ++n) sum += (a[n] - meanA) * (b[n] - meanB);
    return sum / static_cast<double>(count);
}

std::vector<double> sumOf(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t count = std::min(a.size(), b.size());
    std::vector<double> result(count);
    for (std::size_t n = 0; n < count; ++n) result[n] = a[n] + b[n];
    return result;
}

std::vector<double> scaledCopy(const std::vector<double>& signal, double gain) {
    std::vector<double> result(signal.size());
    for (std::size_t n = 0; n < signal.size(); ++n) result[n] = signal[n] * gain;
    return result;
}

// Sigma n*x[n]^2 / Sigma x[n]^2, in samples, relative to the buffer's own
// first sample. Truncation-dependent for a decaying response: the window is
// always declared at the call site.
double energyCentroidSamples(const std::vector<double>& signal) {
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t n = 0; n < signal.size(); ++n) {
        const double energy = signal[n] * signal[n];
        weighted += static_cast<double>(n) * energy;
        total += energy;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

std::size_t firstNonzeroIndex(const std::vector<double>& signal) {
    for (std::size_t n = 0; n < signal.size(); ++n) {
        if (signal[n] != 0.0) return n;
    }
    return signal.size();
}

double rmsOf(const std::vector<double>& signal) { return std::sqrt(meanSquare(signal)); }

// ---------------------------------------------------------------------------
// DS-4 (first half) -- energy conservation of each full cascade.
//
// DS-A closed this for a single section (relative error <= 1e-5 for an impulse
// and for bounded noise); 4a did not extend it to cascades. Here it is the
// wrapper's three actual cascades -- the K_in=4 input chain and the two K_out=2
// output chains -- at both fixture rates, for an impulse and for the shared
// deterministic noise fixture, each drained by 128*sum(d) zeros so the chain's
// own tail is inside the measured window rather than truncated out of it.
// ---------------------------------------------------------------------------

struct CascadeEnergyResult {
    double relativeError = 0.0;
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    std::size_t drainSamples = 0;
    bool valid = false;
};

CascadeEnergyResult cascadeEnergyError(double rate, const std::vector<double>& targets,
                                       const std::vector<float>& script) {
    auto cascade = buildCascade(rate, targets, kGoldenCoefficient);
    if (cascade.empty()) return {};
    std::size_t delaySum = 0;
    for (const auto& section : cascade) delaySum += section.delaySamples();

    CascadeEnergyResult result;
    result.drainSamples = 128 * std::max<std::size_t>(delaySum, 1);
    std::vector<double> output;
    output.reserve(script.size() + result.drainSamples);
    for (std::size_t n = 0; n < script.size() + result.drainSamples; ++n) {
        const float input = n < script.size() ? script[n] : 0.0F;
        result.inputEnergy += static_cast<double>(input) * static_cast<double>(input);
        const auto sample = runCascadeSample(cascade, input);
        if (sample.nonFinite) return {};
        output.push_back(static_cast<double>(sample.value));
        result.outputEnergy += output.back() * output.back();
    }
    if (!hasNonzeroSample(output) || !(result.inputEnergy > 0.0)) return {}; // anti-vacuity guard
    result.relativeError = std::abs(result.outputEnergy - result.inputEnergy) / result.inputEnergy;
    result.valid = true;
    return result;
}

int testDs4CascadeEnergyConservation() {
    const std::array<const char*, 3> labels {"input K_in=4", "output-L K_out=2", "output-R K_out=2"};
    double worstImpulse = 0.0;
    double worstNoise = 0.0;

    for (const double rate : kBracketRates) {
        const DiffusionStereoConfig config = validConfig(rate);
        const std::array<std::vector<double>, 3> chains {
            toTargets(config.inputDelaySeconds),
            toTargets(config.leftOutputDelaySeconds),
            toTargets(config.rightOutputDelaySeconds),
        };
        const std::vector<float> impulse {1.0F};
        const std::vector<float> noise = makeNoiseScript(4096);

        for (std::size_t chain = 0; chain < chains.size(); ++chain) {
            const CascadeEnergyResult impulseResult = cascadeEnergyError(rate, chains[chain], impulse);
            const CascadeEnergyResult noiseResult = cascadeEnergyError(rate, chains[chain], noise);
            if (!impulseResult.valid || !noiseResult.valid) {
                return fail("DS-4 cascade energy fixture failed to render a finite, nonzero response");
            }
            worstImpulse = std::max(worstImpulse, impulseResult.relativeError);
            worstNoise = std::max(worstNoise, noiseResult.relativeError);
            std::cout << std::setprecision(10) << "DS-4 energy " << labels[chain] << " @ " << rate
                      << "Hz: impulse relative error=" << impulseResult.relativeError
                      << ", 4096-sample noise relative error=" << noiseResult.relativeError
                      << " (drain=" << impulseResult.drainSamples << " zeros)\n";
        }
    }

    std::cout << std::setprecision(10) << "DS-4 energy conservation: worst impulse=" << worstImpulse
              << ", worst noise=" << worstNoise << " (stated tolerance " << kEnergyToleranceRelative
              << ", DS-A's own single-section limit reused unchanged)\n";
    if (worstImpulse > kEnergyToleranceRelative || worstNoise > kEnergyToleranceRelative) {
        return fail("DS-4 cascade energy relative error exceeded the stated 1e-5 tolerance");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DS-4 (second half) -- full-path decay-law regression.
//
// ADR-006 correction note C1: no full-path fitted-decay equality with the bare
// FDN is implied or accepted. This test therefore asserts NEITHER equality NOR
// inequality against NS-6. It fits the full wet path with NS-6's own method,
// re-measures the bare network with the identical method purely so the two
// numbers can be printed side by side, and leaves interpretation to review.
// NS-6 itself (tests/FdnTests.cpp) is untouched and remains the network-only
// regression C1 says it must remain.
// ---------------------------------------------------------------------------

struct DecayFit {
    double slopeLog10PerSample = 0.0;
    double impliedT60Seconds = 0.0;
    std::size_t windowStart = 0;
    std::size_t windowStop = 0;
    std::size_t pointCount = 0;
    bool valid = false;
};

// The last index whose magnitude still reaches kDecayFitFloor. Past it the
// envelope is being truncated by the 1e-20 recursive-memory cutoff rather than
// decaying, and log10 of it stops measuring the decay law.
std::size_t decayFitFloorStop(const std::vector<double>& signal) {
    for (std::size_t n = signal.size(); n > 0; --n) {
        if (std::abs(signal[n - 1]) >= kDecayFitFloor) return n;
    }
    return 0;
}

// NS-6's method verbatim (tests/FdnTests.cpp testDecayLaw): linear regression
// of log10|x[n]| on n over a declared window, sampling every kDecayFitStride
// samples and skipping exact zeros. Reproduced here rather than shared with
// FdnTests.cpp because NS-6 is a closed, independently verified regression
// this sub-task must not touch; DS-4's fairness requirement is that the
// *method* match, not that the code be shared.
DecayFit fitLog10DecaySlope(const std::vector<double>& signal, std::size_t start, std::size_t stop,
                            double sampleRate) {
    DecayFit fit;
    fit.windowStart = start;
    fit.windowStop = stop;
    if (start >= stop || stop > signal.size()) return fit;

    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;
    for (std::size_t n = start; n < stop; n += kDecayFitStride) {
        const double magnitude = std::abs(signal[n]);
        if (magnitude <= 0.0) continue;
        const double x = static_cast<double>(n);
        const double y = std::log10(magnitude);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
        ++fit.pointCount;
    }
    if (!hasMinimumFitPoints(fit.pointCount)) return fit; // anti-vacuity guard
    const double count = static_cast<double>(fit.pointCount);
    const double meanX = sumX / count;
    const double meanY = sumY / count;
    const double denominator = sumXX - count * meanX * meanX;
    if (!(std::abs(denominator) > 0.0)) return fit;
    fit.slopeLog10PerSample = (sumXY - count * meanX * meanY) / denominator;
    if (!std::isfinite(fit.slopeLog10PerSample) || !(fit.slopeLog10PerSample < 0.0)) return fit;
    // A log10-domain slope s per sample reaches -60 dB (three decades) after
    // -3/s samples, which is the T60 the fit implies.
    fit.impliedT60Seconds = -3.0 / (fit.slopeLog10PerSample * sampleRate);
    fit.valid = true;
    return fit;
}

// NS-6 starts its window at 2*m_min, "before the shortest line has even
// contributed". The full wet path adds two stages NS-6's bare network has
// not: an input diffusion chain that keeps injecting for its own tail, and an
// output diffusion chain that shifts energy later. The declared full-path
// start is therefore 2*m_min + sum(input delays) + max over the two output
// chains of sum(output delays) -- NS-6's onset skip plus one complete fill of
// the longest diffusion path.
std::size_t fullPathFitStart(OrderedReferencePath& reference) {
    std::size_t inputSum = 0;
    for (const auto& section : reference.input) inputSum += section.delaySamples();
    std::size_t leftSum = 0;
    std::size_t rightSum = 0;
    for (std::size_t index = 0; index < reference.leftOutput.size(); ++index) {
        leftSum += reference.leftOutput[index].delaySamples();
        rightSum += reference.rightOutput[index].delaySamples();
    }
    return 2 * reference.network.delaySamples(0) + inputSum + std::max(leftSum, rightSum);
}

void reportDecayFit(const char* label, const DecayFit& fit, double sampleRate) {
    std::cout << std::setprecision(10) << "DS-4 decay " << label << ": slope=" << fit.slopeLog10PerSample
              << " log10/sample, implied T60=" << fit.impliedT60Seconds << "s, fit window=["
              << fit.windowStart << ", " << fit.windowStop << ") samples (["
              << std::setprecision(6) << static_cast<double>(fit.windowStart) / sampleRate << "s, "
              << static_cast<double>(fit.windowStop) / sampleRate << "s)), points=" << fit.pointCount
              << " (stride " << kDecayFitStride << ")\n";
}

// One declared fixture: full-scale impulse, one wet-path render, both channels
// fitted separately (the two output chains have different poles, so averaging
// them would fold two different observations into one number).
int measureFullPathDecay(double sampleRate, const char* fixtureLabel, double decayNormalized, double damp,
                         std::size_t renderSamples, double& leftSlopeOut, double& rightSlopeOut) {
    OrderedReferencePath reference;
    const DiffusionStereoConfig config = validConfig(sampleRate);
    if (!reference.prepare(config)) return fail("DS-4 decay fixture preparation failed");
    if (!applyReferenceControls(reference, decayNormalized, damp, 1.0)) {
        return fail("DS-4 decay fixture control application failed");
    }
    const double t60Zero = realizedT60Zero(reference.automation, decayNormalized);

    std::vector<float> script(renderSamples, 0.0F);
    script[0] = 1.0F;
    const FullPathRender render = renderFullPath(reference, script, 0);
    if (!hasNonzeroSample(render.left) || !hasNonzeroSample(render.right)) {
        return fail("DS-4 anti-vacuity: full-path decay fixture rendered silence");
    }

    const std::size_t start = fullPathFitStart(reference);
    const std::size_t leftStop = std::min(decayFitFloorStop(render.left), render.left.size());
    const std::size_t rightStop = std::min(decayFitFloorStop(render.right), render.right.size());
    const DecayFit leftFit = fitLog10DecaySlope(render.left, start, leftStop, sampleRate);
    const DecayFit rightFit = fitLog10DecaySlope(render.right, start, rightStop, sampleRate);
    if (!leftFit.valid || !rightFit.valid) {
        return fail("DS-4 full-path decay fit produced too few points or a non-decaying slope");
    }

    std::cout << std::setprecision(10) << "DS-4 fixture '" << fixtureLabel << "' @ " << sampleRate
              << "Hz: full-scale impulse, Mix=1.0 (full wet), normalized Decay=" << decayNormalized
              << " (realized T60_0=" << t60Zero << "s), normalized Damp=" << damp << ", render="
              << renderSamples << " samples\n";
    reportDecayFit("full path out_L", leftFit, sampleRate);
    reportDecayFit("full path out_R", rightFit, sampleRate);
    leftSlopeOut = leftFit.slopeLog10PerSample;
    rightSlopeOut = rightFit.slopeLog10PerSample;
    return 0;
}

int testDs4FullPathDecayLawRecorded() {
    for (const double rate : kBracketRates) {
        // Fixture A: the minimum-Decay / high-Damp fixture C1 explicitly
        // requires. Normalized Decay=0.0 is T60_min exactly (a few
        // milliseconds), Damp=1.0 is the fixture's full 48 dB of excess
        // high-frequency damping. One second of render is far longer than the
        // network's own T60 here on purpose: the diffusion chains' own tails
        // outlast it, which is the point of measuring the full path.
        double minDecayLeft = 0.0;
        double minDecayRight = 0.0;
        if (const int result = measureFullPathDecay(rate, "minimum Decay, high Damp", 0.0, 1.0,
                                                    static_cast<std::size_t>(rate * 1.0), minDecayLeft,
                                                    minDecayRight);
            result != 0) {
            return result;
        }

        // Fixture B: a comparability fixture at the same network decay target
        // NS-6's bare-FDN fixture uses (T60_0 = T60_pi = 1.0 s), so the
        // side-by-side numbers below differ by the path and not by the target.
        OrderedReferencePath probe;
        if (!probe.prepare(validConfig(rate))) return fail("DS-4 comparability probe preparation failed");
        const double comparabilityDecay = normalizedDecayForT60(probe.automation, kComparabilityT60Seconds);
        double comparabilityLeft = 0.0;
        double comparabilityRight = 0.0;
        if (const int result = measureFullPathDecay(rate, "T60_0 = 1.0 s, Damp bypassed", comparabilityDecay,
                                                    0.0, static_cast<std::size_t>(rate * 3.0),
                                                    comparabilityLeft, comparabilityRight);
            result != 0) {
            return result;
        }

        // The bare network, re-measured here with the identical method purely
        // so the two numbers can be printed together. NS-6's own assertion
        // lives in tests/FdnTests.cpp and is untouched.
        FeedbackDelayNetwork bare;
        if (!bare.prepare(rate, 8, kTMin, kTMax, kComparabilityT60Seconds, kComparabilityT60Seconds)) {
            return fail("DS-4 bare-network reference preparation failed");
        }
        const auto bareSamples = static_cast<std::size_t>(rate * kComparabilityT60Seconds * 3.0);
        std::vector<float> bareInput(bareSamples, 0.0F);
        std::vector<float> bareOutput(bareSamples, 0.0F);
        bareInput[0] = 1.0F;
        bare.process(bareInput.data(), bareOutput.data(), bareSamples);
        std::vector<double> bareDouble(bareSamples);
        for (std::size_t n = 0; n < bareSamples; ++n) bareDouble[n] = static_cast<double>(bareOutput[n]);
        if (!hasNonzeroSample(bareDouble)) return fail("DS-4 anti-vacuity: bare-network reference was silent");

        // Two windows on the same buffer: NS-6's own (2*m_min to end-0.2s) and
        // this test's full-path window, so the comparison cannot be confounded
        // by the window choice alone.
        const std::size_t ns6Start = bare.delaySamples(0) * 2;
        const std::size_t ns6Stop = bareSamples - static_cast<std::size_t>(rate * 0.2);
        const DecayFit bareNs6Window = fitLog10DecaySlope(bareDouble, ns6Start, ns6Stop, rate);
        const std::size_t fullStart = fullPathFitStart(probe);
        const DecayFit bareFullWindow =
            fitLog10DecaySlope(bareDouble, fullStart, std::min(decayFitFloorStop(bareDouble), ns6Stop), rate);
        if (!bareNs6Window.valid || !bareFullWindow.valid) {
            return fail("DS-4 bare-network reference fit produced too few points or a non-decaying slope");
        }
        reportDecayFit("bare network (NS-6 window)", bareNs6Window, rate);
        reportDecayFit("bare network (full-path window)", bareFullWindow, rate);

        const double idealSlope = -3.0 / (rate * kComparabilityT60Seconds);
        std::cout << std::setprecision(10) << "DS-4 side-by-side @ " << rate
                  << "Hz at T60_0=1.0s (NO equality or inequality claim is made or asserted, per ADR-006 "
                     "correction note C1): full path out_L=" << comparabilityLeft << ", out_R="
                  << comparabilityRight << ", bare network (NS-6 window)=" << bareNs6Window.slopeLog10PerSample
                  << ", bare network (full-path window)=" << bareFullWindow.slopeLog10PerSample
                  << ", homogeneous-law ideal=" << idealSlope << " log10/sample\n";
        // Two reference slopes printed beside the minimum-Decay result, both
        // arithmetic rather than measured, so a reader can see what the fitted
        // value is and is not close to. The first is the slope the network's
        // own T60_min would produce alone. The second is the slope of the
        // slowest pole anywhere in the diffusion chains -- the longest input
        // section's, radius g_ap^(1/d), i.e. log10(g_ap)/d per sample. NO
        // claim is made that the measured slope equals either; they are
        // context for review, which is what C1 asks this case to produce.
        std::size_t longestInputDelay = 0;
        for (const auto& section : probe.input) {
            longestInputDelay = std::max(longestInputDelay, section.delaySamples());
        }
        const double slowestDiffuserSlope =
            std::log10(kGoldenCoefficient) / static_cast<double>(longestInputDelay);
        std::cout << std::setprecision(10) << "DS-4 minimum-Decay/high-Damp full-path slopes @ " << rate
                  << "Hz: out_L=" << minDecayLeft << ", out_R=" << minDecayRight
                  << " log10/sample (recorded only; for context the network's own T60_min slope at this "
                     "setting is " << -3.0 / (rate * probe.automation.t60Min())
                  << " log10/sample, and the slowest diffusion pole -- the longest input section, d="
                  << longestInputDelay << " -- decays at " << slowestDiffuserSlope << " log10/sample)\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DS-7 -- interchannel coherence, diagnostic only.
//
// ADR-006 correction note C2: for an ideal fixed linear mono-input path,
// L = H_L X and R = H_R X imply MSC = 1 wherever both spectra are nonzero, so
// a zero-coherence target is prohibited as an acceptance criterion and the
// prediction of decorrelation is conditional on an unproven assumption. This
// test therefore RECORDS the Welch MSC and, separately, the time-domain
// correlation coefficient, and gates only on the estimate being
// non-vacuous -- never on the coherence value itself.
//
// Mix is held at 1.0 (full wet) throughout. A dry contribution is summed
// identically into both channels (ADR-006 (g)), so any Mix < 1 would drive
// both MSC and correlation toward 1 for a reason that has nothing to do with
// the stereo path.
// ---------------------------------------------------------------------------

void reportCoherence(const char* excitation, double rate, double t60Zero, const CoherenceSummary& summary,
                     const aetherfield::dsp_test::WelchCrossSpectra& spectra, double zeroLag,
                     const aetherfield::dsp_test::ShortLagCorrelation& shortLag) {
    std::cout << std::setprecision(8) << "DS-7 " << excitation << " @ " << rate << "Hz, T60_0="
              << t60Zero << "s: segments=" << spectra.segmentCount << " x " << spectra.segmentLength
              << " samples, hop=" << spectra.hopSize << " (50% overlap), periodic Hann, FFT="
              << spectra.fftLength << ", silent-bin floor=" << kSilentBinFloorFraction
              << " of peak PSD (absolute " << summary.floorAbsolute << "), retained "
              << summary.retainedBins << "/" << summary.examinedBins
              << " bins (DC and Nyquist excluded); MSC min=" << summary.minimum << " median="
              << summary.median << " mean=" << summary.mean << " max=" << summary.maximum
              << "; normalized cross-correlation at lag 0=" << zeroLag << ", max |rho| over +/-"
              << kShortLagSeconds << "s=" << shortLag.magnitude << " (signed " << shortLag.signedValue
              << ") at lag " << shortLag.lag << " samples\n";
}

int measureCoherenceRecord(const char* excitation, double rate, double t60Zero,
                           const std::vector<double>& left, const std::vector<double>& right,
                           long long maxLag) {
    if (!hasNonzeroSample(left) || !hasNonzeroSample(right)) {
        return fail("DS-7 anti-vacuity: coherence fixture rendered a silent channel");
    }
    // The time-domain correlation is a property of the record, not of a
    // spectral setting, so it is computed once and repeated on each line for
    // readability.
    const double zeroLag = pearsonCorrelationAtLag(left, right, 0);
    const auto shortLag = maxShortLagCorrelation(left, right, maxLag);
    if (!std::isfinite(zeroLag)) return fail("DS-7 correlation summary was not finite");

    for (const std::size_t segmentLength : kWelchSegmentLengths) {
        const auto spectra = welchCrossSpectra(left, right, segmentLength, segmentLength / 2);
        if (!hasMinimumWelchSegments(spectra.segmentCount)) {
            return fail("DS-7 anti-vacuity: coherence estimate had fewer than two complete Welch segments");
        }
        const CoherenceSummary summary = magnitudeSquaredCoherence(spectra, kSilentBinFloorFraction);
        if (!hasMinimumRetainedBins(summary.retainedBins)) {
            return fail("DS-7 anti-vacuity: every coherence bin fell under the silent-bin floor");
        }
        if (!std::isfinite(summary.mean)) return fail("DS-7 coherence summary was not finite");
        reportCoherence(excitation, rate, t60Zero, summary, spectra, zeroLag, shortLag);
    }
    return 0;
}

int testDs7InterchannelCoherenceRecorded() {
    const auto analysis = static_cast<std::size_t>(kAnalysisRecordSamples);
    for (const double rate : kBracketRates) {
        const auto maxLag = static_cast<long long>(std::llround(kShortLagSeconds * rate));
        for (const double t60Target : kCoherenceT60Seconds) {
            OrderedReferencePath noisePath;
            if (!noisePath.prepare(validConfig(rate))) return fail("DS-7 fixture preparation failed");
            const double decay = normalizedDecayForT60(noisePath.automation, t60Target);
            const double t60Zero = realizedT60Zero(noisePath.automation, decay);
            if (!applyReferenceControls(noisePath, decay, 0.0, 1.0)) {
                return fail("DS-7 fixture control application failed");
            }

            // Continuous deterministic broadband noise (the plan's required
            // DS-7 excitation), with a warm-up of one T60_0 discarded so the
            // analyzed record is the steady-state response rather than the
            // buildup transient. The warm-up is capped at 3 s so a long-decay
            // sweep point cannot dominate the suite's runtime.
            const auto warmup = static_cast<std::size_t>(std::min(t60Zero, 3.0) * rate);
            const std::vector<float> noiseScript = makeNoiseScript(warmup + analysis);
            const FullPathRender noiseRender = renderFullPath(noisePath, noiseScript, warmup);
            if (const int result = measureCoherenceRecord("noise  ", rate, t60Zero, noiseRender.left,
                                                          noiseRender.right, maxLag);
                result != 0) {
                return result;
            }

            // Impulse excitation, which ADR-006 DS-7 also names. Its segments
            // are successive windows of one decaying response rather than
            // independent realizations, so this estimate is reported for
            // completeness beside the noise one and is NOT the primary
            // result; it is still a multi-segment Welch average and never a
            // single periodogram, which the plan prohibits.
            OrderedReferencePath impulsePath;
            if (!impulsePath.prepare(validConfig(rate))) return fail("DS-7 impulse fixture preparation failed");
            if (!applyReferenceControls(impulsePath, decay, 0.0, 1.0)) {
                return fail("DS-7 impulse fixture control application failed");
            }
            const std::size_t impulseLength = 2 * impulsePath.network.delaySamples(0) + analysis;
            std::vector<float> impulseScript(impulseLength, 0.0F);
            impulseScript[0] = 1.0F;
            const FullPathRender impulseRender = renderFullPath(impulsePath, impulseScript, 0);
            const std::size_t arrival = firstNonzeroIndex(impulseRender.left);
            if (arrival + analysis > impulseRender.left.size()) {
                return fail("DS-7 impulse fixture was too short to hold a full analysis record");
            }
            const std::vector<double> impulseLeft(impulseRender.left.begin() + static_cast<long>(arrival),
                                                  impulseRender.left.begin()
                                                      + static_cast<long>(arrival + analysis));
            const std::vector<double> impulseRight(impulseRender.right.begin() + static_cast<long>(arrival),
                                                   impulseRender.right.begin()
                                                       + static_cast<long>(arrival + analysis));
            if (const int result =
                    measureCoherenceRecord("impulse", rate, t60Zero, impulseLeft, impulseRight, maxLag);
                result != 0) {
                return result;
            }
        }
    }
    std::cout << "DS-7: recorded only. ADR-006 correction note C2 -- the zero-coherence prediction is "
                 "conditional on an unproven assumption, a zero-MSC target is prohibited as an acceptance "
                 "criterion, and nothing above is gated on a coherence value.\n";
    return 0;
}

// DS-7 methodology diagnostic: how much of the reported MSC is the estimator.
//
// C2's own algebra says a fixed linear mono-input path has MSC = 1 wherever
// both spectra are nonzero, yet the estimates above report a median well
// below 1 that rises with segment length and falls as T60_0 rises. That is
// the known Welch coherence bias for a system whose impulse response is
// longer than the analysis segment: energy that entered during one segment
// leaves during a later one, so the estimator cannot see that the two
// channels share a driver, and it under-reports coherence by an amount set by
// the segment length rather than by the path.
//
// This is not a third DS-7 result; it is the evidence a reader needs in order
// to read the DS-7 table correctly. One fixture on a longer record is
// re-estimated at three segment lengths, each well above C2's two-segment
// minimum, extending the table's own two-length trend far enough to show
// where it is heading.
int testDs7CoherenceSegmentLengthSensitivity() {
    constexpr double kRate = 48000.0;
    constexpr std::size_t kDiagnosticRecordSamples = 262144;
    constexpr std::array<std::size_t, 3> kSegmentLengths {4096, 16384, 65536};

    OrderedReferencePath reference;
    if (!reference.prepare(validConfig(kRate))) return fail("DS-7 sensitivity fixture preparation failed");
    const double decay = normalizedDecayForT60(reference.automation, kComparabilityT60Seconds);
    const double t60Zero = realizedT60Zero(reference.automation, decay);
    if (!applyReferenceControls(reference, decay, 0.0, 1.0)) {
        return fail("DS-7 sensitivity fixture control application failed");
    }
    const auto warmup = static_cast<std::size_t>(kComparabilityT60Seconds * kRate);
    const std::vector<float> script = makeNoiseScript(warmup + kDiagnosticRecordSamples);
    const FullPathRender render = renderFullPath(reference, script, warmup);
    if (!hasNonzeroSample(render.left) || !hasNonzeroSample(render.right)) {
        return fail("DS-7 anti-vacuity: the sensitivity fixture rendered a silent channel");
    }

    for (const std::size_t segmentLength : kSegmentLengths) {
        const auto spectra = welchCrossSpectra(render.left, render.right, segmentLength, segmentLength / 2);
        if (!hasMinimumWelchSegments(spectra.segmentCount)) {
            return fail("DS-7 sensitivity estimate had fewer than two complete Welch segments");
        }
        const CoherenceSummary summary = magnitudeSquaredCoherence(spectra, kSilentBinFloorFraction);
        if (!hasMinimumRetainedBins(summary.retainedBins)) {
            return fail("DS-7 sensitivity estimate retained too few bins");
        }
        std::cout << std::setprecision(8) << "DS-7 segment-length sensitivity @ " << kRate << "Hz, T60_0="
                  << t60Zero << "s (" << kDiagnosticRecordSamples
                  << "-sample noise record, periodic Hann, 50% overlap): segment=" << segmentLength
                  << " samples (" << 1000.0 * static_cast<double>(segmentLength) / kRate << "ms, "
                  << spectra.segmentCount << " segments, FFT=" << spectra.fftLength << "), retained "
                  << summary.retainedBins << "/" << summary.examinedBins << " bins, MSC min="
                  << summary.minimum << " median=" << summary.median << " mean=" << summary.mean
                  << " max=" << summary.maximum << '\n';
    }
    std::cout << "DS-7 sensitivity: the MSC estimate rises with segment length on an unchanged fixture, so "
                 "the DS-7 table's values are segment-length-limited estimates and are NOT evidence that "
                 "out_L and out_R are incoherent. ADR-006 correction note C2's own algebra (MSC = 1 for a "
                 "fixed linear mono-input path wherever both spectra are nonzero) is the prediction these "
                 "estimates are biased away from; recorded, not resolved.\n";
    return 0;
}

// ---------------------------------------------------------------------------
// DS-8 -- mono compatibility and the Mix consequence.
//
// ADR-006 correction note C5 requires E[L^2], E[R^2], E[L R], the actual sum
// power and the dry/wet covariance across the Mix sweep, and says the
// +3.01 dB incoherent-wet-sum figure is conditional on all-lag
// uncorrelatedness and is NOT an acceptance value. It is reported here as a
// measured comparison and gates nothing.
// ---------------------------------------------------------------------------

int testDs8MonoCompatibilityAcrossMixSweep() {
    constexpr double kRate = 48000.0;
    constexpr std::array<double, 5> kMixSweep {0.0, 0.25, 0.5, 0.75, 1.0};
    constexpr std::array<double, 5> kBandEdgesHz {0.0, 500.0, 2000.0, 8000.0, 24000.0};

    std::cout << std::setprecision(8) << "DS-8 fixture: " << kRate
              << "Hz, continuous deterministic broadband noise, T60_0=" << kComparabilityT60Seconds
              << "s with Damp bypassed, one-T60_0 warm-up discarded, " << kAnalysisRecordSamples
              << "-sample analysis record\n";

    for (const double mix : kMixSweep) {
        OrderedReferencePath reference;
        if (!reference.prepare(validConfig(kRate))) return fail("DS-8 fixture preparation failed");
        const double decay = normalizedDecayForT60(reference.automation, kComparabilityT60Seconds);
        if (!applyReferenceControls(reference, decay, 0.0, mix)) {
            return fail("DS-8 fixture control application failed");
        }
        const auto warmup = static_cast<std::size_t>(kComparabilityT60Seconds * kRate);
        const std::vector<float> script = makeNoiseScript(warmup + kAnalysisRecordSamples);
        const FullPathRender render = renderFullPath(reference, script, warmup);
        if (!hasNonzeroSample(render.dry)) return fail("DS-8 anti-vacuity: the dry fixture was silent");
        if (!hasNonzeroSample(render.wetLeft) || !hasNonzeroSample(render.wetRight)) {
            return fail("DS-8 anti-vacuity: the pre-Mix wet path was silent");
        }

        const std::vector<double> sum = sumOf(render.left, render.right);
        const double eLL = meanSquare(render.left);
        const double eRR = meanSquare(render.right);
        const double eLR = meanProduct(render.left, render.right);
        const double eSum = meanSquare(sum);
        // E[(L+R)^2] = E[L^2] + E[R^2] + 2E[LR] is an identity; its residual
        // is a self-check on the accumulation, not a property of the path.
        const double identityResidual = std::abs(eSum - (eLL + eRR + 2.0 * eLR));

        // The dry and wet components of out_L as actually realized at this
        // Mix, i.e. after the Mix gains. The covariance therefore scales with
        // dry(m)*wet(m) and is exactly 0 at both endpoints, which is a true
        // statement about the realized signals; the Mix-independent
        // normalized correlation of x against y_L' is reported beside it.
        const std::vector<double> dryComponent = scaledCopy(render.dry, render.dryGain);
        const std::vector<double> wetComponentLeft = scaledCopy(render.wetLeft, render.wetGain);
        const std::vector<double> wetComponentRight = scaledCopy(render.wetRight, render.wetGain);
        const double covDryWetLeft = covarianceOf(dryComponent, wetComponentLeft);
        const double covDryWetRight = covarianceOf(dryComponent, wetComponentRight);
        const double rhoDryWetLeft = pearsonCorrelationAtLag(render.dry, render.wetLeft, 0);
        const double rhoDryWetRight = pearsonCorrelationAtLag(render.dry, render.wetRight, 0);

        std::cout << std::setprecision(10) << "DS-8 Mix=" << mix << " (dry gain=" << render.dryGain
                  << ", wet gain=" << render.wetGain << "): E[L^2]=" << eLL << ", E[R^2]=" << eRR
                  << ", E[L*R]=" << eLR << ", E[(L+R)^2]=" << eSum << " (identity residual="
                  << identityResidual << "), per-channel power sum=" << eLL + eRR
                  << "; mono-sum level relative to out_L=" << 10.0 * std::log10(eSum / eLL)
                  << "dB; cov(dry, wet_L)=" << covDryWetLeft << ", cov(dry, wet_R)=" << covDryWetRight
                  << "; Mix-independent normalized dry/wet correlation rho_L=" << rhoDryWetLeft
                  << ", rho_R=" << rhoDryWetRight << '\n';

        // "L + R level and spectrum against each channel": Welch PSD ratios of
        // the mono sum to each channel, in four declared bands. Same estimator
        // and settings as DS-7.
        const auto channelSpectra = welchCrossSpectra(render.left, render.right, kWelchSegmentLength,
                                                      kWelchHopSize);
        const auto sumSpectra = welchCrossSpectra(sum, sum, kWelchSegmentLength, kWelchHopSize);
        if (!hasMinimumWelchSegments(channelSpectra.segmentCount)
            || !hasMinimumWelchSegments(sumSpectra.segmentCount)) {
            return fail("DS-8 anti-vacuity: mono-sum spectrum had fewer than two complete Welch segments");
        }
        std::cout << std::setprecision(6) << "DS-8 Mix=" << mix << " mono-sum spectrum ("
                  << sumSpectra.segmentCount << " x " << sumSpectra.segmentLength
                  << " periodic-Hann segments, 50% overlap, FFT=" << sumSpectra.fftLength << "): ";
        for (std::size_t band = 0; band + 1 < kBandEdgesHz.size(); ++band) {
            const double binWidth = kRate / static_cast<double>(sumSpectra.fftLength);
            const auto lowBin = std::max<std::size_t>(1, static_cast<std::size_t>(kBandEdgesHz[band] / binWidth));
            const auto highBin = std::min<std::size_t>(sumSpectra.sxx.size() - 1,
                                                       static_cast<std::size_t>(kBandEdgesHz[band + 1] / binWidth));
            double sumBand = 0.0;
            double leftBand = 0.0;
            double rightBand = 0.0;
            std::size_t binCount = 0;
            for (std::size_t bin = lowBin; bin < highBin; ++bin) {
                sumBand += sumSpectra.sxx[bin];
                leftBand += channelSpectra.sxx[bin];
                rightBand += channelSpectra.syy[bin];
                ++binCount;
            }
            if (binCount == 0 || !(leftBand > 0.0) || !(rightBand > 0.0)) continue;
            std::cout << "[" << kBandEdgesHz[band] << "-" << kBandEdgesHz[band + 1]
                      << "Hz] sum/L=" << 10.0 * std::log10(sumBand / leftBand)
                      << "dB sum/R=" << 10.0 * std::log10(sumBand / rightBand) << "dB ";
        }
        std::cout << '\n';

        // The conditional +3.01 dB figure, measured on the pre-Mix wet
        // channels (which are identical at every Mix, so this ratio is
        // Mix-independent by construction and is printed once per sweep point
        // only to show that it is). ADR-006 correction note C5: conditional on
        // all-lag uncorrelatedness, never an acceptance value.
        const std::vector<double> wetSum = sumOf(render.wetLeft, render.wetRight);
        const double eWetL = meanSquare(render.wetLeft);
        const double eWetR = meanSquare(render.wetRight);
        const double eWetSum = meanSquare(wetSum);
        const double meanChannelPower = 0.5 * (eWetL + eWetR);
        std::cout << std::setprecision(8) << "DS-8 Mix=" << mix << " pre-Mix wet sum (CONDITIONAL check, not "
                     "an acceptance criterion): E[y_L'^2]=" << eWetL << ", E[y_R'^2]=" << eWetR
                  << ", E[y_L' y_R']=" << meanProduct(render.wetLeft, render.wetRight)
                  << ", E[(y_L'+y_R')^2]=" << eWetSum << " -> measured sum gain="
                  << 10.0 * std::log10(eWetSum / meanChannelPower)
                  << "dB against the +3.01dB incoherent prediction\n";
    }
    std::cout << "DS-8: recorded only. The +3.01dB figure is conditional on all-lag uncorrelatedness "
                 "(ADR-006 correction note C5) and gates nothing here; no perceptual tolerance follows.\n";
    return 0;
}

// ---------------------------------------------------------------------------
// DS-9 -- channel-power measurements, arrivals and energy centroids.
//
// ADR-006 correction note C5: the 126-sample (48 kHz) / 122-sample (44.1 kHz)
// figure describes only the ISOLATED output diffuser chains and is not a
// whole-path onset, channel-power or centroid bound. Both are measured here
// and reported separately. A measured L/R difference is not gated or assigned
// a cause: this harness does not decompose the tapped-line variances and
// cross-covariances needed to support a causal explanation, and ADR-006 C5
// forbids a pre-review channel-balance/perceptual tolerance.
// ---------------------------------------------------------------------------

int testDs9ChannelBalanceAndCentroids() {
    for (const double rate : kBracketRates) {
        const DiffusionStereoConfig config = validConfig(rate);

        // (1) Channel balance on a continuous broadband-noise fixture.
        OrderedReferencePath noisePath;
        if (!noisePath.prepare(config)) return fail("DS-9 noise fixture preparation failed");
        const double decay = normalizedDecayForT60(noisePath.automation, kComparabilityT60Seconds);
        const double t60Zero = realizedT60Zero(noisePath.automation, decay);
        if (!applyReferenceControls(noisePath, decay, 0.0, 1.0)) {
            return fail("DS-9 noise fixture control application failed");
        }
        const auto warmup = static_cast<std::size_t>(kComparabilityT60Seconds * rate);
        const std::vector<float> noiseScript = makeNoiseScript(warmup + kAnalysisRecordSamples);
        const FullPathRender noiseRender = renderFullPath(noisePath, noiseScript, warmup);
        if (!hasNonzeroSample(noiseRender.left) || !hasNonzeroSample(noiseRender.right)) {
            return fail("DS-9 anti-vacuity: the channel-balance fixture rendered a silent channel");
        }
        const double noiseRmsLeft = rmsOf(noiseRender.left);
        const double noiseRmsRight = rmsOf(noiseRender.right);
        const double noiseImbalanceDb = 20.0 * std::log10(noiseRmsLeft / noiseRmsRight);

        // (2) Arrivals and full-path energy centroids on an impulse fixture.
        // The centroid of a decaying response depends on the window, so the
        // window is declared: 3*T60_0 of render, centroid taken over the whole
        // of it, indices relative to the impulse at sample 0.
        OrderedReferencePath impulsePath;
        if (!impulsePath.prepare(config)) return fail("DS-9 impulse fixture preparation failed");
        if (!applyReferenceControls(impulsePath, decay, 0.0, 1.0)) {
            return fail("DS-9 impulse fixture control application failed");
        }
        const auto impulseLength = static_cast<std::size_t>(rate * kComparabilityT60Seconds * 3.0);
        std::vector<float> impulseScript(impulseLength, 0.0F);
        impulseScript[0] = 1.0F;
        const FullPathRender impulseRender = renderFullPath(impulsePath, impulseScript, 0);
        if (!hasNonzeroSample(impulseRender.left) || !hasNonzeroSample(impulseRender.right)) {
            return fail("DS-9 anti-vacuity: the impulse fixture rendered a silent channel");
        }
        const std::size_t arrivalLeft = firstNonzeroIndex(impulseRender.left);
        const std::size_t arrivalRight = firstNonzeroIndex(impulseRender.right);
        const double centroidLeft = energyCentroidSamples(impulseRender.left);
        const double centroidRight = energyCentroidSamples(impulseRender.right);
        const double impulseRmsLeft = rmsOf(impulseRender.left);
        const double impulseRmsRight = rmsOf(impulseRender.right);
        const double impulseImbalanceDb = 20.0 * std::log10(impulseRmsLeft / impulseRmsRight);

        // (3) The ISOLATED output diffuser chains, fed a matched unit impulse
        // with no FDN and no input chain in front of them. This is exactly the
        // quantity ADR-006 (f) computed algebraically, so it is also a direct
        // cross-check of that arithmetic against a measurement.
        auto leftChain = buildCascade(rate, toTargets(config.leftOutputDelaySeconds), kGoldenCoefficient);
        auto rightChain = buildCascade(rate, toTargets(config.rightOutputDelaySeconds), kGoldenCoefficient);
        if (leftChain.empty() || rightChain.empty()) return fail("DS-9 isolated output chain preparation failed");
        std::size_t leftDelaySum = 0;
        std::size_t rightDelaySum = 0;
        for (const auto& section : leftChain) leftDelaySum += section.delaySamples();
        for (const auto& section : rightChain) rightDelaySum += section.delaySamples();
        const std::size_t isolatedLength = 128 * std::max(leftDelaySum, rightDelaySum);
        std::vector<double> isolatedLeft(isolatedLength, 0.0);
        std::vector<double> isolatedRight(isolatedLength, 0.0);
        for (std::size_t n = 0; n < isolatedLength; ++n) {
            const float input = n == 0 ? 1.0F : 0.0F;
            const auto leftSample = runCascadeSample(leftChain, input);
            const auto rightSample = runCascadeSample(rightChain, input);
            if (leftSample.nonFinite || rightSample.nonFinite) return fail("DS-9 isolated chain raised a fault");
            isolatedLeft[n] = static_cast<double>(leftSample.value);
            isolatedRight[n] = static_cast<double>(rightSample.value);
        }
        if (!hasNonzeroSample(isolatedLeft) || !hasNonzeroSample(isolatedRight)) {
            return fail("DS-9 anti-vacuity: an isolated output chain rendered silence");
        }
        const double isolatedCentroidLeft = energyCentroidSamples(isolatedLeft);
        const double isolatedCentroidRight = energyCentroidSamples(isolatedRight);
        const double isolatedDifference = isolatedCentroidRight - isolatedCentroidLeft;
        const auto expectedDifference =
            static_cast<double>(rightDelaySum) - static_cast<double>(leftDelaySum);

        std::cout << std::setprecision(10) << "DS-9 @ " << rate << "Hz (T60_0=" << t60Zero
                  << "s, Damp bypassed, Mix=1.0 full wet):\n"
                  << "  channel balance, " << kAnalysisRecordSamples
                  << "-sample noise record after a one-T60_0 warm-up: RMS_L=" << noiseRmsLeft
                  << ", RMS_R=" << noiseRmsRight << ", 20log10(L/R)=" << noiseImbalanceDb
                  << "dB (recorded only; no channel-balance tolerance)\n"
                  << "  channel balance, " << impulseLength << "-sample impulse response: RMS_L="
                  << impulseRmsLeft << ", RMS_R=" << impulseRmsRight
                  << ", 20log10(L/R)=" << impulseImbalanceDb << "dB\n"
                  << "  first nonzero arrival: L=" << arrivalLeft << " samples ("
                  << std::setprecision(6) << 1000.0 * static_cast<double>(arrivalLeft) / rate << "ms), R="
                  << arrivalRight << " samples (" << 1000.0 * static_cast<double>(arrivalRight) / rate
                  << "ms); shortest even-indexed line m_0=" << impulsePath.network.delaySamples(0)
                  << " samples, shortest odd-indexed line m_1=" << impulsePath.network.delaySamples(1)
                  << " samples -- the even/odd tap of ADR-006 (e), not the output diffusers, is what "
                     "separates the two first arrivals\n"
                  << std::setprecision(10) << "  full-path energy centroid over the declared "
                  << impulseLength << "-sample window: L=" << centroidLeft << " samples ("
                  << std::setprecision(6) << 1000.0 * centroidLeft / rate << "ms), R=" << std::setprecision(10)
                  << centroidRight << " samples (" << std::setprecision(6) << 1000.0 * centroidRight / rate
                  << "ms), R-L=" << std::setprecision(10) << centroidRight - centroidLeft << " samples ("
                  << std::setprecision(6) << 1000.0 * (centroidRight - centroidLeft) / rate << "ms)\n"
                  << std::setprecision(10) << "  isolated output-diffuser centroid (unit impulse, no FDN, "
                  << isolatedLength << "-sample drain): L=" << isolatedCentroidLeft << " samples, R="
                  << isolatedCentroidRight << " samples, R-L=" << isolatedDifference
                  << " samples (" << std::setprecision(6) << 1000.0 * isolatedDifference / rate
                  << "ms); ADR-006 (f) chain-length difference sum(d_R)-sum(d_L)=" << expectedDifference
                  << " samples\n";

        // Derivation for the 0.5-sample numerical cross-check below: a Schroeder allpass
        // section's energy centroid equals its own delay length exactly, so a
        // cascade's centroid equals the sum of its delays. For one section
        // with delay d and coefficient g, the impulse response is
        // h[0] = -g and h[k*d] = (1 - g^2) * g^(k-1) for k >= 1 (all other
        // samples are 0). Its energy is
        //   sum(h^2) = g^2 + (1-g^2)^2 * sum_{k>=1} g^(2k-2)
        //            = g^2 + (1-g^2)^2 * 1/(1-g^2) = g^2 + (1-g^2) = 1,
        // i.e. unit energy for any |g| < 1 -- confirming the allpass property
        // (an allpass section neither gains nor loses energy). Its
        // energy-weighted centroid (in samples) is
        //   sum(n * h[n]^2) = 0 * g^2 + sum_{k>=1} (k*d) * (1-g^2)^2 * g^(2k-2)
        //                   = d * (1-g^2)^2 * sum_{k>=1} k * g^(2k-2)
        //                   = d * (1-g^2)^2 * 1/(1-g^2)^2 = d,
        // using sum_{k>=1} k*x^(k-1) = 1/(1-x)^2 for x = g^2. So the centroid
        // of a unit-energy allpass section is exactly its delay d, independent
        // of g. This is the frequency-averaged group delay; those averages add
        // across a cascade, so the centroid is the sum of its delays. The
        // 0.5-sample numerical tolerance covers the finite drain and float
        // arithmetic residual, not a channel-power or perceptual criterion.
        if (std::abs(isolatedCentroidLeft - static_cast<double>(leftDelaySum)) > 0.5
            || std::abs(isolatedCentroidRight - static_cast<double>(rightDelaySum)) > 0.5) {
            return fail("DS-9 isolated output-diffuser centroid did not reproduce its chain's total delay");
        }
        if (std::abs(isolatedDifference - expectedDifference) > 0.5) {
            return fail("DS-9 isolated output-diffuser centroid difference did not match ADR-006 (f)");
        }
        std::cout << "  NOTE: any full-path centroid difference beyond the isolated-chain figure above is "
                     "recorded, not explained; the same holds for measured L/R RMS differences. ADR-006 "
                     "correction note C5 supplies no channel-balance acceptance value.\n";
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

    if (testAntiVacuityInfrastructure() != 0) return 1;
    if (testGeometricTargetsSingleElementDegeneracy() != 0) return 1;
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

    if (testWelchCoherenceEstimatorSelfCheck() != 0) return 1;
    if (testWelchPartialTrailingSegmentIsDropped() != 0) return 1;
    if (testDs4CascadeEnergyConservation() != 0) return 1;
    if (testDs4FullPathDecayLawRecorded() != 0) return 1;
    if (testDs7InterchannelCoherenceRecorded() != 0) return 1;
    if (testDs7CoherenceSegmentLengthSensitivity() != 0) return 1;
    if (testDs8MonoCompatibilityAcrossMixSweep() != 0) return 1;
    if (testDs9ChannelBalanceAndCentroids() != 0) return 1;
    std::cout << "DiffusionStereoPath Task 4b (DS-4,7,8,9) measurements passed\n";
    return 0;
}

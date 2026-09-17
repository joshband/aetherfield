#include "dsp/SchroederAllpass.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <utility>
#include <vector>

namespace {

std::atomic_size_t gAllocationCount = 0;

void* allocateOrThrow(std::size_t size) {
    ++gAllocationCount;
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::size_t alignment) {
    ++gAllocationCount;
    const std::size_t requested = size == 0 ? 1 : size;
    const std::size_t remainder = requested % alignment;
    if (requested > std::numeric_limits<std::size_t>::max() - (remainder == 0 ? 0 : alignment - remainder)) {
        throw std::bad_alloc();
    }
    const std::size_t rounded = requested + (remainder == 0 ? 0 : alignment - remainder);
    void* pointer = nullptr;
    if (posix_memalign(&pointer, alignment, rounded) == 0) return pointer;
    throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }

namespace {

using aetherfield::dsp::SchroederAllpass;

const double kGoldenCoefficient = (std::sqrt(5.0) - 1.0) / 2.0;
constexpr std::array<double, 2> kFixtureRates {48000.0, 44100.0};
constexpr std::array<double, 8> kDefaultDelaySeconds {
    0.001, 0.00215443, 0.00464159, 0.010,
    0.004, 0.00503968, 0.00634960, 0.008,
};
constexpr std::size_t kMinimumFftSize = 131072;

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool closeEnough(float actual, double expected, double tolerance = 3e-6) {
    return std::isfinite(actual) && std::abs(static_cast<double>(actual) - expected) <= tolerance;
}

float deterministicNoise(std::uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    const float unit = static_cast<float>(state >> 8U) / 16777215.0F;
    return 0.75F * (2.0F * unit - 1.0F);
}

std::vector<float> makeNoiseScript(std::size_t count) {
    std::vector<float> script(count);
    std::uint32_t state = 0xA37F4D29U;
    for (float& sample : script) sample = deterministicNoise(state);
    return script;
}

std::size_t nextPowerOfTwo(std::size_t value) {
    std::size_t result = 1;
    while (result < value) result <<= 1U;
    return result;
}

struct DoubleReferenceAllpass {
    explicit DoubleReferenceAllpass(std::size_t delaySamples, float coefficient)
        : delay(delaySamples, 0.0), g(static_cast<double>(coefficient)) {}

    double process(float input) {
        const double delayed = delay[writePosition];
        const double state = static_cast<double>(input) + g * delayed;
        const double output = delayed - g * state;
        delay[writePosition] = std::abs(state) >= 1e-20 ? state : 0.0;
        ++writePosition;
        if (writePosition == delay.size()) writePosition = 0;
        return output;
    }

    std::vector<double> delay;
    std::size_t writePosition = 0;
    double g;
};

double exactImpulse(std::size_t sample, std::size_t delay, double g) {
    if (sample == 0) return -g;
    if (sample % delay != 0) return 0.0;
    return (1.0 - g * g) * std::pow(g, static_cast<double>(sample / delay - 1));
}

int testImpulseAndPrimeDerivation() {
    SchroederAllpass section;
    if (!section.prepare(48000.0, 0.001, kGoldenCoefficient)) return fail("valid preparation rejected");
    if (section.delaySamples() != 47) return fail("nearest-prime derivation did not select 47");
    const double g = section.coefficient();
    for (std::size_t n = 0; n <= 4 * section.delaySamples(); ++n) {
        const auto sample = section.processSample(n == 0 ? 1.0F : 0.0F);
        if (sample.nonFinite || !closeEnough(sample.value, exactImpulse(n, section.delaySamples(), g))) {
            return fail("impulse response mismatch");
        }
    }
    return 0;
}

int testPureDelayAndReset() {
    SchroederAllpass section;
    if (!section.prepare(48000.0, 1.0 / 48000.0, 0.0) || section.delaySamples() != 2) {
        return fail("g=0 preparation or prime derivation failed");
    }
    if (section.processSample(1.0F).value != 0.0F || section.processSample(0.0F).value != 0.0F
        || section.processSample(0.0F).value != 1.0F) return fail("g=0 did not behave as a pure delay");
    section.reset();
    if (section.processSample(0.0F).value != 0.0F || section.processSample(0.0F).value != 0.0F) {
        return fail("reset did not restore silence");
    }
    return 0;
}

int testValidationAndPrepareRollback() {
    SchroederAllpass section;
    if (!section.prepare(48000.0, 0.001, 0.5)) return fail("baseline preparation failed");
    const auto oldDelay = section.delaySamples();
    const auto oldCoefficient = section.coefficient();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (section.prepare(nan, 0.001, 0.5) || section.prepare(48000.0, 0.001, -0.1)
        || section.prepare(48000.0, 0.001, 0.91) || section.prepare(48000.0, 0.0, 0.5)
        || section.prepare(48000.0, std::numeric_limits<double>::max(), 0.5)) return fail("invalid preparation accepted");
    if (section.delaySamples() != oldDelay || section.coefficient() != oldCoefficient) {
        return fail("failed preparation disturbed prior state");
    }
    if (!section.prepare(48000.0, 176.0 / 48000.0, 0.9) || section.delaySamples() != 173
        || section.coefficient() != 0.9F) return fail("nearest-prime lower tie at 176 was not selected");
    if (!section.prepare(48000.0, 205.0 / 48000.0, 0.9) || section.delaySamples() != 199) {
        return fail("nearest-prime lower tie at 205 was not selected");
    }
    if (!section.prepare(44100.0, 0.001, 0.9) || section.delaySamples() != 43) {
        return fail("44.1 kHz nearest-prime derivation failed");
    }
    return 0;
}

int testFaultReporting() {
    SchroederAllpass section;
    if (!section.prepare(48000.0, 0.001, 0.9)) return fail("fault-test preparation failed");
    const auto nan = section.processSample(std::numeric_limits<float>::quiet_NaN());
    if (!nan.nonFinite || !std::isnan(nan.value)) return fail("NaN was not reported and preserved");
    section.reset();
    const auto silence = section.processSample(0.0F);
    return (!silence.nonFinite && silence.value == 0.0F) ? 0 : fail("reset did not clear poisoned state");
}

int testIndependentDoubleRecurrence() {
    double maximumError = 0.0;
    for (const double rate : kFixtureRates) for (const double seconds : kDefaultDelaySeconds) {
        SchroederAllpass section;
        if (!section.prepare(rate, seconds, kGoldenCoefficient)) return fail("recurrence preparation failed");
        DoubleReferenceAllpass reference(section.delaySamples(), section.coefficient());
        for (std::size_t n = 0; n < 4096 + 2 * section.delaySamples(); ++n) {
            const float input = n == 0 ? 1.0F : 0.0F;
            const auto actual = section.processSample(input);
            const double expected = reference.process(input);
            if (actual.nonFinite) return fail("finite recurrence raised a fault");
            maximumError = std::max(maximumError, std::abs(static_cast<double>(actual.value) - expected));
        }
        section.reset();
        reference = DoubleReferenceAllpass(section.delaySamples(), section.coefficient());
        std::uint32_t state = 0xC001D00DU;
        for (std::size_t n = 0; n < 4096 + 2 * section.delaySamples(); ++n) {
            const float input = deterministicNoise(state);
            const auto actual = section.processSample(input);
            const double expected = reference.process(input);
            if (actual.nonFinite) return fail("finite recurrence raised a fault");
            maximumError = std::max(maximumError, std::abs(static_cast<double>(actual.value) - expected));
        }
    }
    std::cout << std::setprecision(10) << "Task 2 recurrence maximum absolute error: " << maximumError << '\n';
    return maximumError <= 2e-5 ? 0 : fail("double recurrence error exceeded 2e-5");
}

using Complex = std::complex<double>;

void radix2Fft(std::vector<Complex>& values) {
    const std::size_t count = values.size();
    for (std::size_t index = 1, reversed = 0; index < count; ++index) {
        std::size_t bit = count >> 1U;
        for (; (reversed & bit) != 0; bit >>= 1U) reversed ^= bit;
        reversed ^= bit;
        if (index < reversed) std::swap(values[index], values[reversed]);
    }
    for (std::size_t length = 2; length <= count; length <<= 1U) {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(length);
        const Complex step(std::cos(angle), std::sin(angle));
        for (std::size_t base = 0; base < count; base += length) {
            Complex twiddle(1.0, 0.0);
            for (std::size_t offset = 0; offset < length / 2; ++offset) {
                const Complex even = values[base + offset];
                const Complex odd = values[base + offset + length / 2] * twiddle;
                values[base + offset] = even + odd;
                values[base + offset + length / 2] = even - odd;
                twiddle *= step;
            }
        }
    }
}

int testFftFixture() {
    std::vector<Complex> impulse(kMinimumFftSize, Complex(0.0, 0.0));
    impulse[0] = Complex(1.0, 0.0);
    radix2Fft(impulse);
    double impulseError = 0.0;
    for (const Complex value : impulse) impulseError = std::max(impulseError, std::abs(value - Complex(1.0, 0.0)));
    constexpr std::size_t delay = 37;
    std::vector<Complex> delayed(kMinimumFftSize, Complex(0.0, 0.0));
    delayed[delay] = Complex(1.0, 0.0);
    radix2Fft(delayed);
    double phaseError = 0.0;
    for (std::size_t bin = 0; bin < delayed.size(); ++bin) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(bin * delay) / static_cast<double>(delayed.size());
        phaseError = std::max(phaseError, std::abs(delayed[bin] - Complex(std::cos(angle), std::sin(angle))));
    }
    std::cout << std::setprecision(10) << "Task 2 FFT fixture maximum errors: impulse=" << impulseError
              << ", delayed-phase=" << phaseError << '\n';
    return impulseError <= 1e-10 && phaseError <= 1e-10 ? 0 : fail("test-local FFT fixture did not reproduce impulse transforms");
}

int testMeasuredFftMagnitudeResponse() {
    double maximumMagnitudeError = 0.0;
    std::size_t configurations = 0;
    for (const double rate : kFixtureRates) for (const double seconds : kDefaultDelaySeconds) {
        SchroederAllpass section;
        if (!section.prepare(rate, seconds, kGoldenCoefficient)) return fail("FFT preparation failed");
        const std::size_t outputCount = 128 * section.delaySamples();
        const std::size_t fftSize = nextPowerOfTwo(std::max(kMinimumFftSize, outputCount));
        std::vector<Complex> response(fftSize, Complex(0.0, 0.0));
        for (std::size_t n = 0; n < outputCount; ++n) {
            const auto sample = section.processSample(n == 0 ? 1.0F : 0.0F);
            if (sample.nonFinite) return fail("impulse response raised a fault");
            response[n] = Complex(static_cast<double>(sample.value), 0.0);
        }
        radix2Fft(response);
        for (std::size_t bin = 0; bin <= fftSize / 2; ++bin) {
            maximumMagnitudeError = std::max(maximumMagnitudeError, std::abs(std::abs(response[bin]) - 1.0));
        }
        ++configurations;
    }
    std::cout << std::setprecision(10) << "Task 2 FFT magnitude max error across " << configurations
              << " configured sections: " << maximumMagnitudeError << '\n';
    return maximumMagnitudeError <= 1e-6 ? 0 : fail("measured FFT magnitude error exceeded 1e-6");
}

double renderRelativeEnergyError(SchroederAllpass& section, const std::vector<float>& input, std::size_t tail) {
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    for (std::size_t n = 0; n < input.size() + tail; ++n) {
        const float sample = n < input.size() ? input[n] : 0.0F;
        const auto output = section.processSample(sample);
        if (output.nonFinite) return std::numeric_limits<double>::infinity();
        inputEnergy += static_cast<double>(sample) * static_cast<double>(sample);
        outputEnergy += static_cast<double>(output.value) * static_cast<double>(output.value);
    }
    return std::abs(outputEnergy - inputEnergy) / inputEnergy;
}

int testFiniteFixtureEnergyConservation() {
    double impulseMaximum = 0.0;
    double noiseMaximum = 0.0;
    const std::vector<float> noise = makeNoiseScript(4096);
    const std::vector<float> impulse {1.0F};
    for (const double rate : kFixtureRates) for (const double seconds : kDefaultDelaySeconds) {
        SchroederAllpass impulseSection;
        SchroederAllpass noiseSection;
        if (!impulseSection.prepare(rate, seconds, kGoldenCoefficient)
            || !noiseSection.prepare(rate, seconds, kGoldenCoefficient)) return fail("energy preparation failed");
        const std::size_t tail = 128 * impulseSection.delaySamples();
        impulseMaximum = std::max(impulseMaximum, renderRelativeEnergyError(impulseSection, impulse, tail));
        noiseMaximum = std::max(noiseMaximum, renderRelativeEnergyError(noiseSection, noise, tail));
    }
    std::cout << std::setprecision(10) << "Task 2 energy relative errors: impulse=" << impulseMaximum
              << ", noise=" << noiseMaximum << '\n';
    return impulseMaximum <= 1e-5 && noiseMaximum <= 1e-5 ? 0 : fail("finite fixture energy error exceeded 1e-5");
}

int testAdversarialSectionPeakBound() {
    double measuredPeak = 0.0;
    double largestBound = 0.0;
    for (const double rate : kFixtureRates) for (const double seconds : kDefaultDelaySeconds) {
        SchroederAllpass section;
        if (!section.prepare(rate, seconds, kGoldenCoefficient)) return fail("peak preparation failed");
        const std::size_t delay = section.delaySamples();
        const std::size_t maximumIndex = 64 * delay;
        const double g = static_cast<double>(section.coefficient());
        const double bound = 1.0 + 2.0 * std::abs(g);
        double casePeak = 0.0;
        for (std::size_t n = 0; n <= maximumIndex + 2 * delay; ++n) {
            const double reversed = n <= maximumIndex ? exactImpulse(maximumIndex - n, delay, g) : 0.0;
            const float input = reversed > 0.0 ? 1.0F : (reversed < 0.0 ? -1.0F : 0.0F);
            const auto output = section.processSample(input);
            if (output.nonFinite) return fail("adversarial finite input raised a fault");
            casePeak = std::max(casePeak, std::abs(static_cast<double>(output.value)));
        }
        measuredPeak = std::max(measuredPeak, casePeak);
        largestBound = std::max(largestBound, bound);
        if (casePeak > bound + 1e-5) return fail("single-section adversarial peak exceeded l1 bound");
    }
    std::cout << std::setprecision(10) << "Task 2 largest section peak=" << measuredPeak
              << ", largest checked bound=" << largestBound << '\n';
    return 0;
}

int testCutoffSilenceFixture() {
    for (const double rate : kFixtureRates) for (const double seconds : kDefaultDelaySeconds) {
        for (const double requestedG : {kGoldenCoefficient, 0.9}) {
            SchroederAllpass section;
            if (!section.prepare(rate, seconds, requestedG)) return fail("cutoff preparation failed");
            const std::size_t delay = section.delaySamples();
            const double g = static_cast<double>(section.coefficient());
            const std::size_t silenceStart = static_cast<std::size_t>(std::ceil(std::log(1e-20) / std::log(g)) + 3.0) * delay;
            for (std::size_t n = 0; n < silenceStart + 2 * delay; ++n) {
                const auto output = section.processSample(n == 0 ? 1.0F : 0.0F);
                if (output.nonFinite) return fail("cutoff fixture raised a fault");
                if (n >= silenceStart && output.value != 0.0F) return fail("cutoff fixture was not exactly silent at conservative bound");
            }
        }
        SchroederAllpass pureDelay;
        if (!pureDelay.prepare(rate, seconds, 0.0)) return fail("g=0 cutoff preparation failed");
        const std::size_t delay = pureDelay.delaySamples();
        for (std::size_t n = 0; n <= 3 * delay; ++n) {
            const auto output = pureDelay.processSample(n == 0 ? 1.0F : 0.0F);
            if (output.nonFinite || (n >= delay + 1 && output.value != 0.0F)) return fail("g=0 impulse did not end after d+1 samples");
        }
    }
    std::cout << "Task 2 cutoff fixture: all configured lengths reached exact silence at the conservative bound\n";
    return 0;
}

int testFaultAndResetBehavior() {
    SchroederAllpass overflow;
    if (!overflow.prepare(48000.0, 0.001, 0.9)) return fail("overflow preparation failed");
    bool sawOverflow = false;
    for (std::size_t n = 0; n < 2 * overflow.delaySamples() + 1; ++n) {
        sawOverflow = overflow.processSample(std::numeric_limits<float>::max()).nonFinite || sawOverflow;
    }
    if (!sawOverflow) return fail("finite FLT_MAX fixture did not report overflow");
    overflow.reset();
    for (std::size_t n = 0; n < 2 * overflow.delaySamples(); ++n) {
        const auto silence = overflow.processSample(0.0F);
        if (silence.nonFinite || silence.value != 0.0F) return fail("overflow reset did not restore exact silent no-fault behavior");
    }
    for (const float exceptional : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        SchroederAllpass section;
        if (!section.prepare(48000.0, 0.001, 0.9)) return fail("exceptional-input preparation failed");
        if (!section.processSample(exceptional).nonFinite) return fail("exceptional input did not report a fault");
        section.reset();
        for (std::size_t n = 0; n < 2 * section.delaySamples(); ++n) {
            const auto silence = section.processSample(0.0F);
            if (silence.nonFinite || silence.value != 0.0F) return fail("reset did not restore exact silent no-fault behavior");
        }
    }
    std::cout << "Task 2 fault fixture: finite overflow, NaN, and infinity each reported; reset restored silence\n";
    return 0;
}

int testNoAllocationDuringProcessAndReset() {
    SchroederAllpass section;
    if (!section.prepare(48000.0, 0.010, kGoldenCoefficient)) return fail("allocation fixture preparation failed");
    const std::vector<float> script = makeNoiseScript(8192);
    std::vector<float> output(script.size());
    section.reset();
    const std::size_t before = gAllocationCount.load();
    for (std::size_t n = 0; n < script.size(); ++n) output[n] = section.processSample(script[n]).value;
    section.reset();
    const std::size_t after = gAllocationCount.load();
    if (after != before) return fail("allocation occurred during processSample/reset fixture");
    std::cout << "Task 2 allocation delta through process/reset: " << (after - before) << '\n';
    return 0;
}

std::vector<std::uint32_t> renderPartitionedScript(std::size_t fixedPartition, bool ragged) {
    SchroederAllpass section;
    if (!section.prepare(48000.0, 0.010, kGoldenCoefficient)) return {};
    const std::vector<float> script = makeNoiseScript(8192);
    std::vector<std::uint32_t> output(script.size());
    constexpr std::array<std::size_t, 5> kRagged {7, 29, 3, 211, 5};
    std::size_t offset = 0;
    std::size_t raggedIndex = 0;
    while (offset < script.size()) {
        const std::size_t requested = ragged ? kRagged[raggedIndex++ % kRagged.size()] : fixedPartition;
        const std::size_t count = std::min(requested, script.size() - offset);
        for (std::size_t n = 0; n < count; ++n) {
            const auto sample = section.processSample(script[offset + n]);
            if (sample.nonFinite) return {};
            output[offset + n] = std::bit_cast<std::uint32_t>(sample.value);
        }
        offset += count;
    }
    return output;
}

int testBitIdentityAcrossRepeatedScriptsAndPartitions() {
    const std::vector<std::uint32_t> baseline = renderPartitionedScript(1, false);
    if (baseline.empty()) return fail("baseline deterministic script failed");
    if (renderPartitionedScript(1, false) != baseline) return fail("repeated identical script changed float bits");
    for (const std::size_t partition : {std::size_t(13), std::size_t(64), std::size_t(512)}) {
        if (renderPartitionedScript(partition, false) != baseline) return fail("fixed partition changed float bits");
    }
    if (renderPartitionedScript(0, true) != baseline) return fail("ragged partition changed float bits");
    std::cout << "Task 2 deterministic partitions: {1,13,64,512,ragged} are float-bit identical\n";
    return 0;
}

int testIndependentResponseAndNumericalEvidence() {
    if (testIndependentDoubleRecurrence() != 0) return 1;
    if (testFftFixture() != 0) return 1;
    if (testMeasuredFftMagnitudeResponse() != 0) return 1;
    if (testFiniteFixtureEnergyConservation() != 0) return 1;
    if (testAdversarialSectionPeakBound() != 0) return 1;
    if (testCutoffSilenceFixture() != 0) return 1;
    if (testFaultAndResetBehavior() != 0) return 1;
    if (testNoAllocationDuringProcessAndReset() != 0) return 1;
    return testBitIdentityAcrossRepeatedScriptsAndPartitions();
}

} // namespace

int main() {
    if (testImpulseAndPrimeDerivation() != 0) return 1;
    if (testPureDelayAndReset() != 0) return 1;
    if (testValidationAndPrepareRollback() != 0) return 1;
    if (testFaultReporting() != 0) return 1;
    if (testIndependentResponseAndNumericalEvidence() != 0) return 1;
    std::cout << "SchroederAllpass tests passed\n";
    return 0;
}

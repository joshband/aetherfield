// Offline render of a single impulse response through the S2/PT fixture
// (FeedbackDelayNetwork automated by ParameterAutomation), for a first
// observational listen. Per ADR-002's S2 exit condition and testing.md's
// "Sonic acceptance" gate: this is an OBSERVATION, not an ACCEPTANCE. No
// diffusion, stereo, or input/output tap design exists yet; the network's
// injection/output-tap convention here is the same test-only convention
// docs/phase1-s2-plan.md defines, not a product signal path. Parameter
// values below are stated, deterministic, and explicitly NOT tuned
// defaults or product recommendations — see the printed summary.

#include "dsp/FeedbackDelayNetwork.h"
#include "dsp/ParameterAutomation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void writeU16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void writeU32(std::ofstream& output, std::uint32_t value) {
    writeU16(output, static_cast<std::uint16_t>(value & 0xffffU));
    writeU16(output, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 5) {
        std::cerr << "Usage: aetherfield_render_reverb OUTPUT.wav [decay=0.6] [damp=0.3] [mix=1.0]\n"
                   << "  decay/damp/mix are normalized [0,1] controls (ADR-004); defaults produce\n"
                   << "  the original baseline render (T60_0~=3.06s, moderate damping, full wet).\n";
        return 2;
    }

    // Stated, deterministic fixture and parameter choices. None of these
    // is a product default or a tuned recommendation; N/tMin/tMax/dMaxFixture
    // are the ADR-005/phase1-pt-plan.md fixture values already exercised by
    // the test suites, and the normalized control values below (overridable
    // via argv for A/B listening) were picked only to produce an audible,
    // moderate-length tail for this first listen.
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t lineCount = 8;
    constexpr double tMinSeconds = 0.027;
    constexpr double tMaxSeconds = 0.081;
    constexpr double dMaxFixture = 48.0;
    const double decayNormalized = (argc > 2) ? std::stod(argv[2]) : 0.6;
    const double dampNormalized = (argc > 3) ? std::stod(argv[3]) : 0.3;
    const double mixNormalized = (argc > 4) ? std::stod(argv[4]) : 1.0;

    aetherfield::dsp::FeedbackDelayNetwork network;
    // Initial T60 values here are placeholders: ParameterAutomation
    // overwrites every line's gain/damping coefficient before any signal
    // is processed, via setLineGain()/setDampingCoefficientC().
    if (!network.prepare(sampleRate, lineCount, tMinSeconds, tMaxSeconds, 1.0, 1.0)) {
        std::cerr << "network.prepare() failed.\n";
        return 1;
    }
    aetherfield::dsp::ParameterAutomation automation;
    if (!automation.prepare(network, sampleRate, dMaxFixture)) {
        std::cerr << "automation.prepare() failed.\n";
        return 1;
    }
    if (!automation.setDecay(decayNormalized) || !automation.setDamp(dampNormalized)
        || !automation.setMix(mixNormalized)) {
        std::cerr << "automation.set*() failed.\n";
        return 1;
    }

    // Let the 20ms coefficient ramp settle before the impulse, so the
    // impulse response reflects the fully-realized target configuration,
    // not a partially-ramped one.
    constexpr std::size_t rampLengthSamples = 960; // round(0.020 * 48000), ADR-004 (c)
    for (std::size_t n = 0; n < rampLengthSamples; ++n) {
        automation.checkForNewTargets();
        const auto mix = automation.advance(network);
        const float wet = network.processSample(0.0F);
        (void)mix;
        (void)wet;
    }

    const double t60Zero = automation.t60Min() * std::pow(automation.t60Max() / automation.t60Min(), decayNormalized);
    const std::size_t durationSamples =
        static_cast<std::size_t>(sampleRate * std::min(std::max(3.0 * t60Zero, 2.0), 10.0));

    std::vector<float> output(durationSamples, 0.0F);
    aetherfield::dsp::ParameterAutomation::MixGains finalMix{0.0F, 0.0F};
    for (std::size_t n = 0; n < durationSamples; ++n) {
        automation.checkForNewTargets();
        const auto mix = automation.advance(network);
        const float input = (n == 0) ? 1.0F : 0.0F; // full-scale impulse at n=0, silence thereafter
        const float wet = network.processSample(input);
        output[n] = mix.dry * input + mix.wet * wet;
        finalMix = mix; // settled after the priming ramp above; unchanged for the rest of this render
    }

    if (network.nonFiniteCount() != 0) {
        std::cerr << "Non-finite value produced during render (count="
                   << network.nonFiniteCount() << "). Aborting: never render a poisoned network.\n";
        return 1;
    }

    float peak = 0.0F;
    double sumSquares = 0.0;
    for (float sample : output) {
        peak = std::max(peak, std::fabs(sample));
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double rms = std::sqrt(sumSquares / static_cast<double>(output.size()));

    // Peak-normalize for a comfortable listening level, reporting the
    // exact original peak/RMS and the applied gain -- never silently
    // clipped or hidden, per testing.md's "never hidden by clipping" rule.
    float appliedGain = 1.0F;
    if (peak > 0.0F) {
        appliedGain = 0.891F / peak; // target ~ -1 dBFS peak after normalization
        for (float& sample : output) {
            sample *= appliedGain;
        }
    }

    std::ofstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Unable to open output WAV.\n";
        return 1;
    }

    constexpr std::uint32_t sampleRateU32 = 48000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(output.size() * sizeof(std::int16_t));
    file.write("RIFF", 4);
    writeU32(file, 36U + dataBytes);
    file.write("WAVEfmt ", 8);
    writeU32(file, 16);
    writeU16(file, 1);
    writeU16(file, channels);
    writeU32(file, sampleRateU32);
    writeU32(file, sampleRateU32 * channels * (bitsPerSample / 8U));
    writeU16(file, channels * (bitsPerSample / 8U));
    writeU16(file, bitsPerSample);
    file.write("data", 4);
    writeU32(file, dataBytes);
    for (float sample : output) {
        const float clamped = std::min(1.0F, std::max(-1.0F, sample));
        const std::int16_t quantized = static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
        writeU16(file, static_cast<std::uint16_t>(quantized));
    }

    file.close();
    if (!file) {
        std::cerr << "Unable to write output WAV.\n";
        return 1;
    }

    std::cerr << "Wrote " << argv[1] << ": " << output.size() << " samples ("
               << (static_cast<double>(output.size()) / sampleRate) << "s) at " << sampleRateU32 << " Hz mono.\n"
               << "Fixture: N=" << lineCount << ", T60_0=" << t60Zero << "s (Decay=" << decayNormalized
               << " normalized), Damp=" << dampNormalized << " normalized (D_max_fixture=" << dMaxFixture
               << "dB, test-only), Mix=" << mixNormalized << " normalized (as-applied dry=" << finalMix.dry
               << ", wet=" << finalMix.wet << ", equal-power).\n"
               << "Pre-normalization: peak=" << peak << ", RMS=" << rms << ". Applied gain="
               << appliedGain << " for listening level.\n"
               << "This is a bare impulse response of the fixed late network plus its parameter "
               << "automation, with no diffusion, stereo, or product input/output tap design. "
               << "Per ADR-002's S2 exit condition: this is an OBSERVATION, not an acceptance.\n";
    return 0;
}

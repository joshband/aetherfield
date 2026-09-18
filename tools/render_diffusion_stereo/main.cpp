// Offline stereo render of a single mono impulse through DiffusionStereoPath
// (input allpass diffusion -> FDN -> even/odd taps -> output allpass
// diffusion/decorrelation -> Mix), for a first observational listen through
// the DS-B evaluation-baseline wet path measured by DS-1..12
// (tests/DiffusionStereoPathTests.cpp, docs/testing.md). Per ADR-006 and the
// roadmap's "Sonic acceptance" gate: this is an OBSERVATION, not an
// ACCEPTANCE. ADR-006 remains an accepted evaluation baseline, not a claim
// that the product's final line count, sample-rate matrix, D_max, or tap/mix
// design are settled.
//
// Decay/Damp/Mix are adjustable via setDecay()/setDamp()/setMix() (owner-
// authorized directly in conversation 2026-09-18, not via a written ADR/
// plan; see docs/testing.md and docs/agent-log.md), following render_reverb's
// own CLI convention (normalized [0,1], ADR-004). The fixture below (line
// count, delay windows, ADR-006 delay-in-seconds targets, the golden-ratio
// allpass coefficient) is the identical evaluation baseline validConfig() in
// tests/DiffusionStereoPathTests.cpp already exercises, not a tuned product
// preset.

#include "dsp/DiffusionStereoPath.h"
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

// Same evaluation-fixture delay/coefficient values as validConfig() in
// tests/DiffusionStereoPathTests.cpp, parameterized only by sample rate.
aetherfield::dsp::DiffusionStereoConfig evaluationFixtureConfig(double sampleRate) {
    return {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = 0.027,
        .fdnMaxDelaySeconds = 0.081,
        .t60ZeroSeconds = 4.0, // placeholder: ParameterAutomation overwrites every
        .t60PiSeconds = 4.0,   // line's gain/damping coefficient before any signal is processed.
        .dMaxDb = 48.0,
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = 0.6180339887498948482, // (sqrt(5)-1)/2, ADR-006 (b)
    };
}

// Independently prepares a same-shape bare FeedbackDelayNetwork + its own
// ParameterAutomation, purely to read the realized T60_0 at the given Decay
// for the printed summary -- the identical technique
// testDs10ProofTemplateAndMeasuredSilence uses, and the same closed form
// render_reverb.cpp already applies to its own bare-network fixture. This
// never touches the actual render path; it is diagnostic only.
bool realizedT60(double sampleRate, double decayNormalized, double& t60Zero) {
    aetherfield::dsp::FeedbackDelayNetwork network;
    if (!network.prepare(sampleRate, 8, 0.027, 0.081, 4.0, 4.0)) return false;
    aetherfield::dsp::ParameterAutomation automation;
    if (!automation.prepare(network, sampleRate, 48.0)) return false;
    t60Zero = automation.t60Min() * std::pow(automation.t60Max() / automation.t60Min(), decayNormalized);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 7) {
        std::cerr << "Usage: aetherfield_render_diffusion_stereo OUTPUT.wav [rate=48000] "
                     "[durationSeconds=8.0] [decay=0.5] [damp=0.0] [mix=1.0]\n"
                     "  rate: sample rate in Hz (fixture is exercised at 48000/44100 by the test suite;\n"
                     "        other rates are accepted but not independently verified).\n"
                     "  durationSeconds: render length; the impulse response is NOT truncated to a\n"
                     "  measured silence point, so pick a value comfortably longer than the tail you\n"
                     "  want to hear.\n"
                     "  decay/damp/mix: normalized [0,1] controls (ADR-004), forwarded to the\n"
                     "  wrapper's owned ParameterAutomation; defaults match its own built-in defaults.\n";
        return 2;
    }

    const double sampleRate = (argc > 2) ? std::stod(argv[2]) : 48000.0;
    const double durationSeconds = (argc > 3) ? std::stod(argv[3]) : 8.0;
    const double decayNormalized = (argc > 4) ? std::stod(argv[4]) : 0.5;
    const double dampNormalized = (argc > 5) ? std::stod(argv[5]) : 0.0;
    const double mixNormalized = (argc > 6) ? std::stod(argv[6]) : 1.0;
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
        std::cerr << "rate must be finite and > 0.\n";
        return 2;
    }
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0 || durationSeconds > 120.0) {
        std::cerr << "durationSeconds must be finite, > 0, and <= 120.\n";
        return 2;
    }

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(evaluationFixtureConfig(sampleRate))) {
        std::cerr << "path.prepare() failed.\n";
        return 1;
    }
    if (!path.setDecay(decayNormalized) || !path.setDamp(dampNormalized) || !path.setMix(mixNormalized)) {
        std::cerr << "path.set{Decay,Damp,Mix}() rejected an argument (must be finite).\n";
        return 2;
    }

    // Let the 20ms coefficient ramp settle (ADR-004 (c)) before the impulse,
    // so the impulse response reflects the fully-realized target
    // configuration rather than a partially-ramped one -- matching
    // render_reverb.cpp's own priming step. Unconditional: prepare()'s own
    // built-in defaults are already settled (prepare() ends with reset(),
    // which snaps every ramp to its target), so this is a no-op cost when
    // decay/damp/mix match those defaults, and correctness-required
    // otherwise.
    constexpr std::size_t rampLengthSamples = 960; // round(0.020 * 48000), ADR-004 (c)
    const std::size_t primingSamples = static_cast<std::size_t>(
        std::lround(static_cast<double>(rampLengthSamples) * sampleRate / 48000.0));
    std::vector<float> silence(primingSamples, 0.0F);
    std::vector<float> primeLeft(primingSamples);
    std::vector<float> primeRight(primingSamples);
    path.process(silence.data(), primeLeft.data(), primeRight.data(), primingSamples);

    const std::size_t durationSamples = static_cast<std::size_t>(sampleRate * durationSeconds);
    std::vector<float> input(durationSamples, 0.0F);
    input[0] = 1.0F; // full-scale impulse at n=0, silence thereafter
    std::vector<float> left(durationSamples);
    std::vector<float> right(durationSamples);
    path.process(input.data(), left.data(), right.data(), durationSamples);

    if (path.nonFiniteCount() != 0) {
        std::cerr << "Non-finite value produced during render (count=" << path.nonFiniteCount()
                   << "). Aborting: never render a poisoned path.\n";
        return 1;
    }

    // Peak-normalize both channels together, by a single shared gain, so
    // the render's stereo balance (including DS-9's recorded L/R asymmetry)
    // is preserved rather than hidden by independent per-channel gains.
    float peak = 0.0F;
    double sumSquaresL = 0.0;
    double sumSquaresR = 0.0;
    for (std::size_t n = 0; n < durationSamples; ++n) {
        peak = std::max({peak, std::fabs(left[n]), std::fabs(right[n])});
        sumSquaresL += static_cast<double>(left[n]) * static_cast<double>(left[n]);
        sumSquaresR += static_cast<double>(right[n]) * static_cast<double>(right[n]);
    }
    const double rmsL = std::sqrt(sumSquaresL / static_cast<double>(durationSamples));
    const double rmsR = std::sqrt(sumSquaresR / static_cast<double>(durationSamples));

    float appliedGain = 1.0F;
    if (peak > 0.0F) {
        appliedGain = 0.891F / peak; // target ~ -1 dBFS peak after normalization
        for (std::size_t n = 0; n < durationSamples; ++n) {
            left[n] *= appliedGain;
            right[n] *= appliedGain;
        }
    }

    std::ofstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Unable to open output WAV.\n";
        return 1;
    }

    const std::uint32_t sampleRateU32 = static_cast<std::uint32_t>(std::lround(sampleRate));
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bitsPerSample = 16;
    const std::uint32_t dataBytes
        = static_cast<std::uint32_t>(durationSamples * channels * (bitsPerSample / 8U));
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
    for (std::size_t n = 0; n < durationSamples; ++n) {
        const float clampedL = std::min(1.0F, std::max(-1.0F, left[n]));
        const float clampedR = std::min(1.0F, std::max(-1.0F, right[n]));
        writeU16(file, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clampedL * 32767.0F))));
        writeU16(file, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clampedR * 32767.0F))));
    }

    file.close();
    if (!file) {
        std::cerr << "Unable to write output WAV.\n";
        return 1;
    }

    double t60Zero = 0.0;
    const bool haveT60 = realizedT60(sampleRate, decayNormalized, t60Zero);

    std::cerr << "Wrote " << argv[1] << ": " << durationSamples << " samples ("
              << durationSeconds << "s) at " << sampleRateU32 << " Hz stereo.\n"
              << "Fixture: N=8, input K_in=4 / output K_out=2 each channel, g_ap=0.6180339887 "
              << "(ADR-006 (b)), Decay=" << decayNormalized << " normalized, Damp=" << dampNormalized
              << " normalized (D_max_fixture=48dB, test-only), Mix=" << mixNormalized << " normalized";
    if (haveT60) {
        std::cerr << ", realized T60_0=" << t60Zero << "s";
    }
    std::cerr << ".\n"
              << "Pre-normalization: peak=" << peak << ", RMS_L=" << rmsL << ", RMS_R=" << rmsR
              << ". Applied gain=" << appliedGain << " for listening level (single shared gain, "
              << "so relative L/R level is unaltered).\n"
              << "This is DS-B Task 4's measured evaluation-baseline wet path (ADR-006), not a "
              << "finished product signal path or a claim of sonic acceptance -- see roadmap.md's "
              << "Sonic acceptance gate and docs/testing.md's DS-9 recorded L/R balance/arrival "
              << "findings.\n";
    return 0;
}

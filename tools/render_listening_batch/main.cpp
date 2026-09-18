// Round-2 owner-listening batch for the DS-B diffusion/stereo wet path
// (DiffusionStereoPath). Generates, in one deterministic pass:
//   - Decay isolation (short/baseline/long), impulse source.
//   - Damp isolation (off/full), impulse source, more extreme than round 1's
//     Damp=0.3 (which round 1 found only subtly distinguishable) per round
//     1's own deferred suggestion.
//   - Mix isolation (dry/half/wet), using a SUSTAINED tone rather than a
//     single-sample impulse, normalized to ONE SHARED peak across the three
//     files in this group rather than per-file -- fixing round 1's recorded
//     design flaw (a single-sample dry impulse is inaudible, and per-file
//     normalization erased the one real difference Mix produced).
//   - A synthetic, deterministic "musical corpus" (pluck chord, sustained
//     pad, percussive transient sequence) rendered at the wrapper's default
//     settings, per the Sonic acceptance gate's "impulse and repeatable
//     musical corpus" requirement (roadmap.md, docs/testing.md).
//   - One stereo-vs-mono-fold-down comparison on the pad corpus item, since
//     DS-8/DS-9 measured (not judged) a mono-sum consequence and an L/R
//     asymmetry that only listening can actually assess.
//
// Every source signal here is synthesized deterministically (fixed
// frequencies/envelopes, or a fixed-seed simple PRNG for the transient
// bursts) -- repeatable, no external audio files, matching this repo's
// existing renderer conventions. This is an OBSERVATION tool per ADR-006
// and the roadmap's Sonic acceptance gate, not an acceptance claim.

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

using aetherfield::dsp::DiffusionStereoConfig;
using aetherfield::dsp::DiffusionStereoPath;

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kRampLengthSamples = 960; // 20ms @48kHz, ADR-004 (c)

void writeU16(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void writeU32(std::ofstream& output, std::uint32_t value) {
    writeU16(output, static_cast<std::uint16_t>(value & 0xffffU));
    writeU16(output, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

bool writeStereoWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    const std::uint32_t sampleRateU32 = static_cast<std::uint32_t>(std::lround(kSampleRate));
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bitsPerSample = 16;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(left.size() * channels * (bitsPerSample / 8U));
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
    for (std::size_t n = 0; n < left.size(); ++n) {
        const float clampedL = std::min(1.0F, std::max(-1.0F, left[n]));
        const float clampedR = std::min(1.0F, std::max(-1.0F, right[n]));
        writeU16(file, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clampedL * 32767.0F))));
        writeU16(file, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clampedR * 32767.0F))));
    }
    file.close();
    return static_cast<bool>(file);
}

bool writeMonoWav(const std::string& path, const std::vector<float>& mono) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    const std::uint32_t sampleRateU32 = static_cast<std::uint32_t>(std::lround(kSampleRate));
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(mono.size() * channels * (bitsPerSample / 8U));
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
    for (float sample : mono) {
        const float clamped = std::min(1.0F, std::max(-1.0F, sample));
        writeU16(file, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clamped * 32767.0F))));
    }
    file.close();
    return static_cast<bool>(file);
}

DiffusionStereoConfig evaluationFixtureConfig(double sampleRate) {
    return {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = 0.027,
        .fdnMaxDelaySeconds = 0.081,
        .t60ZeroSeconds = 4.0,
        .t60PiSeconds = 4.0,
        .dMaxDb = 48.0,
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = 0.6180339887498948482,
    };
}

// Same technique as tools/render_diffusion_stereo/main.cpp's realizedT60():
// an independent bare-network fixture, read-only, used purely to size each
// render's duration to its own realized T60_0.
bool realizedT60(double decayNormalized, double& t60Zero) {
    aetherfield::dsp::FeedbackDelayNetwork network;
    if (!network.prepare(kSampleRate, 8, 0.027, 0.081, 4.0, 4.0)) return false;
    aetherfield::dsp::ParameterAutomation automation;
    if (!automation.prepare(network, kSampleRate, 48.0)) return false;
    t60Zero = automation.t60Min() * std::pow(automation.t60Max() / automation.t60Min(), decayNormalized);
    return true;
}

std::size_t tailSamplesFor(double decayNormalized) {
    double t60Zero = 1.0;
    realizedT60(decayNormalized, t60Zero);
    // Capped at 90s, not a flat 20s: T60_0 spans roughly 0.018s (Decay=0.1)
    // to 66.7s (Decay=0.9) at this fixture (measured directly, not assumed
    // -- see docs/agent-log.md's round-2 listening entry), so a flat cap
    // well below the top of that range silently truncates a slow decay
    // mid-fade rather than letting it actually die away. 90s covers
    // Decay=0.9 to roughly 1.3 T60 periods (~-81dB from peak) without
    // making every other render unreasonably long, since 4*T60_0 is well
    // under 90s for every Decay setting below roughly 0.93.
    const double seconds = std::min(std::max(4.0 * t60Zero, 3.0), 90.0);
    return static_cast<std::size_t>(kSampleRate * seconds);
}

// ---- Deterministic source generators ----

std::vector<float> impulseSource(std::size_t totalSamples) {
    std::vector<float> source(totalSamples, 0.0F);
    source[0] = 1.0F;
    return source;
}

std::vector<float> sustainedTone(double freqHz, double seconds, std::size_t tailSamples) {
    const std::size_t toneSamples = static_cast<std::size_t>(kSampleRate * seconds);
    std::vector<float> source(toneSamples + tailSamples, 0.0F);
    constexpr double fadeSeconds = 0.05;
    const std::size_t fadeSamples = static_cast<std::size_t>(kSampleRate * fadeSeconds);
    for (std::size_t n = 0; n < toneSamples; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        double envelope = 1.0;
        if (n < fadeSamples) {
            envelope = static_cast<double>(n) / static_cast<double>(fadeSamples);
        } else if (n >= toneSamples - fadeSamples) {
            envelope = static_cast<double>(toneSamples - n) / static_cast<double>(fadeSamples);
        }
        source[n] = static_cast<float>(0.5 * envelope * std::sin(2.0 * M_PI * freqHz * t));
    }
    return source;
}

// A short plucked/mallet-like chord: three partials, fast attack, exponential
// decay -- deterministic, no external samples.
std::vector<float> pluckChord(std::size_t tailSamples) {
    constexpr double sourceSeconds = 1.5;
    constexpr std::array<double, 3> freqsHz {261.63, 329.63, 392.00}; // C major triad
    constexpr double attackSeconds = 0.004;
    constexpr double decayTau = 0.55;
    const std::size_t sourceSamples = static_cast<std::size_t>(kSampleRate * sourceSeconds);
    const std::size_t attackSamples = static_cast<std::size_t>(kSampleRate * attackSeconds);
    std::vector<float> source(sourceSamples + tailSamples, 0.0F);
    for (std::size_t n = 0; n < sourceSamples; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        const double attack = (n < attackSamples) ? static_cast<double>(n) / static_cast<double>(attackSamples) : 1.0;
        const double decay = std::exp(-t / decayTau);
        double sample = 0.0;
        for (double freq : freqsHz) sample += std::sin(2.0 * M_PI * freq * t);
        source[n] = static_cast<float>((sample / static_cast<double>(freqsHz.size())) * attack * decay);
    }
    return source;
}

// A slow-swelling sustained pad chord over several seconds.
std::vector<float> sustainedPad(std::size_t tailSamples) {
    constexpr double sourceSeconds = 4.0;
    constexpr std::array<double, 3> freqsHz {220.00, 261.63, 329.63}; // A minor triad
    constexpr double fadeSeconds = 0.6;
    const std::size_t sourceSamples = static_cast<std::size_t>(kSampleRate * sourceSeconds);
    const std::size_t fadeSamples = static_cast<std::size_t>(kSampleRate * fadeSeconds);
    std::vector<float> source(sourceSamples + tailSamples, 0.0F);
    for (std::size_t n = 0; n < sourceSamples; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        double envelope = 1.0;
        if (n < fadeSamples) {
            envelope = static_cast<double>(n) / static_cast<double>(fadeSamples);
        } else if (n >= sourceSamples - fadeSamples) {
            envelope = static_cast<double>(sourceSamples - n) / static_cast<double>(fadeSamples);
        }
        double sample = 0.0;
        for (double freq : freqsHz) sample += std::sin(2.0 * M_PI * freq * t);
        source[n] = static_cast<float>((sample / static_cast<double>(freqsHz.size())) * 0.6 * envelope);
    }
    return source;
}

// A handful of short filtered-noise-like percussive bursts separated by
// silence, to expose echo density/onset smearing on transient material
// (DS-6). Deterministic: a fixed-seed xorshift PRNG, not std::random.
// burstCount=1 (round 3's single-transient centre-image check) reuses the
// identical burst shape as the round-2 4-burst sequence.
std::vector<float> transientBurstSequence(std::size_t tailSamples, std::size_t burstCount = 4) {
    constexpr double burstSeconds = 0.008;
    constexpr double gapSeconds = 0.22;
    const std::size_t burstSamples = static_cast<std::size_t>(kSampleRate * burstSeconds);
    const std::size_t gapSamples = static_cast<std::size_t>(kSampleRate * gapSeconds);
    const std::size_t sourceSamples = burstCount * (burstSamples + gapSamples);
    std::vector<float> source(sourceSamples + tailSamples, 0.0F);

    std::uint32_t state = 0x1DEA5EEDU; // fixed seed, repeatable
    auto nextUniform = [&state]() -> float {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (static_cast<float>(state) / static_cast<float>(0xFFFFFFFFU)) * 2.0F - 1.0F;
    };

    for (std::size_t burst = 0; burst < burstCount; ++burst) {
        const std::size_t start = burst * (burstSamples + gapSamples);
        // One-pole lowpass on white noise for a duller "hit" rather than
        // full-band hiss, and a short linear decay envelope within the burst.
        float lowpassState = 0.0F;
        for (std::size_t i = 0; i < burstSamples; ++i) {
            const float white = nextUniform();
            lowpassState = 0.3F * white + 0.7F * lowpassState;
            const float envelope = 1.0F - (static_cast<float>(i) / static_cast<float>(burstSamples));
            source[start + i] = 0.8F * lowpassState * envelope;
        }
    }
    return source;
}

struct RenderResult {
    std::string label;
    std::vector<float> left;
    std::vector<float> right;
};

bool renderInto(const std::vector<float>& source, double decayNormalized, double dampNormalized,
                 double mixNormalized, RenderResult& result) {
    DiffusionStereoPath path;
    if (!path.prepare(evaluationFixtureConfig(kSampleRate))) return false;
    if (!path.setDecay(decayNormalized) || !path.setDamp(dampNormalized) || !path.setMix(mixNormalized)) {
        return false;
    }

    std::vector<float> silence(kRampLengthSamples, 0.0F);
    std::vector<float> primeLeft(kRampLengthSamples);
    std::vector<float> primeRight(kRampLengthSamples);
    path.process(silence.data(), primeLeft.data(), primeRight.data(), kRampLengthSamples);

    result.left.assign(source.size(), 0.0F);
    result.right.assign(source.size(), 0.0F);
    path.process(source.data(), result.left.data(), result.right.data(), source.size());

    return path.nonFiniteCount() == 0;
}

// Writes every render in the group with ONE SHARED peak-normalization gain
// (computed across all of them together), so a comparison across files
// preserves their real relative levels -- fixing round 1's per-file
// normalization flaw for exactly the comparisons that need it. Also prints
// each file's own pre-normalization peak/RMS (not just the group-shared
// peak), per Sol's 2026-09-18 review: DS-5's "peak under ordinary programme
// material" trigger condition needs exactly this number, and it was
// previously computed but not captured into testing.md.
bool writeSharedNormalizedGroup(const std::vector<RenderResult>& group, const std::string& outputDir) {
    float peak = 0.0F;
    for (const auto& r : group) {
        for (float s : r.left) peak = std::max(peak, std::fabs(s));
        for (float s : r.right) peak = std::max(peak, std::fabs(s));
    }
    const float gain = (peak > 0.0F) ? (0.891F / peak) : 1.0F;

    for (const auto& r : group) {
        float ownPeak = 0.0F;
        double sumSquares = 0.0;
        for (float s : r.left) {
            ownPeak = std::max(ownPeak, std::fabs(s));
            sumSquares += static_cast<double>(s) * static_cast<double>(s);
        }
        for (float s : r.right) {
            ownPeak = std::max(ownPeak, std::fabs(s));
            sumSquares += static_cast<double>(s) * static_cast<double>(s);
        }
        const double rms = std::sqrt(sumSquares / static_cast<double>(2 * r.left.size()));

        std::vector<float> left = r.left;
        std::vector<float> right = r.right;
        for (float& s : left) s *= gain;
        for (float& s : right) s *= gain;
        const std::string path = outputDir + "/" + r.label + ".wav";
        if (!writeStereoWav(path, left, right)) {
            std::cerr << "Unable to write " << path << "\n";
            return false;
        }
        std::cerr << "Wrote " << path << " (" << r.left.size() << " samples, "
                  << (static_cast<double>(r.left.size()) / kSampleRate) << "s), own pre-normalization peak="
                  << ownPeak << ", own pre-normalization RMS=" << rms << ", group-shared peak=" << peak
                  << ", group-shared gain=" << gain << "\n";
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string outputDir = (argc > 1) ? argv[1] : "artifacts";

    // --- Decay isolation: short / baseline / long, impulse source. ---
    {
        std::vector<RenderResult> group;
        const std::array<std::pair<const char*, double>, 3> settings {{
            {"round2-decay-short_decay0.1_damp0.0_mix1.0", 0.1},
            {"round2-decay-baseline_decay0.5_damp0.0_mix1.0", 0.5},
            {"round2-decay-long_decay0.9_damp0.0_mix1.0", 0.9},
        }};
        for (const auto& [label, decay] : settings) {
            RenderResult result;
            result.label = label;
            const std::size_t total = tailSamplesFor(decay);
            if (!renderInto(impulseSource(total), decay, 0.0, 1.0, result)) {
                std::cerr << "Decay isolation render failed for " << label << "\n";
                return 1;
            }
            group.push_back(std::move(result));
        }
        if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
    }

    // --- Damp isolation: off / full, impulse source. More extreme than
    // round 1's Damp=0.3 (found "only subtly distinguishable"), per round
    // 1's own deferred suggestion of a more extreme comparison. ---
    {
        std::vector<RenderResult> group;
        const std::array<std::pair<const char*, double>, 2> settings {{
            {"round2-damp-off_decay0.5_damp0.0_mix1.0", 0.0},
            {"round2-damp-full_decay0.5_damp1.0_mix1.0", 1.0},
        }};
        for (const auto& [label, damp] : settings) {
            RenderResult result;
            result.label = label;
            const std::size_t total = tailSamplesFor(0.5);
            if (!renderInto(impulseSource(total), 0.5, damp, 1.0, result)) {
                std::cerr << "Damp isolation render failed for " << label << "\n";
                return 1;
            }
            group.push_back(std::move(result));
        }
        if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
    }

    // --- Mix isolation: dry / half / wet, SUSTAINED tone source, shared
    // batch normalization -- the round 1 fix. ---
    {
        std::vector<RenderResult> group;
        const std::size_t tail = tailSamplesFor(0.5);
        const std::vector<float> tone = sustainedTone(220.0, 2.0, tail);
        const std::array<std::pair<const char*, double>, 3> settings {{
            {"round2-mix-dry_decay0.5_damp0.0_mix0.0", 0.0},
            {"round2-mix-half_decay0.5_damp0.0_mix0.5", 0.5},
            {"round2-mix-wet_decay0.5_damp0.0_mix1.0", 1.0},
        }};
        for (const auto& [label, mix] : settings) {
            RenderResult result;
            result.label = label;
            if (!renderInto(tone, 0.5, 0.0, mix, result)) {
                std::cerr << "Mix isolation render failed for " << label << "\n";
                return 1;
            }
            group.push_back(std::move(result));
        }
        if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
    }

    // --- Musical corpus, at the wrapper's own default settings. ---
    {
        const std::size_t tail = tailSamplesFor(0.5);
        const std::array<std::pair<const char*, std::vector<float>>, 3> corpus {{
            {"round2-corpus-pluck-chord_defaults", pluckChord(tail)},
            {"round2-corpus-sustained-pad_defaults", sustainedPad(tail)},
            {"round2-corpus-transient-bursts_defaults", transientBurstSequence(tail)},
        }};
        for (const auto& [label, source] : corpus) {
            std::vector<RenderResult> group;
            RenderResult result;
            result.label = label;
            if (!renderInto(source, 0.5, 0.0, 1.0, result)) {
                std::cerr << "Corpus render failed for " << label << "\n";
                return 1;
            }
            group.push_back(std::move(result));
            if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
        }
    }

    // --- Stereo vs. mono fold-down, on the sustained-pad corpus item, at
    // defaults -- DS-8/DS-9 measured a mono-sum and channel-balance
    // consequence; only listening can judge whether it matters. ---
    {
        const std::size_t tail = tailSamplesFor(0.5);
        RenderResult stereo;
        stereo.label = "round2-stereo-vs-mono-stereo_defaults";
        if (!renderInto(sustainedPad(tail), 0.5, 0.0, 1.0, stereo)) {
            std::cerr << "Stereo/mono comparison render failed\n";
            return 1;
        }
        float peak = 0.0F;
        for (float s : stereo.left) peak = std::max(peak, std::fabs(s));
        for (float s : stereo.right) peak = std::max(peak, std::fabs(s));
        std::vector<float> mono(stereo.left.size());
        for (std::size_t n = 0; n < stereo.left.size(); ++n) {
            mono[n] = 0.5F * (stereo.left[n] + stereo.right[n]);
            peak = std::max(peak, std::fabs(mono[n]));
        }
        const float gain = (peak > 0.0F) ? (0.891F / peak) : 1.0F;
        std::vector<float> stereoLeft = stereo.left;
        std::vector<float> stereoRight = stereo.right;
        for (float& s : stereoLeft) s *= gain;
        for (float& s : stereoRight) s *= gain;
        for (float& s : mono) s *= gain;

        const std::string stereoPath = outputDir + "/round2-stereo-vs-mono-stereo_defaults.wav";
        const std::string monoPath = outputDir + "/round2-stereo-vs-mono-monofold_defaults.wav";
        if (!writeStereoWav(stereoPath, stereoLeft, stereoRight) || !writeMonoWav(monoPath, mono)) {
            std::cerr << "Unable to write stereo/mono comparison files\n";
            return 1;
        }
        std::cerr << "Wrote " << stereoPath << " and " << monoPath << " (shared peak=" << peak
                  << ", shared gain=" << gain << ")\n";
    }

    // --- Round 3, per Sol's 2026-09-18 review of the round-1/round-2
    // evidence (docs/testing.md). Three items, none authorizing any DSP or
    // ADR change: ---

    // (1) Mix=0.5 stereo-"swap" falsification at three frequencies. If the
    // uneven left/right character differs by frequency, that corroborates
    // the modal-residue hypothesis (ADR-006 (e)'s disjoint tap support
    // giving L/R different residues of the network's shared poles); if it
    // is identical at every frequency, that points toward a defect
    // instead. Frequencies match DS-13's measured H_L/H_R comparison
    // points in tests/DiffusionStereoPathTests.cpp.
    {
        std::vector<RenderResult> group;
        const std::size_t tail = tailSamplesFor(0.5);
        const std::array<std::pair<const char*, double>, 3> settings {{
            {"round3-mix-swap-220hz_decay0.5_damp0.0_mix0.5", 220.0},
            {"round3-mix-swap-277hz_decay0.5_damp0.0_mix0.5", 277.0},
            {"round3-mix-swap-330hz_decay0.5_damp0.0_mix0.5", 330.0},
        }};
        for (const auto& [label, freqHz] : settings) {
            RenderResult result;
            result.label = label;
            if (!renderInto(sustainedTone(freqHz, 2.0, tail), 0.5, 0.0, 0.5, result)) {
                std::cerr << "Mix=0.5 frequency-falsification render failed for " << label << "\n";
                return 1;
            }
            group.push_back(std::move(result));
        }
        if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
    }

    // (2) Metallic-confound isolation on the transient-burst corpus item:
    // a dry (source-only) reference, a Damp sweep, and a product-plausible
    // Mix, all under one shared normalization so they are directly
    // comparable. If "metallic" tracks Damp and largely disappears by
    // Damp=0.5, it is this evaluation setting, not the network topology;
    // the dry reference isolates whatever the synthesized source itself
    // contributes, independent of the network entirely.
    //
    // Uses a SINGLE burst (burstCount=1), not the round-2 corpus item's
    // four-hit sequence: the 220ms inter-hit gap there is far shorter than
    // this fixture's ~1.09s T60_0, so successive tails pile up rather than
    // decaying to a clean, quiet window between hits -- exactly the
    // condition needed to actually hear Damp's spectral coloring. This was
    // caught by the owner reporting that damp=0.0/0.5/1.0 (the same 0.0-vs-
    // 1.0 extremes round 2's damp-off/damp-full comparison already showed
    // were clearly distinguishable, on an impulse source) "all sound the
    // same" on the four-hit sequence -- a test-design flaw, not a DSP
    // defect, per that already-established contradicting result.
    {
        std::vector<RenderResult> group;
        const std::size_t tail = tailSamplesFor(0.5);
        const std::vector<float> burst = transientBurstSequence(tail, 1);
        struct Confound { const char* label; double decay, damp, mix; };
        const std::array<Confound, 5> settings {{
            {"round3-transient-confound-dry_mix0.0", 0.5, 0.0, 0.0},
            {"round3-transient-confound-damp0.0_mix1.0", 0.5, 0.0, 1.0},
            {"round3-transient-confound-damp0.5_mix1.0", 0.5, 0.5, 1.0},
            {"round3-transient-confound-damp1.0_mix1.0", 0.5, 1.0, 1.0},
            {"round3-transient-confound-damp0.0_mix0.3", 0.5, 0.0, 0.3},
        }};
        for (const auto& s : settings) {
            RenderResult result;
            result.label = s.label;
            if (!renderInto(burst, s.decay, s.damp, s.mix, result)) {
                std::cerr << "Transient confound-isolation render failed for " << s.label << "\n";
                return 1;
            }
            group.push_back(std::move(result));
        }
        if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
    }

    // (3) Single-transient centre-image check for the already-measured DS-9
    // asymmetry (L always arrives ~4.46ms before R at every rate/Decay, by
    // construction of the even/odd interleave putting the network's
    // shortest line in L; L also measures ~0.6dB louder). One hit, not
    // four, at defaults, so the image at onset is what's being judged, not
    // an averaged impression across repeated hits.
    {
        std::vector<RenderResult> group;
        const std::size_t tail = tailSamplesFor(0.5);
        RenderResult result;
        result.label = "round3-single-transient-center-check_defaults";
        if (!renderInto(transientBurstSequence(tail, 1), 0.5, 0.0, 1.0, result)) {
            std::cerr << "Single-transient centre-image render failed\n";
            return 1;
        }
        group.push_back(std::move(result));
        if (!writeSharedNormalizedGroup(group, outputDir)) return 1;
    }

    std::cerr << "\nRound 2/3 listening batch complete. All files share a peak reference only within\n"
                 "their own comparison group (decay / damp / mix / stereo-vs-mono / mix-swap-freq /\n"
                 "transient-confound), never across groups, and standalone items (corpus, single-\n"
                 "transient check) are independently normalized. This is an OBSERVATION set per\n"
                 "ADR-006 and the roadmap's Sonic acceptance gate, not an acceptance claim.\n";
    return 0;
}

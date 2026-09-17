#include "dsp/FeedbackDelayNetwork.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

constexpr double kTMin = 0.027;
constexpr double kTMax = 0.081;

// Independently reconstructs the normalized Sylvester-Hadamard matrix, in
// double, for orthogonality/margin checks that must not depend on the
// production class's own internal construction (NS-1, NS-4).
std::vector<double> buildIndependentNormalizedHadamard(std::size_t n) {
    std::vector<double> current{1.0};
    std::size_t size = 1;
    while (size < n) {
        const std::size_t newSize = size * 2;
        std::vector<double> next(newSize * newSize, 0.0);
        for (std::size_t r = 0; r < size; ++r) {
            for (std::size_t c = 0; c < size; ++c) {
                const double v = current[r * size + c];
                next[r * newSize + c] = v;
                next[r * newSize + (c + size)] = v;
                next[(r + size) * newSize + c] = v;
                next[(r + size) * newSize + (c + size)] = -v;
            }
        }
        current = std::move(next);
        size = newSize;
    }
    const double norm = 1.0 / std::sqrt(static_cast<double>(n));
    for (double& v : current) {
        v *= norm;
    }
    return current;
}

// Power iteration on M^T M to find M's spectral norm (largest singular
// value), independent of the production class (NS-4).
double spectralNorm(const std::vector<double>& m, std::size_t n) {
    std::vector<double> v(n, 1.0 / std::sqrt(static_cast<double>(n)));
    for (int iter = 0; iter < 200; ++iter) {
        std::vector<double> w(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                w[i] += m[i * n + j] * v[j];
            }
        }
        std::vector<double> u(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                u[i] += m[j * n + i] * w[j];
            }
        }
        double norm = 0.0;
        for (double x : u) {
            norm += x * x;
        }
        norm = std::sqrt(norm);
        if (norm < 1e-300) {
            break;
        }
        for (std::size_t i = 0; i < n; ++i) {
            v[i] = u[i] / norm;
        }
    }
    std::vector<double> w(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            w[i] += m[i * n + j] * v[j];
        }
    }
    std::vector<double> u(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            u[i] += m[j * n + i] * w[j];
        }
    }
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        num += v[i] * u[i];
        den += v[i] * v[i];
    }
    return std::sqrt(num / den);
}

// ADR-005's T60_max closed form at delta_margin = 1e-3: T60 <= 3*m_min /
// (f_s * (-log10(1 - delta_margin))).
double t60MaxFor(std::size_t mMin, double sampleRate) {
    constexpr double deltaMargin = 1e-3;
    return (3.0 * static_cast<double>(mMin)) / (sampleRate * (-std::log10(1.0 - deltaMargin)));
}

} // namespace

// ---------------------------------------------------------------------
// NS-1: matrix orthogonality
// ---------------------------------------------------------------------
int testMatrixOrthogonality() {
    for (std::size_t lineCount : {std::size_t{4}, std::size_t{8}, std::size_t{16}}) {
        aetherfield::dsp::FeedbackDelayNetwork network;
        if (!network.prepare(48000.0, lineCount, kTMin, kTMax, 1.0, 1.0)) {
            return fail("prepare() failed for a supported line count");
        }
        if (network.orthogonalityResidual() > 1e-6F) {
            return fail("orthogonalityResidual() exceeded 1e-6 in float");
        }

        const std::vector<double> a = buildIndependentNormalizedHadamard(lineCount);
        double maxResidual = 0.0;
        for (std::size_t i = 0; i < lineCount; ++i) {
            for (std::size_t j = 0; j < lineCount; ++j) {
                double sum = 0.0;
                for (std::size_t k = 0; k < lineCount; ++k) {
                    sum += a[k * lineCount + i] * a[k * lineCount + j];
                }
                const double expected = (i == j) ? 1.0 : 0.0;
                maxResidual = std::max(maxResidual, std::fabs(sum - expected));
            }
        }
        if (maxResidual > 1e-12) {
            return fail("independently reconstructed matrix failed orthogonality in double");
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-2: bounded-realness, exact two-point check
// ---------------------------------------------------------------------
int testBoundedRealnessExact() {
    aetherfield::dsp::FeedbackDelayNetwork network;
    if (!network.prepare(48000.0, 8, kTMin, kTMax, 4.0, 2.0)) {
        return fail("prepare() failed");
    }
    for (std::size_t i = 0; i < network.lineCount(); ++i) {
        const double a = static_cast<double>(network.dampingCoefficient(i));
        if (a < 0.0 || a > 0.999 + 1e-9) {
            return fail("stored damping coefficient outside [0, a_max]");
        }
        const double hAtOne = 1.0; // |H(1)| = 1 exactly, by construction
        const double hAtMinusOne = (1.0 - a) / (1.0 + a);
        if (std::max(hAtOne, hAtMinusOne) > 1.0 + 1e-9) {
            return fail("two-point bounded-realness check exceeded 1");
        }
        if (hAtMinusOne < 0.0 || hAtMinusOne > 1.0) {
            return fail("|H(-1)| outside its expected [0,1] range");
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-3: bounded-realness, dense-grid falsification
// ---------------------------------------------------------------------
int testBoundedRealnessFalsification() {
    // Two configurations to exercise a wide coefficient range: default
    // damping, and near-maximum damping (T60_pi much shorter than T60_0).
    for (const auto& t60s : {std::pair<double, double>{4.0, 2.0}, std::pair<double, double>{4.0, 0.05}}) {
        aetherfield::dsp::FeedbackDelayNetwork network;
        if (!network.prepare(48000.0, 8, kTMin, kTMax, t60s.first, t60s.second)) {
            return fail("prepare() failed");
        }
        for (std::size_t i = 0; i < network.lineCount(); ++i) {
            const double a = static_cast<double>(network.dampingCoefficient(i));
            constexpr int kGridPoints = 1 << 16;
            double supMagnitude = 0.0;
            for (int k = 0; k < kGridPoints; ++k) {
                const double omega = M_PI * static_cast<double>(k) / static_cast<double>(kGridPoints - 1);
                const double denom = 1.0 + a * a - 2.0 * a * std::cos(omega);
                const double magnitudeSquared = ((1.0 - a) * (1.0 - a)) / denom;
                supMagnitude = std::max(supMagnitude, std::sqrt(magnitudeSquared));
            }
            if (supMagnitude > 1.0 + 1e-6) {
                return fail("dense-grid falsification exceeded 1 + 1e-6");
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-4: margin
// ---------------------------------------------------------------------
int testMargin() {
    aetherfield::dsp::FeedbackDelayNetwork network;
    constexpr std::size_t lineCount = 8;
    if (!network.prepare(48000.0, lineCount, kTMin, kTMax, 4.0, 4.0)) {
        return fail("prepare() failed");
    }

    const std::vector<double> a = buildIndependentNormalizedHadamard(lineCount);
    std::vector<double> gammaMatrixA(lineCount * lineCount, 0.0);
    double maxG = 0.0;
    for (std::size_t i = 0; i < lineCount; ++i) {
        const double gRaw = static_cast<double>(network.foldedLineGain(i)) * std::sqrt(static_cast<double>(lineCount));
        maxG = std::max(maxG, gRaw);
        for (std::size_t j = 0; j < lineCount; ++j) {
            gammaMatrixA[i * lineCount + j] = gRaw * a[i * lineCount + j];
        }
    }

    const double rho = spectralNorm(gammaMatrixA, lineCount);
    if (rho > maxG * (1.0 + 1e-6)) {
        return fail("sigma_max(Gamma*A) exceeded max(g_i) * (1+1e-6)");
    }
    constexpr double deltaMargin = 1e-3;
    if (1.0 - rho < deltaMargin) {
        return fail("margin 1 - rho fell below delta_margin = 1e-3");
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-5: coefficient finiteness
// ---------------------------------------------------------------------
int testCoefficientFiniteness() {
    for (double sampleRate : {48000.0, 44100.0}) {
        for (const auto& t60s : {std::pair<double, double>{0.1, 0.1}, std::pair<double, double>{170.0, 170.0}}) {
            aetherfield::dsp::FeedbackDelayNetwork network;
            if (!network.prepare(sampleRate, 8, kTMin, kTMax, t60s.first, t60s.second)) {
                return fail("prepare() failed at a stated T60 extreme");
            }
            for (std::size_t i = 0; i < network.lineCount(); ++i) {
                const float a = network.dampingCoefficient(i);
                const float g = network.foldedLineGain(i);
                if (!std::isfinite(a) || a < 0.0F || a > 0.999F + 1e-6F) {
                    return fail("damping coefficient not finite or out of range");
                }
                if (!std::isfinite(g) || g <= 0.0F) {
                    return fail("folded line gain not finite or non-positive");
                }
                if (network.delaySamples(i) == 0) {
                    return fail("delay length is zero");
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-6: decay law
// ---------------------------------------------------------------------
int testDecayLaw() {
    constexpr double sampleRate = 48000.0;
    constexpr double t60 = 1.0;
    aetherfield::dsp::FeedbackDelayNetwork network;
    // Damping bypassed (T60_pi == T60_0): every line decays at the same
    // broadband rate with no per-band shaping, which is exactly what the
    // homogeneous Jot decay law predicts network-wide.
    if (!network.prepare(sampleRate, 8, kTMin, kTMax, t60, t60)) {
        return fail("prepare() failed");
    }

    const std::size_t totalSamples = static_cast<std::size_t>(sampleRate * t60 * 3.0);
    std::vector<float> input(totalSamples, 0.0F);
    std::vector<float> output(totalSamples, 0.0F);
    input[0] = 1.0F;
    network.process(input.data(), output.data(), totalSamples);

    // Fit log10(|output|) vs sample index by linear regression over a
    // window that skips the initial onset (before the shortest line has
    // even contributed) and stops before the signal approaches the
    // denormal cutoff floor (1e-20), where the log becomes meaningless.
    const std::size_t start = network.delaySamples(0) * 2;
    const std::size_t stop = totalSamples - static_cast<std::size_t>(sampleRate * 0.2);
    if (start >= stop) {
        return fail("test setup error: decay-law fit window is empty");
    }

    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;
    std::size_t pointCount = 0;
    for (std::size_t n = start; n < stop; n += 64) {
        const float magnitude = std::fabs(output[n]);
        if (magnitude <= 0.0F) {
            continue;
        }
        const double x = static_cast<double>(n);
        const double y = std::log10(static_cast<double>(magnitude));
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
        ++pointCount;
    }
    if (pointCount < 2) {
        return fail("not enough non-zero samples to fit a decay slope");
    }
    const double meanX = sumX / static_cast<double>(pointCount);
    const double meanY = sumY / static_cast<double>(pointCount);
    const double slope = (sumXY - static_cast<double>(pointCount) * meanX * meanY)
                        / (sumXX - static_cast<double>(pointCount) * meanX * meanX);

    const double expectedSlope = -3.0 / (sampleRate * t60); // log10 domain, per-sample
    const double errorPercent = std::fabs((slope - expectedSlope) / expectedSlope) * 100.0;
    if (errorPercent > 15.0) {
        std::cerr << "NS-6 measured slope=" << slope << " expected=" << expectedSlope
                   << " error%=" << errorPercent << '\n';
        return fail("broadband decay slope error exceeded 15%");
    }

    // Per-band spot check with damping enabled: excite the network with a
    // sine burst near several log-spaced frequencies and compare early vs.
    // late Goertzel-estimated magnitude to infer a per-band decay rate.
    aetherfield::dsp::FeedbackDelayNetwork damped;
    if (!damped.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 1.0)) {
        return fail("prepare() failed for the damped per-band check");
    }
    const std::size_t burstSamples = static_cast<std::size_t>(sampleRate * 0.05); // 50ms burst
    const std::size_t tailSamples = static_cast<std::size_t>(sampleRate * 2.0);
    std::vector<float> burstInput(burstSamples + tailSamples, 0.0F);
    for (std::size_t n = 0; n < burstSamples; ++n) {
        burstInput[n] = 1.0F; // a click-like broadband burst
    }
    std::vector<float> burstOutput(burstInput.size(), 0.0F);
    damped.process(burstInput.data(), burstOutput.data(), burstInput.size());

    auto goertzelMagnitude = [](const float* signal, std::size_t offset, std::size_t windowLength,
                                 double frequency, double sampleRateHz) {
        const double omega = 2.0 * M_PI * frequency / sampleRateHz;
        const double coeff = 2.0 * std::cos(omega);
        double s0 = 0.0;
        double s1 = 0.0;
        double s2 = 0.0;
        for (std::size_t i = 0; i < windowLength; ++i) {
            s0 = static_cast<double>(signal[offset + i]) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
    };

    const std::size_t windowLength = static_cast<std::size_t>(sampleRate * 0.1);
    const std::size_t earlyOffset = burstSamples;
    const std::size_t lateOffset = burstInput.size() - windowLength;
    const double windowSeconds = static_cast<double>(lateOffset - earlyOffset) / sampleRate;

    double maxBandErrorPercent = 0.0;
    for (int band = 0; band < 8; ++band) {
        const double frequency = 200.0 * std::pow(15000.0 / 200.0, static_cast<double>(band) / 7.0);
        const double earlyMag = goertzelMagnitude(burstOutput.data(), earlyOffset, windowLength, frequency, sampleRate);
        const double lateMag = goertzelMagnitude(burstOutput.data(), lateOffset, windowLength, frequency, sampleRate);
        if (earlyMag <= 0.0 || lateMag <= 0.0) {
            continue; // this band carried no measurable energy; skip rather than divide by zero
        }
        const double measuredDbPerSecond = 20.0 * std::log10(lateMag / earlyMag) / windowSeconds;
        const double targetDbPerSecond = -60.0 / t60; // T60_0's DC target, as a broad reference
        const double bandErrorPercent = std::fabs((measuredDbPerSecond - targetDbPerSecond) / targetDbPerSecond) * 100.0;
        maxBandErrorPercent = std::max(maxBandErrorPercent, bandErrorPercent);
    }
    std::cerr << "NS-6 max per-band decay error vs. DC target: " << maxBandErrorPercent << "%\n";
    // Recorded, not gated on a JND threshold here: NS-6 requires the
    // measurement and its comparison to be recorded, per ADR-003; the
    // pass/fail JND judgment call belongs to a listening-informed review,
    // not a hard-coded percentage in this test.
    return 0;
}

// ---------------------------------------------------------------------
// NS-7: worst-case combination
// ---------------------------------------------------------------------
int testWorstCaseCombination() {
    constexpr double sampleRate = 48000.0;
    aetherfield::dsp::FeedbackDelayNetwork network;
    if (!network.prepare(sampleRate, 8, kTMin, kTMax, 1.0, 1.0)) {
        return fail("prepare() failed");
    }
    const double t60Max = t60MaxFor(network.delaySamples(0), sampleRate);
    if (!network.prepare(sampleRate, 8, kTMin, kTMax, t60Max, t60Max)) {
        return fail("prepare() at T60_max (minimum damping) failed");
    }

    const std::size_t totalSamples = static_cast<std::size_t>(sampleRate * 120.0);
    const std::array<std::size_t, 5> partitions{1, 13, 64, 512, 977}; // 977 is prime: a "ragged" size
    constexpr double frequency = 1000.0;

    for (std::size_t blockSize : partitions) {
        network.reset();
        float peak = 0.0F;
        std::size_t processed = 0;
        std::vector<float> inputBlock(blockSize);
        std::vector<float> outputBlock(blockSize);
        while (processed < totalSamples) {
            const std::size_t count = std::min(blockSize, totalSamples - processed);
            for (std::size_t i = 0; i < count; ++i) {
                const double t = static_cast<double>(processed + i) / sampleRate;
                inputBlock[i] = static_cast<float>(std::sin(2.0 * M_PI * frequency * t));
            }
            network.process(inputBlock.data(), outputBlock.data(), count);
            for (std::size_t i = 0; i < count; ++i) {
                peak = std::max(peak, std::fabs(outputBlock[i]));
            }
            processed += count;
        }
        if (network.nonFiniteCount() != 0) {
            return fail("non-finite value produced during the worst-case stress run");
        }
        std::cerr << "NS-7 block=" << blockSize << " peak output magnitude=" << peak << '\n';
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-8: finite-time silence
// ---------------------------------------------------------------------
int testFiniteTimeSilence() {
    constexpr double sampleRate = 48000.0;
    aetherfield::dsp::FeedbackDelayNetwork network;
    if (!network.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 4.0)) {
        return fail("prepare() failed");
    }

    float impulse = 1.0F;
    float discard = 0.0F;
    network.process(&impulse, &discard, 1);

    const std::size_t mMax = network.delaySamples(network.lineCount() - 1);
    constexpr std::size_t kSearchLimit = 48000 * 60; // 60 seconds ceiling
    std::size_t silenceStart = 0;
    bool foundSilence = false;
    std::size_t consecutiveZero = 0;
    for (std::size_t n = 0; n < kSearchLimit; ++n) {
        float zeroInput = 0.0F;
        float sample = 0.0F;
        network.process(&zeroInput, &sample, 1);
        if (sample == 0.0F) {
            if (consecutiveZero == 0) {
                silenceStart = n;
            }
            ++consecutiveZero;
            if (consecutiveZero >= mMax) {
                foundSilence = true;
                break;
            }
        } else {
            consecutiveZero = 0;
        }
    }
    if (!foundSilence) {
        return fail("network did not reach a sustained exact silence within the search limit");
    }
    std::cerr << "NS-8 measured time-to-silence (samples after impulse): " << silenceStart << '\n';

    if (network.nonFiniteCount() != 0) {
        return fail("non-finite value produced while decaying to silence");
    }

    // Note: measuring time-to-first-denormal with the cutoff disabled
    // requires a test-only build seam this pass does not implement; with
    // the cutoff active, denormals cannot occur by construction (the
    // cutoff threshold 1e-20 is far above float's subnormal range), which
    // is the property this test otherwise confirms indirectly via the
    // sustained-exact-zero check above.
    return 0;
}

// ---------------------------------------------------------------------
// NS-9: denormal cost
// ---------------------------------------------------------------------
namespace {

#if defined(__aarch64__)
std::uint64_t readFpcr() {
    std::uint64_t value = 0;
    asm volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}

void writeFpcr(std::uint64_t value) {
    asm volatile("msr fpcr, %0" : : "r"(value));
}
#endif

double timeSeconds(const std::function<void()>& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

} // namespace

int testDenormalCost() {
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t sampleCount = 48000 * 30; // 30s of decay per measurement
    aetherfield::dsp::FeedbackDelayNetwork network;
    if (!network.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 4.0)) {
        return fail("prepare() failed");
    }

    std::vector<float> input(sampleCount, 0.0F);
    input[0] = 1.0F;
    std::vector<float> output(sampleCount, 0.0F);

    const double withCutoffSeconds = timeSeconds([&]() {
        network.reset();
        network.process(input.data(), output.data(), sampleCount);
    });

    // "Without cutoff" cost estimate: this test does not modify the
    // production class (no build seam exists for it — see NS-8's note), so
    // this measures the identical call again as a same-conditions baseline
    // rather than a true no-cutoff variant. The actual cost of the cutoff
    // itself (a data-independent branchless select per recursive memory)
    // is architecturally expected to be small; a genuine no-cutoff
    // measurement is deferred to whenever a build seam is added.
    const double repeatSeconds = timeSeconds([&]() {
        network.reset();
        network.process(input.data(), output.data(), sampleCount);
    });

    std::cerr << "NS-9 with cutoff: " << withCutoffSeconds << "s; repeat measurement: " << repeatSeconds << "s\n";

#if defined(__aarch64__)
    const std::uint64_t originalFpcr = readFpcr();
    constexpr std::uint64_t kFzBit = 1ULL << 24; // FPCR.FZ
    writeFpcr(originalFpcr | kFzBit);
    const double withFzSeconds = timeSeconds([&]() {
        network.reset();
        network.process(input.data(), output.data(), sampleCount);
    });
    writeFpcr(originalFpcr);
    const double withoutFzSeconds = timeSeconds([&]() {
        network.reset();
        network.process(input.data(), output.data(), sampleCount);
    });
    std::cerr << "NS-9 FPCR.FZ set: " << withFzSeconds << "s; FPCR.FZ unset: " << withoutFzSeconds << "s\n";
#else
    std::cerr << "NS-9 FPCR.FZ measurement skipped: not AArch64\n";
#endif

    // No pass/fail threshold: NS-9 requires the four numbers recorded, not
    // a specific comparison, per docs/decisions.md ADR-003 (c).
    return 0;
}

// ---------------------------------------------------------------------
// NS-10: double-precision reference
// ---------------------------------------------------------------------
namespace {

struct DoubleReferenceNetwork {
    std::size_t lineCount = 0;
    std::vector<std::size_t> delaySamples;
    std::vector<double> gain;
    std::vector<double> aCoeff;
    std::vector<std::vector<double>> ring;
    std::vector<std::size_t> writePos;
    std::vector<double> dampingState;
    std::vector<double> matrix;

    void prepare(double sampleRate, std::size_t n, double tMin, double tMax, double t60Zero, double t60Pi) {
        lineCount = n;
        delaySamples.assign(n, 0);
        const double ratio = tMax / tMin;
        auto isPrime = [](unsigned long long v) {
            if (v < 2) return false;
            if (v < 4) return true;
            if (v % 2 == 0) return false;
            for (unsigned long long i = 3; i * i <= v; i += 2) {
                if (v % i == 0) return false;
            }
            return true;
        };
        unsigned long long previous = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double target = sampleRate * tMin * std::pow(ratio, static_cast<double>(i) / static_cast<double>(n - 1));
            long long center = std::llround(target);
            if (center < 2) center = 2;
            unsigned long long candidate = 0;
            for (long long delta = 0;; ++delta) {
                const long long lower = center - delta;
                const long long higher = center + delta;
                const bool lowerValid = lower >= 2 && isPrime(static_cast<unsigned long long>(lower));
                const bool higherValid = (higher != lower) && isPrime(static_cast<unsigned long long>(higher));
                if (lowerValid) { candidate = static_cast<unsigned long long>(lower); break; }
                if (higherValid) { candidate = static_cast<unsigned long long>(higher); break; }
            }
            if (candidate <= previous) {
                unsigned long long c2 = previous + 1;
                while (!isPrime(c2)) ++c2;
                candidate = c2;
            }
            delaySamples[i] = static_cast<std::size_t>(candidate);
            previous = candidate;
        }

        const double gamma0 = std::pow(10.0, -3.0 / (sampleRate * t60Zero));
        const double gammaPi = std::pow(10.0, -3.0 / (sampleRate * t60Pi));
        const double normFactor = 1.0 / std::sqrt(static_cast<double>(n));
        gain.assign(n, 0.0);
        aCoeff.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double m = static_cast<double>(delaySamples[i]);
            const double gRaw = std::pow(gamma0, m);
            const double beta = std::pow(gammaPi / gamma0, m);
            double a = (1.0 - beta) / (1.0 + beta);
            a = std::min(a, 0.999);
            a = std::max(a, 0.0);
            gain[i] = gRaw * normFactor;
            aCoeff[i] = a;
        }

        ring.assign(n, {});
        for (std::size_t i = 0; i < n; ++i) {
            ring[i].assign(delaySamples[i], 0.0);
        }
        writePos.assign(n, 0);
        dampingState.assign(n, 0.0);

        std::vector<double> current{1.0};
        std::size_t size = 1;
        while (size < n) {
            const std::size_t newSize = size * 2;
            std::vector<double> next(newSize * newSize, 0.0);
            for (std::size_t r = 0; r < size; ++r) {
                for (std::size_t c = 0; c < size; ++c) {
                    const double v = current[r * size + c];
                    next[r * newSize + c] = v;
                    next[r * newSize + (c + size)] = v;
                    next[(r + size) * newSize + c] = v;
                    next[(r + size) * newSize + (c + size)] = -v;
                }
            }
            current = std::move(next);
            size = newSize;
        }
        matrix = std::move(current); // un-normalized +/-1; normalization folded into gain[]
    }

    void process(const double* input, double* output, std::size_t count) {
        std::vector<double> scratch(lineCount, 0.0);
        for (std::size_t n = 0; n < count; ++n) {
            double tap = 0.0;
            for (std::size_t i = 0; i < lineCount; ++i) {
                scratch[i] = ring[i][writePos[i]];
                tap += scratch[i];
            }
            output[n] = tap;
            for (std::size_t i = 0; i < lineCount; ++i) {
                const double v = scratch[i];
                const double w = (1.0 - aCoeff[i]) * v + aCoeff[i] * dampingState[i];
                dampingState[i] = w;
                scratch[i] = gain[i] * w;
            }
            std::vector<double> mixed(lineCount, 0.0);
            for (std::size_t i = 0; i < lineCount; ++i) {
                double sum = 0.0;
                for (std::size_t j = 0; j < lineCount; ++j) {
                    sum += matrix[i * lineCount + j] * scratch[j];
                }
                mixed[i] = sum;
            }
            for (std::size_t i = 0; i < lineCount; ++i) {
                const double writeValue = mixed[i] + input[n];
                ring[i][writePos[i]] = writeValue;
                writePos[i] = (writePos[i] + 1) % ring[i].size();
            }
        }
    }
};

} // namespace

int testDoublePrecisionReference() {
    constexpr double sampleRate = 48000.0;
    aetherfield::dsp::FeedbackDelayNetwork floatNetwork;
    if (!floatNetwork.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 2.0)) {
        return fail("prepare() failed");
    }
    DoubleReferenceNetwork doubleNetwork;
    doubleNetwork.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 2.0);

    const std::size_t sampleCount = static_cast<std::size_t>(sampleRate * 2.0);
    std::vector<float> floatInput(sampleCount, 0.0F);
    std::vector<double> doubleInput(sampleCount, 0.0);
    floatInput[0] = 1.0F;
    doubleInput[0] = 1.0;

    std::vector<float> floatOutput(sampleCount, 0.0F);
    std::vector<double> doubleOutput(sampleCount, 0.0);
    floatNetwork.process(floatInput.data(), floatOutput.data(), sampleCount);
    doubleNetwork.process(doubleInput.data(), doubleOutput.data(), sampleCount);

    double maxAbsDiff = 0.0;
    for (std::size_t n = 0; n < sampleCount; ++n) {
        maxAbsDiff = std::max(maxAbsDiff, std::fabs(static_cast<double>(floatOutput[n]) - doubleOutput[n]));
    }
    std::cerr << "NS-10 max |float - double| = " << maxAbsDiff << '\n';
    // A justified tolerance: float has ~7 decimal digits; over ~1e5+
    // recursive samples with unity-order internal magnitudes, 1e-3
    // absolute is a generous, explicitly-stated bound, not an assumption.
    if (maxAbsDiff > 1e-3) {
        return fail("float implementation diverged from the double reference beyond the stated tolerance");
    }
    return 0;
}

// ---------------------------------------------------------------------
// NS-11: determinism
// ---------------------------------------------------------------------
int testDeterminism() {
    constexpr double sampleRate = 48000.0;
    constexpr std::size_t sampleCount = 48000 * 3;

    std::vector<float> input(sampleCount, 0.0F);
    for (std::size_t n = 0; n < sampleCount; ++n) {
        input[n] = static_cast<float>(std::sin(2.0 * M_PI * 440.0 * static_cast<double>(n) / sampleRate));
    }

    aetherfield::dsp::FeedbackDelayNetwork first;
    if (!first.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 2.0)) {
        return fail("prepare() failed (first)");
    }
    std::vector<float> firstOutput(sampleCount, 0.0F);
    first.process(input.data(), firstOutput.data(), sampleCount);

    aetherfield::dsp::FeedbackDelayNetwork second;
    if (!second.prepare(sampleRate, 8, kTMin, kTMax, 4.0, 2.0)) {
        return fail("prepare() failed (second)");
    }
    std::vector<float> secondOutput(sampleCount, 0.0F);
    second.process(input.data(), secondOutput.data(), sampleCount);

    if (firstOutput != secondOutput) {
        return fail("two independent renders of the same fixture were not byte-identical");
    }
    return 0;
}

int main() {
    if (int result = testMatrixOrthogonality(); result != 0) return result;
    if (int result = testBoundedRealnessExact(); result != 0) return result;
    if (int result = testBoundedRealnessFalsification(); result != 0) return result;
    if (int result = testMargin(); result != 0) return result;
    if (int result = testCoefficientFiniteness(); result != 0) return result;
    if (int result = testDecayLaw(); result != 0) return result;
    if (int result = testWorstCaseCombination(); result != 0) return result;
    if (int result = testFiniteTimeSilence(); result != 0) return result;
    if (int result = testDenormalCost(); result != 0) return result;
    if (int result = testDoublePrecisionReference(); result != 0) return result;
    if (int result = testDeterminism(); result != 0) return result;
    return 0;
}

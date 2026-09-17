#include "dsp/FeedbackDelayNetwork.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>

namespace aetherfield::dsp {

namespace {

constexpr float kDenormalCutoff = 1e-20F; // ADR-003 (b)
constexpr double kMaxDampingCoefficient = 0.999; // ADR-003 (a) a_max

bool isPrime(unsigned long long n) noexcept {
    if (n < 2) {
        return false;
    }
    if (n < 4) {
        return true; // 2, 3
    }
    if (n % 2 == 0) {
        return false;
    }
    for (unsigned long long i = 3; i * i <= n; i += 2) {
        if (n % i == 0) {
            return false;
        }
    }
    return true;
}

unsigned long long smallestPrimeAbove(unsigned long long n) noexcept {
    unsigned long long candidate = n + 1;
    while (!isPrime(candidate)) {
        ++candidate;
    }
    return candidate;
}

// Nearest prime to target, tie-broken to the smaller prime on an exact tie
// (ADR-005 (b)).
unsigned long long nearestPrime(double target) noexcept {
    long long center = static_cast<long long>(std::llround(target));
    if (center < 2) {
        center = 2;
    }
    for (long long delta = 0;; ++delta) {
        const long long lower = center - delta;
        const long long higher = center + delta;
        const bool lowerValid = lower >= 2 && isPrime(static_cast<unsigned long long>(lower));
        const bool higherValid = (higher != lower) && isPrime(static_cast<unsigned long long>(higher));
        if (lowerValid) {
            return static_cast<unsigned long long>(lower);
        }
        if (higherValid) {
            return static_cast<unsigned long long>(higher);
        }
    }
}

float applyCutoff(float x) noexcept {
    return (std::fabs(x) >= kDenormalCutoff) ? x : 0.0F;
}

// In-place fast Hadamard transform (un-normalized, +/-1 entries) via the
// Sylvester construction's butterfly. n must be a power of two.
// O(n log n) additions/subtractions, no multiplies, no allocation.
void fastHadamardTransformInPlace(float* x, std::size_t n) noexcept {
    for (std::size_t len = 1; len < n; len <<= 1) {
        for (std::size_t i = 0; i < n; i += (len << 1)) {
            for (std::size_t j = i; j < i + len; ++j) {
                const float a = x[j];
                const float b = x[j + len];
                x[j] = a + b;
                x[j + len] = a - b;
            }
        }
    }
}

} // namespace

FeedbackDelayNetwork::FeedbackDelayNetwork() noexcept = default;

bool FeedbackDelayNetwork::prepare(double sampleRate,
                                    std::size_t lineCount,
                                    double tMinSeconds,
                                    double tMaxSeconds,
                                    double t60ZeroSeconds,
                                    double t60PiSeconds) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
        return false;
    }
    if (lineCount != kMinLineCount && lineCount != 8 && lineCount != kMaxLineCount) {
        return false;
    }
    if (!std::isfinite(tMinSeconds) || !std::isfinite(tMaxSeconds) || tMinSeconds <= 0.0
        || tMaxSeconds <= 0.0 || !(tMinSeconds < tMaxSeconds)) {
        return false;
    }
    if (!std::isfinite(t60ZeroSeconds) || !std::isfinite(t60PiSeconds) || t60ZeroSeconds <= 0.0
        || t60PiSeconds <= 0.0 || t60PiSeconds > t60ZeroSeconds) {
        return false;
    }

    // Derive delay lengths (ADR-005 (b)) into local storage first, so a
    // downstream validation failure never disturbs a prior valid
    // preparation.
    std::vector<std::size_t> newDelaySamples(lineCount);
    {
        const double ratio = tMaxSeconds / tMinSeconds;
        unsigned long long previous = 0;
        for (std::size_t i = 0; i < lineCount; ++i) {
            const double exponent = static_cast<double>(i) / static_cast<double>(lineCount - 1);
            const double targetSeconds = tMinSeconds * std::pow(ratio, exponent);
            const double targetSamples = sampleRate * targetSeconds;
            unsigned long long candidate = nearestPrime(targetSamples);
            if (candidate <= previous) {
                candidate = smallestPrimeAbove(previous);
            }
            newDelaySamples[i] = static_cast<std::size_t>(candidate);
            previous = candidate;
        }
    }

    // Validate: prime and strictly increasing (guaranteed by construction;
    // checked defensively — a failure here is a programming error in the
    // derivation, not a caller input error, and still must not reach the
    // render thread). Pairwise co-prime is checked directly via gcd, never
    // inferred from primality (ADR-005).
    for (std::size_t i = 0; i < lineCount; ++i) {
        if (!isPrime(static_cast<unsigned long long>(newDelaySamples[i]))) {
            return false;
        }
        if (i > 0 && !(newDelaySamples[i] > newDelaySamples[i - 1])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < lineCount; ++i) {
        for (std::size_t j = i + 1; j < lineCount; ++j) {
            if (std::gcd(newDelaySamples[i], newDelaySamples[j]) != 1) {
                return false;
            }
        }
    }

    // Derive gains and damping coefficients in double (ADR-003 (a)).
    const double gamma0 = std::pow(10.0, -3.0 / (sampleRate * t60ZeroSeconds));
    const double gammaPi = std::pow(10.0, -3.0 / (sampleRate * t60PiSeconds));
    const double normFactor = 1.0 / std::sqrt(static_cast<double>(lineCount));

    std::vector<float> newLineGain(lineCount);
    std::vector<float> newA(lineCount);
    std::vector<float> newOneMinusA(lineCount);
    for (std::size_t i = 0; i < lineCount; ++i) {
        const double m = static_cast<double>(newDelaySamples[i]);
        const double gRaw = std::pow(gamma0, m);
        const double beta = std::pow(gammaPi / gamma0, m);
        double a = (1.0 - beta) / (1.0 + beta);
        a = std::min(a, kMaxDampingCoefficient);
        a = std::max(a, 0.0);
        const double gainFolded = gRaw * normFactor;

        if (!std::isfinite(gRaw) || !std::isfinite(a) || !std::isfinite(gainFolded)) {
            return false;
        }

        newLineGain[i] = static_cast<float>(gainFolded);
        newA[i] = static_cast<float>(a);
        newOneMinusA[i] = static_cast<float>(1.0 - a);
    }

    // Prepare each internal line at exactly its own mᵢ (docs/phase1-s2-plan.md
    // "Capacity"). Built into local storage first for the same reason as
    // above.
    std::vector<DelayLine> newLines(lineCount);
    for (std::size_t i = 0; i < lineCount; ++i) {
        if (!newLines[i].prepare(sampleRate, newDelaySamples[i])) {
            return false;
        }
    }

    // All validation passed: commit.
    lineCount_ = lineCount;
    lines_ = std::move(newLines);
    delaySamples_ = std::move(newDelaySamples);
    lineGain_ = std::move(newLineGain);
    dampingCoeffA_ = std::move(newA);
    dampingCoeffOneMinusA_ = std::move(newOneMinusA);
    dampingState_.assign(lineCount_, 0.0F);

    buildNormalizedHadamard();

    // Leaves the object in exactly the state reset() defines (ADR-003 (d)
    // rule 6). reset() does not clear the non-finite counter, matching its
    // own contract: an intervention recorded before this prepare() call
    // remains visible after it.
    reset();
    return true;
}

void FeedbackDelayNetwork::reset() noexcept {
    for (auto& line : lines_) {
        line.reset();
    }
    std::fill(dampingState_.begin(), dampingState_.end(), 0.0F);
    nonFiniteLatched_ = false;
}

void FeedbackDelayNetwork::process(const float* input, float* output, std::size_t count) noexcept {
    if (count == 0) {
        return;
    }

    for (std::size_t n = 0; n < count; ++n) {
        // 1. Peek every line's current output (before any write this sample).
        for (std::size_t i = 0; i < lineCount_; ++i) {
            scratch_[i] = lines_[i].peek();
        }

        // Output tap: the unweighted sum of the peeked values (test-only
        // convention; docs/phase1-s2-plan.md "What this plan is not
        // deciding").
        float tap = 0.0F;
        for (std::size_t i = 0; i < lineCount_; ++i) {
            tap += scratch_[i];
        }
        output[n] = tap;

        // 2. Damping filter, then gain (the matrix's 1/sqrt(lineCount_)
        // normalization is folded in here, per ADR-002's own suggestion).
        for (std::size_t i = 0; i < lineCount_; ++i) {
            const float v = scratch_[i];
            const float w = dampingCoeffOneMinusA_[i] * v + dampingCoeffA_[i] * dampingState_[i];

            if (!std::isfinite(w)) {
                flagNonFinite();
                dampingState_[i] = w; // stored as-is: never silently corrected
            } else {
                dampingState_[i] = applyCutoff(w);
            }

            scratch_[i] = lineGain_[i] * w;
        }

        // 3. Fixed Hadamard matrix (un-normalized +/-1 entries; the
        // normalization is already folded into lineGain_ above).
        fastHadamardTransformInPlace(scratch_.data(), lineCount_);

        // 4. Input injection: uniform across all lines. A non-finite input
        // sample is substituted with 0 for injection purposes and counted
        // (ADR-003 (c)'s table) — the one place a substitution is
        // authorized, because it is the *input*, not an internally-arising
        // value.
        const float rawInput = input[n];
        float effectiveInput = rawInput;
        if (!std::isfinite(rawInput)) {
            flagNonFinite();
            effectiveInput = 0.0F;
        }

        // 5. Push into each line, through the denormal cutoff — unless the
        // write value is itself non-finite, in which case it is stored
        // as-is (never clipped or substituted) and only counted.
        for (std::size_t i = 0; i < lineCount_; ++i) {
            const float writeValue = scratch_[i] + effectiveInput;
            if (!std::isfinite(writeValue)) {
                flagNonFinite();
                lines_[i].push(writeValue);
            } else {
                lines_[i].push(applyCutoff(writeValue));
            }
        }
    }
}

std::size_t FeedbackDelayNetwork::nonFiniteCount() const noexcept {
    return nonFiniteCount_;
}

bool FeedbackDelayNetwork::nonFiniteLatched() const noexcept {
    return nonFiniteLatched_;
}

float FeedbackDelayNetwork::orthogonalityResidual() const noexcept {
    return orthogonalityResidual_;
}

std::size_t FeedbackDelayNetwork::lineCount() const noexcept {
    return lineCount_;
}

std::size_t FeedbackDelayNetwork::delaySamples(std::size_t line) const noexcept {
    return delaySamples_[line];
}

float FeedbackDelayNetwork::dampingCoefficient(std::size_t line) const noexcept {
    return dampingCoeffA_[line];
}

float FeedbackDelayNetwork::foldedLineGain(std::size_t line) const noexcept {
    return lineGain_[line];
}

void FeedbackDelayNetwork::flagNonFinite() noexcept {
    if (nonFiniteCount_ < std::numeric_limits<std::size_t>::max()) {
        ++nonFiniteCount_;
    }
    nonFiniteLatched_ = true;
}

void FeedbackDelayNetwork::buildNormalizedHadamard() {
    const std::size_t n = lineCount_;

    // Sylvester construction: H(1) = [1]; H(2m) = [[H(m), H(m)], [H(m), -H(m)]].
    std::vector<float> current{1.0F};
    std::size_t size = 1;
    while (size < n) {
        const std::size_t newSize = size * 2;
        std::vector<float> next(newSize * newSize, 0.0F);
        for (std::size_t r = 0; r < size; ++r) {
            for (std::size_t c = 0; c < size; ++c) {
                const float v = current[r * size + c];
                next[r * newSize + c] = v;
                next[r * newSize + (c + size)] = v;
                next[(r + size) * newSize + c] = v;
                next[(r + size) * newSize + (c + size)] = -v;
            }
        }
        current = std::move(next);
        size = newSize;
    }

    // Normalize: A = H / sqrt(n), so that A^T A = I exactly (up to float
    // rounding), rather than H^T H = n*I.
    const float norm = 1.0F / std::sqrt(static_cast<float>(n));
    matrix_.assign(n * n, 0.0F);
    for (std::size_t idx = 0; idx < n * n; ++idx) {
        matrix_[idx] = current[idx] * norm;
    }

    // Orthogonality residual ||A^T A - I||_inf, computed independently of
    // the construction above, in double for headroom (ADR-003 (d) rule 4;
    // this is exactly NS-1's bound).
    double maxResidual = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                sum += static_cast<double>(matrix_[k * n + i]) * static_cast<double>(matrix_[k * n + j]);
            }
            const double expected = (i == j) ? 1.0 : 0.0;
            maxResidual = std::max(maxResidual, std::fabs(sum - expected));
        }
    }
    orthogonalityResidual_ = static_cast<float>(maxResidual);
}

} // namespace aetherfield::dsp

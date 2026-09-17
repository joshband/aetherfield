#pragma once

// Test-only DS-A/DS-B analysis helpers.
//
// Shared by tests/DiffusionStereoPathTests.cpp for both halves of
// docs/phases/phase1-ds-integration-plan.md's Task 4: sub-task 4a's
// DS-1/2/3/5/6/10/11/12 measurements and sub-task 4b's DS-4 (decay-law
// regression), DS-7 (interchannel coherence) and DS-8/DS-9 (mono
// compatibility, channel balance) measurements. This header is NOT linked
// into aetherfield_dsp and must not be included from any src/ file.
//
// Every function here is `inline` so the header stays safe to include from
// more than one test translation unit without an ODR violation, even though
// today only one executable (aetherfield_dsp_diffusion_stereo_tests) uses it.
//
// tests/SchroederAllpassTests.cpp (DS-A's own closed, independently verified
// suite) intentionally keeps its own local copies of the FFT/noise helpers
// below rather than including this header — that file predates this header
// and is not touched by this sub-task.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace aetherfield::dsp_test {

using Complex = std::complex<double>;

// Smallest power of two >= value.
inline std::size_t nextPowerOfTwo(std::size_t value) noexcept {
    std::size_t result = 1;
    while (result < value) result <<= 1U;
    return result;
}

// In-place iterative radix-2 FFT, decimation-in-time, natural output order.
// `values.size()` must be a power of two. Identical in method to DS-A's own
// copy in tests/SchroederAllpassTests.cpp.
inline void radix2Fft(std::vector<Complex>& values) {
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

// Deterministic LCG noise generator, matching DS-A's fixture generator
// exactly so cross-file measurements use the same statistical character.
inline float deterministicNoise(std::uint32_t& state) noexcept {
    state = state * 1664525U + 1013904223U;
    const float unit = static_cast<float>(state >> 8U) / 16777215.0F;
    return 0.75F * (2.0F * unit - 1.0F);
}

inline std::vector<float> makeNoiseScript(std::size_t count, std::uint32_t seed = 0xA37F4D29U) {
    std::vector<float> script(count);
    std::uint32_t state = seed;
    for (float& sample : script) sample = deterministicNoise(state);
    return script;
}

// ---- number theory for DS-3 (delay-length derivation) ----

inline bool isPrime(std::size_t n) noexcept {
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (std::size_t d = 3; d * d <= n; d += 2) {
        if (n % d == 0) return false;
    }
    return true;
}

// ADR-006 (c)'s bracket generalization: K target times, inclusive of both
// endpoints, geometrically spaced on [a, b]. Target i (0-indexed) is
// a * (b/a)^(i/(K-1)) for K >= 2. K == 1 degenerates to {a}; the bracket
// itself never uses K < 2.
inline std::vector<double> geometricTargets(double a, double b, std::size_t k) {
    std::vector<double> targets(k);
    if (k == 0) return targets;
    if (k == 1) { targets[0] = a; return targets; }
    for (std::size_t i = 0; i < k; ++i) {
        const double exponent = static_cast<double>(i) / static_cast<double>(k - 1);
        targets[i] = a * std::pow(b / a, exponent);
    }
    return targets;
}

// ---- shared bracket-iteration helper (ADR-006 (c)'s K_in x K_out x
// sample-rate bracket) ----
//
// DS-1/2/5/12 (and, per Task 4's plan, a future sub-task 4b's DS-4/7/8/9) all
// iterate the same two shapes: once per (rate, K_in) on the input side and
// once per (rate, K_out) on the output side, varying only what check runs per
// cascade and how that cascade gets built (some checks want a single built
// cascade, some want raw targets to build more than one cascade themselves,
// e.g. left/right output legs) -- so the callbacks receive only (rate, K)
// and are left to build whatever cascade(s) they need from it. Each callback
// returns 0 on success or a `fail(...)`-style nonzero result, matching this
// test suite's int-returning check-function convention; iteration stops at
// the first nonzero result. `rates`, `kIns` and `kOuts` are passed in rather
// than fixed here so callers can share their own bracket constants (e.g.
// kBracketRates/kBracketKIn/kBracketKOut) without this header needing to know
// their names or types.
//
// DS-3 does not fit this shape (its K_out loop is nested inside its K_in loop
// and shares FDN state across both) and is intentionally left iterating by
// hand rather than forced through this helper.
template <typename RateContainer, typename KInContainer, typename KOutContainer, typename InputCheck,
          typename OutputCheck>
int forEachBracketCascade(const RateContainer& rates, const KInContainer& kIns, const KOutContainer& kOuts,
                          InputCheck&& checkInput, OutputCheck&& checkOutput) {
    for (const auto& rate : rates) {
        for (const auto& kIn : kIns) {
            if (const int result = checkInput(rate, kIn); result != 0) return result;
        }
        for (const auto& kOut : kOuts) {
            if (const int result = checkOutput(rate, kOut); result != 0) return result;
        }
    }
    return 0;
}

// ---- Welch cross-spectral estimation (ADR-006 correction note C2) ----
//
// C2 requires DS-7's coherence to come from a Welch estimator with at least
// two complete segments, and to record the excitation, segment count and
// length, window, overlap, FFT length and silent-bin floor; a single
// periodogram is explicitly prohibited. These helpers are that estimator, and
// they carry every one of those settings back in the result so a reporting
// site cannot print a coherence number without also holding the parameters
// that produced it.
//
// Window choice, recorded rather than assumed: a *periodic* Hann window. Hann
// is the standard Welch default -- its -31 dB first sidelobe keeps a strong
// low-frequency component from leaking across the band, which matters here
// because a reverb tail's spectrum is not flat -- and at 50 % overlap
// consecutive periodic Hann segments sum to a constant, so no part of the
// record is systematically under-weighted. Nothing in the estimator depends
// on this particular window; a caller that prefers another one changes the
// single function below and records the change.

// Periodic (DFT-even) Hann window: w[n] = 0.5 - 0.5*cos(2*pi*n/length).
// Periodic rather than symmetric because these windows are used for spectral
// estimation, where the periodic form is the one that satisfies the
// constant-overlap-add condition at 50 % overlap.
inline std::vector<double> hannWindow(std::size_t length) {
    std::vector<double> window(length);
    if (length == 0) return window;
    if (length == 1) {
        window[0] = 1.0;
        return window;
    }
    for (std::size_t n = 0; n < length; ++n) {
        window[n] = 0.5
                  - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(length));
    }
    return window;
}

// One-sided Welch auto- and cross-spectral estimates over bins
// 0 .. fftLength/2 inclusive, plus the settings that produced them.
// `segmentCount == 0` means no complete segment fitted in the record, which
// hasMinimumWelchSegments() below rejects.
struct WelchCrossSpectra {
    std::size_t segmentCount = 0;
    std::size_t segmentLength = 0;
    std::size_t hopSize = 0;   // segmentLength/2 is 50 % overlap
    std::size_t fftLength = 0;
    std::vector<double> sxx;   // |X|^2 averaged over segments
    std::vector<double> syy;   // |Y|^2 averaged over segments
    std::vector<Complex> sxy;  // X * conj(Y) averaged over segments
};

// Welch estimate over `x` and `y`, which must be sampled at the same rate and
// aligned sample-for-sample. Segments advance by `hopSize`; only *complete*
// segments are used (a partial trailing segment is discarded rather than
// zero-padded, which would bias its spectrum downward). The FFT length is the
// next power of two at or above `segmentLength`, so a non-power-of-two
// segment is zero-padded to the transform length -- that interpolates bins,
// it does not add resolution, and callers should prefer a power-of-two
// segment length. Scaling is 1/(segmentCount * sum(w^2)), which is constant
// across bins and therefore cancels in a coherence ratio; it is applied so
// that PSD magnitudes are comparable between records of different lengths.
inline WelchCrossSpectra welchCrossSpectra(const std::vector<double>& x, const std::vector<double>& y,
                                           std::size_t segmentLength, std::size_t hopSize) {
    WelchCrossSpectra result;
    if (segmentLength == 0 || hopSize == 0) return result;
    const std::size_t usable = std::min(x.size(), y.size());
    if (usable < segmentLength) return result;

    const std::size_t fftLength = nextPowerOfTwo(segmentLength);
    const std::size_t bins = fftLength / 2 + 1;
    const std::vector<double> window = hannWindow(segmentLength);
    double windowEnergy = 0.0;
    for (const double weight : window) windowEnergy += weight * weight;
    if (!(windowEnergy > 0.0)) return result;

    result.sxx.assign(bins, 0.0);
    result.syy.assign(bins, 0.0);
    result.sxy.assign(bins, Complex(0.0, 0.0));
    std::vector<Complex> bufferX(fftLength);
    std::vector<Complex> bufferY(fftLength);

    for (std::size_t offset = 0; offset + segmentLength <= usable; offset += hopSize) {
        std::fill(bufferX.begin(), bufferX.end(), Complex(0.0, 0.0));
        std::fill(bufferY.begin(), bufferY.end(), Complex(0.0, 0.0));
        for (std::size_t n = 0; n < segmentLength; ++n) {
            bufferX[n] = Complex(x[offset + n] * window[n], 0.0);
            bufferY[n] = Complex(y[offset + n] * window[n], 0.0);
        }
        radix2Fft(bufferX);
        radix2Fft(bufferY);
        for (std::size_t bin = 0; bin < bins; ++bin) {
            result.sxx[bin] += std::norm(bufferX[bin]);
            result.syy[bin] += std::norm(bufferY[bin]);
            result.sxy[bin] += bufferX[bin] * std::conj(bufferY[bin]);
        }
        ++result.segmentCount;
    }
    if (result.segmentCount == 0) return result;

    const double scale = 1.0 / (static_cast<double>(result.segmentCount) * windowEnergy);
    for (std::size_t bin = 0; bin < bins; ++bin) {
        result.sxx[bin] *= scale;
        result.syy[bin] *= scale;
        result.sxy[bin] *= scale;
    }
    result.segmentLength = segmentLength;
    result.hopSize = hopSize;
    result.fftLength = fftLength;
    return result;
}

// Magnitude-squared coherence summarized over the bins that survive a
// silent-bin floor, together with the floor and the bin counts a report has
// to state alongside it.
struct CoherenceSummary {
    std::size_t segmentCount = 0;
    std::size_t examinedBins = 0;   // bins offered to the floor test
    std::size_t retainedBins = 0;   // bins that passed it
    double floorAbsolute = 0.0;     // the floor actually applied
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double median = 0.0;
};

// MSC = |Sxy|^2 / (Sxx * Syy), summarized over retained bins.
//
// DC (bin 0) and Nyquist (bin fftLength/2) are excluded unconditionally: both
// are real-valued for a real record, so their "coherence" is 1 by
// construction regardless of the signals, and including them would inflate
// every summary by an amount that says nothing about the path.
//
// A bin is retained only when *both* auto-spectra reach
// `floorFraction * max(Sxx, Syy)` over the examined bins. Below that the
// ratio is dominated by the float round-off of the render rather than by the
// signal, and a 0/0-shaped bin would contribute an arbitrary value. The
// fraction is the caller's declared silent-bin floor and is reported back in
// `floorAbsolute` so the applied threshold, not just the policy, is on record.
inline CoherenceSummary magnitudeSquaredCoherence(const WelchCrossSpectra& spectra, double floorFraction) {
    CoherenceSummary summary;
    summary.segmentCount = spectra.segmentCount;
    const std::size_t bins = spectra.sxx.size();
    if (bins < 3 || spectra.segmentCount == 0) return summary;

    double peak = 0.0;
    for (std::size_t bin = 1; bin + 1 < bins; ++bin) {
        peak = std::max({peak, spectra.sxx[bin], spectra.syy[bin]});
        ++summary.examinedBins;
    }
    summary.floorAbsolute = floorFraction * peak;

    std::vector<double> values;
    values.reserve(summary.examinedBins);
    for (std::size_t bin = 1; bin + 1 < bins; ++bin) {
        if (spectra.sxx[bin] < summary.floorAbsolute || spectra.syy[bin] < summary.floorAbsolute) continue;
        const double denominator = spectra.sxx[bin] * spectra.syy[bin];
        if (!(denominator > 0.0)) continue;
        values.push_back(std::norm(spectra.sxy[bin]) / denominator);
    }
    summary.retainedBins = values.size();
    if (values.empty()) return summary;

    std::sort(values.begin(), values.end());
    summary.minimum = values.front();
    summary.maximum = values.back();
    double sum = 0.0;
    for (const double value : values) sum += value;
    summary.mean = sum / static_cast<double>(values.size());
    const std::size_t middle = values.size() / 2;
    summary.median = (values.size() % 2 == 0) ? 0.5 * (values[middle - 1] + values[middle]) : values[middle];
    return summary;
}

// ---- time-domain correlation (ADR-006 correction note C2's second half) ----
//
// C2 requires the normalized zero-lag cross-correlation and a *declared*
// short-lag range to be reported separately from coherence, precisely because
// allpass phase differences can drive correlation down while leaving the
// ideal magnitude-squared coherence at 1.

// Pearson correlation coefficient between x[n] and y[n+lag], computed over
// exactly the overlap the lag leaves, with each series' mean removed over
// that same overlap (so it is a correlation coefficient, not a raw normalized
// inner product, and a small DC offset in either record cannot masquerade as
// correlation). Returns 0 for an overlap shorter than two samples or for a
// constant series, both of which are degenerate rather than uncorrelated;
// callers gate on their own anti-vacuity checks first.
inline double pearsonCorrelationAtLag(const std::vector<double>& x, const std::vector<double>& y,
                                      long long lag) noexcept {
    const auto count = static_cast<long long>(std::min(x.size(), y.size()));
    const long long begin = std::max<long long>(0, -lag);
    const long long end = std::min<long long>(count, count - lag);
    if (end - begin < 2) return 0.0;

    const auto span = static_cast<double>(end - begin);
    double sumX = 0.0;
    double sumY = 0.0;
    for (long long n = begin; n < end; ++n) {
        sumX += x[static_cast<std::size_t>(n)];
        sumY += y[static_cast<std::size_t>(n + lag)];
    }
    const double meanX = sumX / span;
    const double meanY = sumY / span;

    double sxy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    for (long long n = begin; n < end; ++n) {
        const double dx = x[static_cast<std::size_t>(n)] - meanX;
        const double dy = y[static_cast<std::size_t>(n + lag)] - meanY;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    if (!(sxx > 0.0) || !(syy > 0.0)) return 0.0;
    return sxy / std::sqrt(sxx * syy);
}

// Largest |Pearson correlation| over the symmetric lag range [-maxLag, maxLag]
// and the lag that attained it. The range is the caller's declared short-lag
// window; this helper never invents one.
struct ShortLagCorrelation {
    double magnitude = 0.0;
    long long lag = 0;
    double signedValue = 0.0;
};

inline ShortLagCorrelation maxShortLagCorrelation(const std::vector<double>& x, const std::vector<double>& y,
                                                  long long maxLag) noexcept {
    ShortLagCorrelation best;
    for (long long lag = -maxLag; lag <= maxLag; ++lag) {
        const double value = pearsonCorrelationAtLag(x, y, lag);
        if (std::abs(value) > best.magnitude) {
            best.magnitude = std::abs(value);
            best.lag = lag;
            best.signedValue = value;
        }
    }
    return best;
}

// ---- anti-vacuity guards (docs/phases/phase1-ds-integration-plan.md Task 4's
// first bullet: fixtures must contain nonzero wet samples before a
// decay/correlation/centroid/density result is accepted). ----
template <typename Container>
bool hasNonzeroSample(const Container& buffer, double epsilon = 1e-9) noexcept {
    for (const auto& sample : buffer) {
        const double value = static_cast<double>(sample);
        if (std::isfinite(value) && std::abs(value) > epsilon) return true;
    }
    return false;
}

// A Welch-style spectral estimate (coherence, PSD-averaged fit, etc.) needs
// at least two complete, independent segments to mean anything; a
// single-segment "average" is a periodogram wearing a Welch label (ADR-006
// correction note C2 prohibits exactly this as a width/coherence gate). DS-7
// gates on this before accepting any computed MSC/coherence value.
inline bool hasMinimumWelchSegments(std::size_t segmentCount) noexcept {
    return segmentCount >= 2;
}

// A CoherenceSummary computed over zero (or one) retained bin is as vacuous
// as a single-segment estimate: every bin fell under the silent-bin floor, so
// the reported min/max/mean/median describe an empty or single-element set
// while still printing like a spectrum-wide result. DS-7 gates on this
// alongside hasMinimumWelchSegments(), and reports retained/examined bin
// counts so a barely-passing estimate is visible rather than merely legal.
inline bool hasMinimumRetainedBins(std::size_t retainedBins) noexcept {
    return retainedBins >= 2;
}

// A log-magnitude decay regression through one point is undefined and through
// two is an exact interpolation with no residual freedom -- either would
// report a "fitted slope" that measures nothing. Eight sampled points is the
// declared minimum at which a slope through a decaying envelope is a
// regression; DS-4 gates every fit (full path and bare-network reference) on
// this before reporting a slope or a T60 derived from one.
inline bool hasMinimumFitPoints(std::size_t pointCount) noexcept {
    return pointCount >= 8;
}

} // namespace aetherfield::dsp_test

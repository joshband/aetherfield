#pragma once

// Test-only DS-A/DS-B analysis helpers.
//
// Shared by tests/DiffusionStereoPathTests.cpp (sub-task 4a of
// docs/phases/phase1-ds-integration-plan.md's Task 4) and intended for reuse
// by a future sub-task 4b's DS-4 (decay-law regression), DS-7 (interchannel
// coherence) and DS-8/DS-9 (mono compatibility, channel balance) measurements
// in the same file. This header is NOT linked into aetherfield_dsp and must
// not be included from any src/ file.
//
// Every function here is `inline` so the header stays safe to include from
// more than one test translation unit without an ODR violation, even though
// today only one executable (aetherfield_dsp_diffusion_stereo_tests) uses it.
//
// tests/SchroederAllpassTests.cpp (DS-A's own closed, independently verified
// suite) intentionally keeps its own local copies of the FFT/noise helpers
// below rather than including this header — that file predates this header
// and is not touched by this sub-task.

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
// correction note C2 prohibits exactly this as a width/coherence gate). A
// future DS-7 coherence measurement must gate on this before accepting any
// computed MSC/coherence value.
inline bool hasMinimumWelchSegments(std::size_t segmentCount) noexcept {
    return segmentCount >= 2;
}

} // namespace aetherfield::dsp_test

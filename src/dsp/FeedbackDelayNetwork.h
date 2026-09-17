#pragma once

#include "dsp/DelayLine.h"

#include <array>
#include <cstddef>
#include <vector>

namespace aetherfield::dsp {

// A fixed (time-invariant, unmodulated) Feedback Delay Network: N delay
// lines, one fixed normalized-Hadamard orthogonal feedback matrix, one
// scalar per-line gain and one bounded-real one-pole damping filter per
// line, in series inside the loop. No modulation, no diffusion, no stereo
// strategy, no runtime-adjustable Size/pre-delay. See docs/decisions.md
// ADR-002 (topology), ADR-003 (damping/denormal/lifecycle), ADR-005 (N and
// the delay-length derivation rule this class implements internally), and
// docs/phase1-s2-plan.md (this interface's design rationale).
class FeedbackDelayNetwork {
public:
    // The only line counts this class supports, per ADR-005: the Sylvester-
    // Hadamard orders in ADR-002's evaluation bracket. Any other lineCount
    // is rejected by prepare(), not clamped to the nearest supported value.
    static constexpr std::size_t kMinLineCount = 4;
    static constexpr std::size_t kMaxLineCount = 16;

    // Constructs an unprepared object: no allocation, no lines.
    FeedbackDelayNetwork() noexcept;

    // The only allocation point, off the render thread, exactly as
    // DelayLine::prepare(). Preconditions, each independently checked and
    // each causing prepare() to return false with no allocation and no
    // change to any prior valid preparation (ADR-003 (d) rule 7):
    //
    //   sampleRate:    finite, > 0.0.
    //   lineCount:     one of {4, 8, 16}.
    //   tMinSeconds,
    //   tMaxSeconds:   finite, > 0.0, tMinSeconds < tMaxSeconds. ADR-002's
    //                  "time-domain specification, not a sample count" for
    //                  the delay lengths; sample counts mᵢ are derived
    //                  here, never accepted as an argument (ADR-005 (b)).
    //   t60ZeroSeconds,
    //   t60PiSeconds:  finite, > 0.0, t60PiSeconds <= t60ZeroSeconds
    //                  (ADR-003 (a): high frequencies may never decay more
    //                  slowly than DC — this is what keeps every derived
    //                  aᵢ inside [0, 1) and therefore bounded-real).
    //                  t60PiSeconds == t60ZeroSeconds is damping bypass
    //                  (ADR-002 S2): it drives every aᵢ to exactly 0.
    //
    // On success: derives mᵢ for i in [0, lineCount) via ADR-005's rule
    // (nearest prime to a geometrically-spaced time target, tie-break to
    // the smaller prime, a strictly-increasing totality guard), asserts
    // every mᵢ is prime, strictly increasing and pairwise co-prime (checked
    // by direct gcd, never inferred from primality), derives every gᵢ and
    // aᵢ in double per ADR-003 (a)'s exact formulas, builds the normalized
    // Hadamard matrix for lineCount, prepares each internal line at exactly
    // its own mᵢ (docs/phase1-s2-plan.md "Capacity"), sets every damping
    // filter's state to 0, clears the non-finite latched flag (not its
    // count — see reset()), and leaves the object in exactly the state
    // reset() defines. Not noexcept: allocation failure propagates.
    //
    // A validation failure at any of the above returns false, performs no
    // allocation, and leaves the object exactly as it was before the call.
    bool prepare(double sampleRate,
                 std::size_t lineCount,
                 double tMinSeconds,
                 double tMaxSeconds,
                 double t60ZeroSeconds,
                 double t60PiSeconds);

    // Zeros every line's delay history (each line's own reset()), zeros
    // every damping filter's recursive state to exactly 0 (ADR-003 (d)
    // rule 3), restores every write position, and clears the non-finite
    // detector's latched flag. The detector's *counter* is deliberately NOT
    // cleared. Coefficients (gᵢ, aᵢ, the matrix, mᵢ) are configuration, not
    // state, and are never touched by reset(). Idempotent, repeatable, safe
    // before prepare() (a no-op then), safe on the render thread: noexcept,
    // allocation-free, bounded work.
    void reset() noexcept;

    // Advances the network by count samples. For each sample: reads every
    // line's current delayed output via peek(); applies each line's
    // damping filter Hᵢ(z) to its own state; scales by gᵢ (with the
    // matrix's 1/sqrt(lineCount) normalization folded in); applies the
    // fixed Hadamard matrix; adds this sample's input contribution
    // (uniform injection — see docs/phase1-s2-plan.md); pushes the result
    // into each line via push(); writes this sample's output tap (the
    // unweighted sum of the peeked values, taken before any line is
    // modified this sample). Every recursive memory (the value pushed into
    // each delay line, and each damping filter's retained state) passes
    // through the ADR-003 (b) deterministic cutoff, |x| >= 1e-20 ? x : 0,
    // before being stored, unless that value is non-finite (see below).
    //
    // Non-finite input samples are substituted with 0 for that sample's
    // injection and counted (ADR-003 (c)'s table). A non-finite value
    // arising internally (in a line's write-value or a damping filter's
    // state) is never clipped, substituted, or otherwise altered — it is
    // counted and latched, and stored as-is. The network's future output is
    // not meaningful after this; only a subsequent reset() is a documented
    // recovery path.
    //
    // count == 0 is always a safe no-op. input/output are caller-owned;
    // output may be null only when count == 0.
    //
    // Precondition for count > 0: prepare() must have returned true at
    // least once, matching DelayLine's identical contract.
    //
    // noexcept, allocation-free, lock-free, branch count independent of
    // sample values, except for the data-independent branchless cutoff
    // selects ADR-003 (b) itself specifies.
    void process(const float* input, float* output, std::size_t count) noexcept;

    // Diagnostic accessors, per ADR-003 (c)'s mandatory non-finite
    // handling. Both noexcept, allocation-free, safe on the render thread.
    std::size_t nonFiniteCount() const noexcept;
    bool nonFiniteLatched() const noexcept;

    // The orthogonality residual ||AᵀA - I||∞, computed once at prepare()
    // against the realized normalized matrix and retained as a diagnostic
    // (ADR-003 (d) rule 4). This is exactly NS-1's bound.
    float orthogonalityResidual() const noexcept;

    // Diagnostic accessors for the derived per-line configuration.
    // **Added during implementation, not present in the original
    // phase1-s2-plan.md interface** (see docs/agent-log.md): NS-2, NS-3 and
    // NS-5 require checking the stored per-line coefficients directly
    // (e.g. NS-2's exact two-point bounded-realness formula depends on the
    // stored aᵢ), which black-box signal behavior alone cannot expose.
    // Precondition: line < lineCount(). This is a test/diagnostic contract,
    // not a runtime-checked sanitizer, matching this codebase's existing
    // style (e.g. DelayLine::process()'s caller contract). All noexcept,
    // allocation-free.
    std::size_t lineCount() const noexcept;
    std::size_t delaySamples(std::size_t line) const noexcept;  // mᵢ
    float dampingCoefficient(std::size_t line) const noexcept;  // aᵢ
    float foldedLineGain(std::size_t line) const noexcept;      // gᵢ / sqrt(lineCount())

private:
    void buildNormalizedHadamard();
    void flagNonFinite() noexcept; // increments nonFiniteCount_ (saturating) and latches the flag

    std::size_t lineCount_ = 0;
    std::vector<DelayLine> lines_;              // size == lineCount_ once prepared
    std::vector<std::size_t> delaySamples_;     // mᵢ, one per line
    std::vector<float> dampingState_;           // wᵢ[n-1], one float per line
    std::vector<float> dampingCoeffA_;          // aᵢ, one per line
    std::vector<float> dampingCoeffOneMinusA_;  // (1 - aᵢ), one per line
    std::vector<float> lineGain_;               // gᵢ / sqrt(lineCount_), one per line
    std::vector<float> matrix_;                 // lineCount_ x lineCount_, row-major normalized A (diagnostic only)
    std::array<float, kMaxLineCount> scratch_{}; // per-sample working vector; never resized on the render thread
    std::size_t nonFiniteCount_ = 0;
    bool nonFiniteLatched_ = false;
    float orthogonalityResidual_ = 0.0F;
};

} // namespace aetherfield::dsp

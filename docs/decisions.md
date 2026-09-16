# Architectural decisions

## ADR-001 — Portable C++ host loop; AUv3 framework decision deferred

**Status:** Accepted

### Context

Aetherfield begins greenfield as an iOS/iPadOS AUv3 audio effect. Phase 0 must prove CODE → BUILD → TEST → RENDER → INSPECT without implementing reverb or requiring an Audio Unit host. Sol reviewed this consequential boundary; Astra accepted it. Terra evaluated feasibility and implements the approved bootstrap; Luna independently verifies it.

### Alternatives

These are project-specific engineering judgments, not claims that alternative tools cannot work.

| Criterion | CMake core + future native Xcode | Xcode-only | Swift Package Manager + Xcode | JUCE + CMake/Xcode |
|---|---|---|---|---|
| AUv3 support | Later native extension; none today | Native extension targets | Still needs extension/app packaging | Framework supports AUv3 |
| iOS/iPadOS development | Later native Apple workflow | Direct Apple workflow | Good Swift integration; still Apple project work | Supported mobile framework workflow |
| C++ DSP isolation | Explicit independent library | Possible, but Apple project owns host loop | Possible C++ target; interop/module boundary | Possible separate core; wrapper adds framework |
| Offline testability | Small CLI targets and CTest | Possible command-line test targets | Possible executable/test targets | Good, but no framework needed for this loop |
| Build reproducibility | Text build graph; explicit toolchain; no downloads | Schemes/project/SDK settings need control | Package/toolchain control plus Apple packaging | Framework version/license and generator require control |
| Agentic development | Small reviewable files and direct CLI commands | More project-file maintenance early | Simple manifest, additional mixed-language constraints | Broad framework API and configuration surface |
| Dependency burden | CMake, compiler, build tool; standard C++ library | Apple tooling | Swift tooling and later Xcode | Additional third-party framework and licensing choice |
| Maintainability | Small current graph; later cross-project drift risk | One Apple-centric graph; less portable host workflow | Attractive if Swift app boundary benefits later | Attractive if wider platform/format requirements justify it |

### Decision

Use C++20 and CMake for a portable static DSP library, a CTest-registered test executable and an offline WAV renderer. Require no third-party DSP, test or audio-file library. Use a concrete stateless gain function as the only current processor.

Plan an AUv3 integration layer around the same core when integration is authorized. Its framework remains **DEFERRED**: native Apple APIs and JUCE are the serious shortlist, to be decided in an integration-focused ADR. Do not generate an Apple project now. CMake-only AUv3 packaging is not selected. Swift Package Manager, JUCE and Xcode-only are **REJECTED for this bootstrap**, not prohibited forever. UI technology and production parameter transport remain **DEFERRED**.

Following the owner's JUCE/CLAP question, Sol reviewed the distinction explicitly. [CLAP](https://github.com/free-audio/clap) defines a host/plugin ABI and extensions; it is a plugin format, not an alternative AUv3 framework. CLAP is **NOT PLANNED** for the initial delivery; a later desktop CLAP requirement would justify reconsideration. [iPlug2](https://github.com/iPlug2/iPlug2) is another credible plugin framework, but a deeper evaluation is deferred until integration needs justify expanding the shortlist.

### Rationale

The current task needs a portable compiler loop, not a plugin framework. Standard C++ with an explicit library target keeps allocation and ownership visible and supports offline experimentation. Either native Apple APIs or JUCE can later wrap the core without becoming a DSP dependency.

Native integration offers direct lifecycle control and fewer dependencies, but requires more Apple-specific glue. JUCE can reduce wrapper, parameter/state and UI work and help if other formats become requirements; it introduces framework conventions, licensing decisions and a larger build surface. Neither is proven preferable for the eventual product by this trivial host loop.

This separates host-loop proof from future deployment proof. CMake describes the build reproducibly, but does not guarantee bit-identical binaries across compilers or environments.

### Evidence

- [Apple AUAudioUnit documentation](https://developer.apple.com/documentation/AudioToolbox/AUAudioUnit) and [Audio Unit extension guide](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html) describe the native extension and render lifecycle.
- [CMake toolchains manual](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html) documents explicit toolchains and Apple cross-compilation. The current build validates only the host targets.
- [Swift C++ interoperability](https://www.swift.org/documentation/cxx-interop/) documents C++/Swift package and interoperability configuration; Phase 0 needs neither Swift nor a bridging layer.
- [JUCE repository](https://github.com/juce-framework/JUCE) and [CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md) establish AUv3 support and its Xcode generator requirement. Framework capabilities exceed this milestone's needs.
- Local environment: CMake 4.2.2; Apple Command Line Tools clang 21.0.0, arm64. `DEVELOPER_DIR=/Library/Developer/CommandLineTools` selects a working host compiler without changing system settings. The globally selected Xcode installation reports an unaccepted license; native iOS validation is not claimed.
- Actual acceptance commands and results are recorded in [testing.md](testing.md).

### Consequences

DSP remains Apple-independent; tests/rendering work without an AUv3 host. There is no smoothing, state serialization, host parameter tree or concurrent parameter transport. A fixed scalar gain is only a bootstrap fixture input.

Later integration must prevent CMake/Xcode source and build-setting drift. C++20 compatibility, deployment target, architecture slices, device CPU budget, signing and real host behavior require actual validation when Apple integration starts. No license or product identifiers are invented now.

### Revisit When

Native AUv3 integration begins, another platform/format is approved, shared-source maintenance becomes brittle, or evidence shows SPM/JUCE reduces total integration cost enough to justify its constraints.

## ADR-002 — Late reverb network: FDN with an orthogonal feedback matrix and per-line damping

**Status:** Accepted

This ADR decides a topology and the stability conditions that govern it. It authorizes no implementation. No reverb code, measurement, or listening evidence exists in this repository; every quantitative claim below is either literature-derived or arithmetic on stated assumptions, and is labelled as such.

### Context

Roadmap Phase 1 row 1 requires selecting the late-network topology before any reverb component is written. Charter §11–12 propose an FDN explicitly as a *hypothesis, not a requirement*, and instruct the DSP Architect to compare it against credible alternatives and select the simplest architecture capable of the sonic target. §43 prefers mathematically understandable and stable DSP over clever DSP, and a smaller architecture over more features. §45 and §47 define the target as a convincing, extremely spacious, smooth, slowly evolving ambient field from the smallest coherent architecture.

The decision is consequential because the late network fixes the project's stability argument, its per-sample cost floor on mobile hardware, and what later features (frequency-dependent decay, modulation, stereo extraction, eventually Freeze) can be attached without redesign. Phase 0 implements only `y[n] = gain * x[n]`; dsp-design.md records the reverb topology as undecided. Sol decides; Astra accepts; Terra implements when separately authorized; Luna verifies.

Astra flagged one requirement explicitly: this ADR must not let a fixed-network stability proof be read as covering a later modulated network. That distinction is made in the Rationale and is binding.

### Alternatives

These are project-specific engineering judgments against Aetherfield's stated sonic target, not claims that the rejected topologies cannot make good reverbs. All four are proven designs in shipped products.

| Criterion | (a) FDN, orthogonal feedback matrix | (b) Schroeder/Moorer parallel combs + series allpasses | (c) Allpass diffusion tank (plate-style) | (d) Digital waveguide network (multi-junction) |
|---|---|---|---|---|
| Echo-density growth | Dense matrix redistributes each impulse to all N lines per circulation; echo count grows multiplicatively per pass | N parallel comb pulse trains; density grows linearly, so series allpasses are required to thicken it | Nested/series allpasses build density quickly and cheaply | Depends on junction count and branch topology; equals (a) in the single-junction case |
| Tail smoothness / coloration | Modal density set by total delay; residual coloration at small N is a known, actively researched problem | Comb peaks and long allpasses are the classic source of metallic, fluttery coloration | Strong fixed character; per-band control of that character is limited | Least practical design guidance; no smoothness advantage demonstrated over (a) |
| Frequency-dependent decay | Per-line attenuation plus per-line damping filter gives one uniform, directly specified decay law across all modes | Per-comb gain sets per-comb decay; the allpass chain adds decay that is not independently specified | One tank gain and typically one damping filter; decay is a property of the whole loop, not per mode | Expressible through branch losses, but bookkeeping grows with junctions |
| Computational cost | N delay lines plus one matrix; structured orthogonal matrices cost O(N) or O(N log N) additions | Cheapest of the four for a given line count | Very cheap per unit of density | No cheaper than (a) for equal mixing; more state to manage |
| Parameter / modulation flexibility | Delay lengths, per-line gains, damping and the matrix itself are separable; matrix modulation has a published stability result | Modulating combs shifts the pitch of their own ringing; limited headroom | Modulated input allpasses are the classic approach; topology resists a clean Size parameter | Modulating scattering junctions while preserving passivity is awkward |
| Available stability theory | Strongest: explicit norm and eigenvalue criteria, lossless-prototype characterization, and delay-independent sufficient conditions | Per-element conditions are simple; a global bound over a composed network is harder to state | Bound is on a product of loop gains; less convenient once elements are modulated | Energy/passivity arguments exist but are less directly testable in code |
| Stereo extraction | N internal signals are directly available as decorrelated tap sources (strategy itself deferred) | Requires a dedicated decorrelation stage | Figure-of-eight structure offers natural left/right taps | Available, but tied to the chosen topology |
| Implementation risk | Moderate: N lines, one matrix, one damping filter per line — small and auditable | Low risk, but a recognized ceiling on smooth ambient density | Low-to-moderate, but it is a fixed recipe rather than a parameterized architecture | Highest: most design freedom, least off-the-shelf guidance, no proven gain here |

### Decision

**Select (a).** Aetherfield's late reverb network is a Feedback Delay Network of N mutually coupled delay lines, feeding back through a **single real orthogonal matrix** `A` (`AᵀA = I`), with **one scalar attenuation `gᵢ` and one low-order bounded-real damping filter `Hᵢ(z)` placed in series with each delay line, inside the feedback loop, between the delay output and the matrix**.

Within that decision:

- **Feedback matrix family.** A normalized Hadamard matrix is the baseline; a Householder reflection `A = I − (2/N)·11ᵀ` is the named cheaper fallback. Both are orthogonal. The reason for preferring Hadamard at the baseline line count is mixing uniformity, argued in the Rationale.
- **Line count.** `N = 8` is the baseline to be evaluated, bracketed by `N = 4` (cost floor) and `N = 16` (density ceiling). The final N is **DEFERRED** to measured evidence and will be recorded in a follow-up ADR. This ADR fixes the structure, not the number.
- **Decay law.** Homogeneous (Jot) decay: a single per-sample gain `γ` derived from the target RT60, applied per line as `gᵢ = γ^mᵢ`, so every delay line decays at the same rate regardless of its length.
- **Delay lengths.** Mutually co-prime, chosen from a time-domain specification rather than a sample count. Specific values are **DEFERRED**.

**REJECTED for the late network:** (b) Schroeder/Moorer comb+allpass, (c) the allpass diffusion tank, (d) a general multi-junction digital waveguide network. None is prohibited elsewhere: short allpass sections remain the leading candidate for the *input and output diffusion* stages, which this ADR does not decide, and Rocchesso and Smith's result that an orthogonal-matrix FDN is isomorphic to a single-scattering-junction waveguide network means (d) is not a discarded capability but an equivalent description of the accepted design.

**Explicit scope exclusions.** This ADR does **not** decide or imply: pitch or spectral processing (§18); Freeze/Infinite (§17) — the decay clamp below deliberately excludes unity loop gain, so Freeze requires its own energy-preserving design; Bloom (§16); Texture (§15); stereo decorrelation strategy or output tap design; input and output diffusion topology; delay-line interpolation method; whether modulation exists at all, and if so its target, rate or depth; parameter names, units, ranges, mappings, smoothing semantics or automation behavior (Phase 1 row 3); UI; multi-engine architecture (§39); the AUv3 framework choice, which remains **DEFERRED** per ADR-001. It also does not decide file layout, interfaces, class structure or CMake targets; Phase 1 row 4 assigns that translation to Terra under separate authorization.

### Rationale

#### Why an FDN

The three stated requirements — smooth high density, a long decay whose rate is *specified* rather than emergent, and per-frequency control of that rate — are exactly the three quantities an FDN separates. Delay lengths set modal density, per-line gains set decay, and per-line filters set how decay varies with frequency; each can be changed without disturbing the others. Jot and Chaigne's contribution was precisely this: give every delay line its own damping filter designed so all lines decay at the same rate at every frequency, which turns reverberation time into a directly specified parameter of a network whose structure can be chosen independently.

The rejected alternatives each break one of those separations. In parallel combs, a comb's decay and its resonant peaks are the same parameter; Moorer's own improvement to the Schroeder design was to put a lowpass filter in the comb loop, which is the same insight applied to a structure that couples decay to coloration. Series allpasses are "colorless" only in the steady-state-gain sense and only when very short: Smith's treatment notes that allpass filters sound colorless only when their delays are under roughly 10 ms, and that longer allpass impulse responses sound similar to feedback comb filters. That is a direct argument against building a long ambient tail out of allpasses, and it is the reason (c) is rejected as the *late* network while remaining the preferred candidate for short diffusion stages. For (d), Rocchesso and Smith showed the single-junction waveguide network and the orthogonal FDN are the same system; adding junctions adds scattering bookkeeping and design freedom for which there is no evidence of need at this stage, which §43 and §46 both rule out.

Echo density is the other decisive axis. A dense feedback matrix sends every arriving impulse into all N lines on every circulation, so the echo count grows multiplicatively per pass; N parallel combs produce N periodic pulse trains whose density grows only linearly. Schroeder's own criteria for a convincing space — roughly 1000 echoes per second, and roughly 0.15 modes per Hz for a one-second reverberation time — are reached quickly by the former and slowly by the latter. Those figures were derived for indistinguishability from a real room, which is a different goal from Aetherfield's; they are treated here as a floor, not a target.

#### Why orthogonal, and why Hadamard at the baseline

Orthogonality is what makes the stability argument independent of the delay lengths, which is the property that lets delay lengths remain a free sonic parameter. Smith's condition for FDN losslessness is that the feedback matrix have unit-modulus eigenvalues and linearly independent eigenvectors; Schlecht and Habets generalized this to the *unilossless* characterization — a matrix is lossless for every possible choice of delays exactly when it is diagonally similar to a unitary matrix, and any matrix satisfying `AAᴴ = I` qualifies. A real orthogonal matrix satisfies this, so the lossless prototype is lossless for any delay set the design later chooses.

Between the two structured orthogonal candidates, the argument is mixing uniformity versus arithmetic cost. The Householder reflection `A = I − (2/N)·11ᵀ` is remarkably cheap: it needs only `2N − 1` additions, with no multiplies at all when N is a power of two, and for N ≠ 2 all its entries are nonzero so every line couples to every other. But its diagonal entries are `1 − 2/N` and its off-diagonal entries are `−2/N`. Only at N = 4 are those equal in magnitude; as N grows the diagonal dominates and the network tends toward decoupled parallel comb filters — that is, toward exactly the topology rejected above. At N = 8 a Householder line retains 0.75 of its own signal and passes 0.25 to each neighbour, which is a real weakening of the mixing that motivated choosing an FDN. A normalized Hadamard matrix at N = 8 has every entry of equal magnitude `1/√8 ≈ 0.354`, is orthogonal, and costs `N log₂N = 24` additions plus a single uniform scaling that can be absorbed into the per-line attenuation gains. Paying roughly nine extra additions per sample for uniform mixing is the correct trade against §43's preference for the perceptually better of two equally understandable options. Householder is retained as the named fallback because at N = 4 it is both balanced and the cheapest option available, which matters if the mobile CPU budget turns out tighter than expected.

Modal density arithmetic supports N = 8 as the baseline. An FDN's pole count equals the sum of its delay lengths, so modal density is `Σmᵢ / f_s` modes per Hz. At 48 kHz with eight lines averaging 50 ms, `Σmᵢ ≈ 19200` samples gives about 0.4 modes per Hz — comfortably above Schroeder's 0.15 floor, with room to shorten lines for smaller spaces. N = 4 at the same average line length gives about 0.2 modes per Hz, which clears the floor only barely; coloration in four-line FDNs is a documented and still-open research problem, which is direct evidence that the cost floor is also the quality floor.

#### What is mathematically provable about the fixed network

Let `D(z) = diag(z^{−mᵢ})` be the delay bank, `Γ = diag(gᵢ)` the per-line attenuation, and `Hᵢ(z)` the per-line damping filters. Four statements are provable for the **time-invariant, unmodulated** network and are stated as testable conditions:

1. **Lossless prototype.** With `Γ = I` and damping bypassed, an orthogonal `A` makes the network lossless: all poles lie on the unit circle, for *any* set of delay lengths. *Test:* independently compute `‖AᵀA − I‖∞` against a stated tolerance, and confirm a long zero-input impulse response neither grows nor decays beyond a stated bound.
2. **Strict stability, independent of delay lengths.** A pure delay preserves ℓ2 norm, and an orthogonal matrix has spectral norm exactly 1. The loop map — delay bank, then `Γ`, then `A` — therefore has ℓ2 gain at most `‖ΓA‖₂ ≤ ‖Γ‖₂‖A‖₂ = maxᵢ|gᵢ| < 1`. By the small-gain argument the closed loop is a strict contraction and all poles lie strictly inside the unit circle, for every choice of `mᵢ`. This is Smith's `‖A‖₂ < 1` criterion applied to the combined matrix `ΓA`. *Test:* compute the largest singular value of `ΓA` and record the margin `1 − maxᵢ|gᵢ|`.
3. **Damping placement and the stability margin.** Replacing `gᵢ` by `Gᵢ(z) = gᵢHᵢ(z)` preserves the bound **if and only if the damping filters are bounded-real** — that is, each `Hᵢ` is stable with `sup_ω |Hᵢ(e^{jω})| ≤ 1`. Then the loop gain at every frequency is at most `maxᵢ gᵢ < 1` and the margin is unchanged. This is a sufficient, not necessary, condition: it deliberately gives up some achievable brightness in exchange for a bound that holds at every frequency. Its practical importance is negative — a shelving or one-pole filter normalized for unity gain at DC that overshoots anywhere in the band destroys the guarantee, so bounded-realness must be **measured on a dense frequency grid, not assumed from the filter's intended shape**. Placing damping inside the loop in series with each line, rather than once on the output, is what makes this per-line bound available at all.
4. **RT60 mapping.** Homogeneous decay sets the per-sample gain from `γ_dB = −60 / (f_s · T60)`, so `gᵢ = γ^mᵢ = 10^(−3·mᵢ / (f_s·T60))`. Because `gᵢ` depends on that line's own length, all lines decay at the same rate and no single line rings on after the others. *Test:* compare the measured energy decay curve against the predicted `10^(−3n / (f_s·T60))` line over several T60 values at each supported sample rate. Note the direct consequence that **RT60 → ∞ drives `γ` → 1 and the margin to zero**, so a hard clamp `γ ≤ γ_max < 1` is mandatory and the maximum attainable decay is a bounded, documented quantity.

#### What remains a hypothesis

The following are **not** established by the above and must not be described as decided:

- **That the result sounds smooth.** Stability, uniform decay and modal-density counts are not perceptual claims. Residual coloration in FDNs with few lines is an open research topic; whether N = 8 with the chosen delays sounds like a convincing ambient field, or metallic, is an empirical question. §20's "it seems stable" prohibition applies symmetrically here: "the math is clean, so it will sound good" is the same error.
- **That N = 8 is correct.** It is a reasoned starting point with a stated bracket.
- **That anything above survives modulation.** This is the binding distinction Astra required. Every bound in the previous section assumes a **time-invariant** system: fixed delay lengths, fixed matrix, fixed coefficients. Modulating the *delay lengths* — the obvious way to get charter §13's "slow evolution" — makes the loop a time-varying operator with fractional-delay interpolation in it; interpolation is not a norm-preserving operation and a time-varying delay can inject energy, so no part of the small-gain argument above transfers. The one published exception is narrow and specific: Schlecht and Habets proved that modulating the *feedback matrix* along a path that stays unitary is guaranteed stable, and they present that result explicitly in contrast to prior time-varying allpass FDN designs that carried no such guarantee. That exception covers matrix modulation only. Consequently: **this ADR authorizes no modulation of any kind.** If modulation is later proposed, its stability must be established independently — either by adopting unitary matrix modulation and inheriting that proof, or by worst-case empirical measurement under testing.md's Modulation experiment gate, which already requires a retained fixed baseline and explicit approval before enabling.

#### Numerical safety (charter §20)

- **Denormals.** A network whose per-line gains are strictly below 1 has a tail that decays toward, and then persists in, the denormal range — in exact arithmetic it never reaches zero in finite time. This is not a defect of the topology but an unavoidable consequence of any stable feedback structure, so it must be handled rather than hoped away. The platform makes this sharper: on ARM64 the flush-to-zero control (`FPCR.FZ`) is per-thread and is **not inherited by child threads**, unlike x86's `MXCSR`, so an AUv3 render thread created by the host may be running without flush-to-zero even if the app sets it elsewhere. The design must therefore remain correct with denormals present, and a mitigation must be chosen on measurement. Three candidates are named, none selected here: setting `FPCR.FZ` on entry to the render callback; a deterministic state-level cutoff below a stated threshold; or additive anti-denormal noise, which carries a permanent noise floor and is the least attractive for a quiet ambient tail. The choice belongs to the numerical-bounds ADR (Phase 1 row 2) and must be backed by a measured cost difference, not an assumption.
- **NaN/Inf.** A feedback loop is permanent memory for any non-finite value, and a dense feedback matrix is the worst case: one NaN in one delay line reaches all N lines within a single matrix application and never leaves. ARM's default-NaN mode does not help — it standardizes the value produced, not its propagation. The only recovery is a full state reset. This imposes three constraints on the later design: input conditioning must guarantee that only finite samples enter the network; every parameter must be validated and clamped at the control boundary *before* it becomes a coefficient, because the render thread must not be the place where an invalid value is discovered; and a documented, deterministic reset path must exist. An output-side non-finite detector is a diagnostic, not a fix, and per testing.md any such intervention must be *reported*, never hidden by clipping or by regenerating reference data.
- **Extreme and combined parameter values.** The math above already identifies the dangerous edges. Maximum decay drives the margin to zero and requires the `γ_max` clamp. Minimum decay drives `γ^mᵢ` toward underflow on the longest lines, which must be defined behavior rather than a logarithm or division edge case. Minimum size must keep delay lengths at least 1 sample and mutually distinct, since coincident or very short lines collapse modal density and reintroduce audible comb tone. Maximum damping is the safe direction (the tail vanishes); *minimum* damping is the dangerous one, because it leaves the entire stability margin resting on `gᵢ` alone. The worst case is therefore the simultaneous combination of maximum decay, minimum damping and full-scale input, and it must be an explicit named stress case rather than an interpolation between single-parameter tests.
- **Sample-rate changes.** `γ` is a function of `f_s`; a coefficient cached across a rate change silently changes RT60, and at a lower rate the same `γ` yields a longer tail. Delay lengths must likewise be re-derived from their time-domain specification so that Size is rate-independent. Storage must be reserved for the longest supported time at the **highest** supported rate. A rate change invalidates all retained state, which is at the wrong rate, so it implies a full reset.
- **Initialization and reset.** All delay memory is zeroed. The zero state is an equilibrium — zero state with zero input yields zero output — so "silence in, silence out after reset" is a provable and directly testable property, and a useful canary for state that was missed.

#### Realtime safety (charter §19)

These are binding constraints on the future implementation, stated here because the topology is what makes them achievable. Nothing below is an implementation.

- **No allocation on the render thread.** N, the maximum delay in samples at the maximum supported rate, and all delay storage are fixed during preparation, off the render thread. It follows that Size and pre-delay must be expressed as read offsets within already-reserved capacity, never as reallocation. The FDN supports this cleanly because its memory requirement is a function of the *maximum* configuration, not the current one.
- **Bounded, data-independent work.** Per-sample cost is O(N) for the lines and filters plus the fixed matrix operation. It does not depend on parameter values, signal content, or decay length, so there are no unbounded loops and no data-dependent branching in the audio path. This is a property to be preserved by construction and checkable by review.
- **No locks or blocking synchronization.** Parameter transport must be lock-free per §19; its semantics are deferred to Phase 1 row 3, but this ADR constrains them: the render thread must never wait on the control thread, and coefficient updates must be readable without blocking.
- **No file I/O, logging or UI.** Diagnostics such as a non-finite detector may set flags to be read elsewhere; they must not log, allocate or format strings on the render thread.
- **Realtime-safe state transitions.** Reset is a bounded clear of already-owned storage. Rate and size changes are preparation-time operations, never render-thread operations.
- **No hidden-allocation abstractions.** The DSP core stays standard C++ with concrete, auditable storage, per §19 and ADR-001's boundary.

#### Bounded later prototype and evaluation plan

This plan names the smallest next DSP skeleton milestone and the evidence that would close the gap between the math above and measurement. **It authorizes nothing.** Phase 1 rows 2–4 — the numerical-bounds ADR, the parameter-semantics ADR, and Terra's task plan — all precede any code. The milestones map onto testing.md's existing PLANNED validation gates rather than inventing new ones.

**S1 — Delay and lifecycle skeleton (testing.md gate 1).** One fixed-length delay line, no feedback, no matrix, with explicit preparation, reset and sample-rate lifecycle. Evidence: exact impulse position and wraparound; invalid preparation rejected; repeatable reset; all buffer extents respected; zero-frame calls safe; allocation occurring only during preparation. This claims nothing about reverb.

**S2 — Fixed late network (testing.md gate 2).** N fixed delay lines, one fixed orthogonal matrix, per-line gains from the RT60 formula, damping filters initially bypassable. No modulation, no diffusion, no stereo strategy, no parameters. Required evidence, each item tied to a claim above:

1. Independently computed `‖AᵀA − I‖∞` within a stated tolerance, and the largest singular value of `ΓA`, with the margin `1 − maxᵢ|gᵢ|` recorded.
2. Bounded-real check on each damping filter: `sup|Hᵢ(e^{jω})| ≤ 1` over a dense frequency grid, not inferred from the filter's design intent.
3. Lossless-prototype check: with `Γ = I` and damping bypassed, a long zero-input impulse response neither grows nor decays beyond a stated bound.
4. Measured energy decay curve against the predicted `10^(−3n / (f_s·T60))` law, for at least three RT60 values at each supported sample rate, within a stated tolerance.
5. Long zero-input decay from a full-scale impulse: no non-finite value, no growth, plus recorded time-to-denormal and a measured cost difference with and without flush-to-zero.
6. Worst-case stress: maximum decay × minimum damping × full-scale input, across several block-size partitions, reporting peak, overshoot and any safety intervention.
7. Double-precision reference comparison for the same fixture with a justified tolerance.
8. Determinism: repeated renders identical byte-for-byte, per testing.md.

**S2 exit condition.** The numbers above exist and are recorded. Sonic quality is explicitly **not** claimed at S2: smoothness, ringing and width cannot be judged before the diffusion and stereo stages that this ADR does not decide. Any listening note taken at S2 is an observation, not an acceptance.

### Evidence

No Aetherfield measurement supports this decision; the repository has measured nothing about reverb (testing.md). The following are the literature and platform sources the argument rests on.

- [Schroeder, "Natural Sounding Artificial Reverberation," *JAES* 10(3), July 1962, pp. 219–223](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Schroeder_1962.pdf) — the parallel-comb and series-allpass designs, and the echo-density and mode-density criteria used above as floors.
- [Moorer, "About This Reverberation Business," *Computer Music Journal* 3(2), 1979, pp. 13–28](http://articles.ircam.fr/textes/Moorer78b/) — lowpass filtering inside the comb loop as the route to room-like decay.
- [Stautner and Puckette, "Designing Multichannel Reverberators," *Computer Music Journal*, 1982, pp. 52–65](https://msp.ucsd.edu/publications.html) — the original four-line FDN with a feedback matrix. Cited as listed on the author's own publication page; other sources give a different volume number.
- [Jot and Chaigne, "Digital Delay Networks for Designing Artificial Reverberators," AES 90th Convention, February 1991](https://aes2.org/publications/elibrary-page/?id=5663) — per-line damping filters designed for a uniform decay rate at all frequencies, and the extension to recursive delay networks with a unitary feedback matrix.
- [Smith, "A New Approach to Digital Reverberation Using Closed Waveguide Networks," ICMC 1985, pp. 47–53](https://quod.lib.umich.edu/i/icmc/bbp2372.1985.009/6/--new-approach-to-digital-reverberation-using-waveguide) — digital waveguide networks as reverberators.
- [Rocchesso and Smith, "Circulant and Elliptic Feedback Delay Networks for Artificial Reverberation," *IEEE Trans. Speech and Audio Processing* 5(1), 1997, pp. 51–63](https://citeseerx.ist.psu.edu/document?repid=rep1&type=pdf&doi=e69cb5643d81264ed96201e31cef7a7d375408bb) — the orthogonal-matrix FDN is isomorphic to a single-scattering-junction waveguide network.
- [Dattorro, "Effect Design, Part 1: Reverberator and Other Filters," *JAES* 45(9), September 1997](https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf) and [Part 2: Delay-Line Modulation and Chorus, *JAES* 45(10), October 1997, pp. 764–788](https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf) — the allpass diffusion tank evaluated as alternative (c), and delay-line modulation practice.
- Smith, *Physical Audio Signal Processing* (CCRMA): [FDN stability](https://ccrma.stanford.edu/~jos/pasp/FDN_Stability.html) — stability when the feedback matrix reduces the ℓ2 norm of its input, `‖A‖₂ < 1`, the `A = ΓQ` parametrization, and losslessness as unit-modulus eigenvalues with linearly independent eigenvectors; [Householder feedback matrix](https://ccrma.stanford.edu/~jos/pasp/Householder_Feedback_Matrix.html) — the `I − (2/N)·11ᵀ` form, `2N − 1` additions, no multiplies at power-of-two N, all entries nonzero for N ≠ 2, and the diagonal dominance that makes large-N Householder approach decoupled combs; [Schroeder reverberators](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Reverberators.html) — allpass sections sound colorless only below roughly 10 ms, and longer ones resemble feedback combs; [achieving desired reverberation times](https://ccrma.stanford.edu/~jos/pasp/Achieving_Desired_Reverberation_Times.html) — the −60 dB criterion and per-sample attenuation replaced by a lowpass with gain not exceeding 1 at any frequency; [digital waveguide reverberators](https://ccrma.stanford.edu/~jos/pasp/Digital_Waveguide_Reverberators.html) — FDNs as single-scattering-junction DWNs.
- [Schlecht and Habets, "Time-varying feedback matrices in feedback delay networks and their application in artificial reverberation," *JASA* 138(3), September 2015, p. 1389](https://pubs.aip.org/asa/jasa/article-abstract/138/3/1389/680169/Time-varying-feedback-matrices-in-feedback-delay) — unitary feedback-matrix modulation is guaranteed stable, stated explicitly in contrast to prior time-varying allpass FDNs. This is the only modulated-stability result relied on, and it covers matrix modulation only.
- Schlecht and Habets, "On Lossless Feedback Delay Networks," *IEEE Trans. Signal Processing* 65(6), March 2017, pp. 1554–1564 — the unilossless characterization: lossless for every choice of delays exactly when diagonally similar to a unitary matrix.
- [Dal Santo, Prawda, Schlecht and Välimäki, "Optimizing tiny colorless feedback delay networks," arXiv:2402.11216](https://arxiv.org/abs/2402.11216) — homogeneous attenuation `γ_dB = −60/(f_s·T60)`, the unilossless and paraunitary conditions, and direct evidence that spectral coloration grows more noticeable as the number of delay lines falls, which is the basis for not selecting N = 4 as the baseline.
- [ARM: when to use flush-to-zero mode](https://developer.arm.com/documentation/dui0801/a/Advanced-SIMD-and-Floating-point-Programming/When-to-use-flush-to-zero-mode) and [flush-to-zero behavior](https://developer.arm.com/documentation/ddi0406/c/Application-Level-Architecture/Application-Level-Programmers--Model/Floating-point-data-types-and-arithmetic/Flush-to-zero) — the `FPCR.FZ` control. [A field report of an ARM64 audio thread running without it](https://github.com/mixxxdj/mixxx/issues/16126) documents that `FPCR` is per-thread and not inherited by child threads on ARM64, unlike x86 `MXCSR`, and that denormals in feedback effects caused underruns. Treated as a hazard to measure, not a measured Aetherfield result.

### Consequences

Aetherfield now has a late-network architecture whose stability rests on two conditions a reviewer can check independently — orthogonality of one matrix, and a per-line loop gain strictly below 1 at every frequency — and a decay law that is specified rather than tuned. Delay lengths become a free sonic parameter, because the stability bound does not depend on them. Frequency-dependent decay has a defined home (inside the loop, per line) before any filter is designed. The cost floor is bounded and predictable: N delay lines, N low-order filters, and a matrix costing O(N log N) additions, with no data-dependent work.

The decision also creates obligations. A hard clamp on maximum decay is now mandatory, which means the maximum RT60 is a bounded product quantity and Freeze cannot be delivered by pushing this parameter to its limit — §17 will need an energy-preserving design of its own. Damping filters must be verified bounded-real by measurement, adding a design constraint that rules out otherwise reasonable filter normalizations. Denormal handling must be resolved with a measured decision because the platform does not guarantee flush-to-zero on the render thread. Parameter validation moves to the control boundary, because a dense feedback matrix makes NaN containment impossible after the fact.

Several things stay deliberately open and must not drift into being treated as settled: N, the delay lengths, the damping filter order, the diffusion stages, stereo extraction, interpolation, and whether modulation exists at all. The modulation exclusion is the most load-bearing: the fixed-network proof does not extend to time-varying delays, and charter §13's "slow evolution" therefore remains an unfunded requirement whose stability argument has not been made.

### Revisit When

S2 measurement shows the fixed network cannot reach acceptable modal or echo density within the mobile CPU budget at the baseline line count; listening at a later milestone identifies coloration that per-line damping and delay-length choice cannot remove, implicating the matrix family or N; a Freeze or Bloom requirement demands loop behavior that the `γ_max` clamp forbids; modulation is proposed, at which point its stability must be argued separately and may favor unitary matrix modulation over delay modulation, changing the matrix decision; bounded-real damping proves too restrictive to reach the intended tone, requiring a less conservative but still provable stability criterion; or measured evidence shows a diffusion-based or waveguide topology reaches the same tail at materially lower cost.

## ADR-003 — Damping filter, denormal mitigation, numerical bounds, and full-network lifecycle

**Status:** Accepted

This ADR sharpens the items ADR-002 left open. It decides no topology, no line count `N`, no delay lengths, no parameter names or ranges, and no modulation. It authorizes no implementation and no source file. Every quantitative statement below is either a proof on stated assumptions, arithmetic on clearly labelled *assumed* values, or a literature/platform citation — never a measurement, because Aetherfield has still measured nothing about reverb (testing.md).

**No sonic claim is made anywhere in this ADR.** Bounded-realness, a stability margin, a denormal strategy and a reset contract are numerical properties. They do not establish that the network sounds smooth, spacious or uncoloured. ADR-002's warning applies unchanged and symmetrically: "the math is clean, so it will sound good" is the same error as "it seems stable."

### Context

Roadmap Phase 1 row 2 requires decay/damping, modulation and numerical bounds, initialization/reset and the sample-rate lifecycle to be defined before any reverb component is written. Its acceptance bar is "explicit stability assumptions and stress-test criteria; no claim of proven sonic quality."

ADR-002 accepted the topology — an FDN of `N` mutually coupled delay lines, one real orthogonal matrix `A`, one scalar gain `gᵢ` and one bounded-real damping filter `Hᵢ(z)` per line inside the loop, homogeneous Jot decay `gᵢ = γ^mᵢ` — and proved strict stability for the *time-invariant* network via `‖ΓA‖₂ ≤ maxᵢ|gᵢ| < 1`. It explicitly left five things open, and they are exactly this ADR's scope:

1. The concrete damping filter. ADR-002 specified only the *property* "bounded-real," and warned that a filter "normalized for unity gain at DC that overshoots anywhere in the band destroys the guarantee."
2. Denormal mitigation. ADR-002 named three candidates — `FPCR.FZ` on render-thread entry, a deterministic state-level cutoff, additive anti-denormal noise — and selected none, having established that on ARM64 `FPCR` is per-thread and is not inherited by a host-created render thread.
3. Concrete, testable NaN/Inf and extreme-parameter bounds, and the stress specification for the worst-case combination it identified (maximum decay × minimum damping × full-scale input).
4. Full-network initialization/reset and sample-rate lifecycle. docs/phase1-s1-plan.md specifies this only for S1's single undamped delay line with no feedback; the network adds a matrix, per-line gains, and per-line filters with their own recursive state.
5. What modulation would require. ADR-002 authorizes none, and its proof does not extend to a time-varying network.

Sol decides; Astra accepts; Terra implements when separately authorized; Luna verifies.

### Alternatives

#### (a) Damping filter `Hᵢ(z)`

Judged against Aetherfield's stated needs — frequency-dependent decay that is *specified* rather than emergent, bounded-real by construction, and the smallest thing that does the job (§43). None of the rejected options is a bad filter.

| Criterion | (a1) One-pole lowpass, unity DC gain | (a2) First-order shelving filter (one pole, one zero) | (a3) Proportional graphic EQ / higher-order IIR |
|---|---|---|---|
| Degrees of freedom | 2 (`gᵢ`, `aᵢ`) → matches target decay *exactly* at DC and Nyquist | 3 → also an independent damping corner frequency | One per band; matches an arbitrary `T60(ω)` |
| Bounded-real by construction? | Yes, from a single coefficient constraint `aᵢ ∈ [0,1)`; `sup\|H\| = 1` attained only at DC | Yes, but only after checking two endpoint values (see the monotonicity lemma); a sign error silently breaks it | Not by construction; needs a design-time magnitude constraint per band and a dense verification |
| Peak (ℓ∞) behaviour | Also non-expansive: `h[n] = (1−aᵢ)aᵢⁿ ≥ 0`, so `‖h‖₁ = H(1) = 1` | Sign-changing impulse response gives `‖h‖₁ > \|H(1)\|`; peak-expansive | Generally peak-expansive |
| Mid-band `T60` accuracy | Approximate; error is the known weakness | Slightly better shaping, same first-order limitation | The documented fix; Schlecht and Habets show why it matters |
| Cost per line per sample | 1 multiply-add pair, 1 state | 2 multiplies, 2 states | Several biquads, several states |
| State to reset | 1 value | 2 values | Several per band |
| Verification burden | 2-point exact check plus a dense grid falsification run | Same, with more ways to get the sign wrong | Dense grid over every band, per line, per rate |

#### (b) Denormal mitigation

| Criterion | (b1) `FPCR.FZ` on render-thread entry | (b2) Deterministic state-level cutoff | (b3) Additive anti-denormal noise |
|---|---|---|---|
| Guaranteed present on the AUv3 render thread? | **No.** ADR-002 established `FPCR` is per-thread and not inherited by a host-created thread; the AArch64 `FPCR` description also makes `FZ`'s effect on outputs conditional on `FPCR.AH` | Yes — it is a property of the algorithm, not of thread state we do not own | Yes |
| Affects code we do not own | Yes: mutates the FP environment of whatever else runs on that thread unless saved and restored around the callback | No | No |
| Preserves ADR-002's contraction bound | Neutral | **Yes, provably**: a memoryless map with `\|f(x)\| ≤ \|x\|` has ℓ2 gain ≤ 1, so inserting it in the loop cannot raise `‖ΓA‖₂` | **No**: injects energy into a loop whose gain approaches 1 at maximum decay |
| Determinism (testing.md requires byte-exact repeat renders) | Yes | Yes, bit-exact and reproducible | Only if the noise is a fixed deterministic sequence; a PRNG adds seed state to reset |
| Effect on the noise floor of a quiet ambient tail | None | None; the discarded quantity is bounded by the threshold | A permanent noise floor — ADR-002 already called this the least attractive property |
| Tail termination | Tail approaches zero asymptotically | **Reaches exactly zero in bounded time** (proved below) | Never reaches zero |
| Cost | Two system-register accesses per callback | `2N` compares-and-selects per sample | `N` adds per sample plus generator state |
| Portability | AArch64-specific; x86 uses `MXCSR` | Architecture-independent | Architecture-independent |

### Decision

#### (a) Damping filter: a unity-DC-gain one-pole lowpass, `aᵢ ∈ [0, a_max]`

**Select (a1).** Each line's in-loop damping filter is the first-order lowpass

> `Hᵢ(z) = (1 − aᵢ) / (1 − aᵢ·z⁻¹)`,  difference equation  `wᵢ[n] = (1 − aᵢ)·vᵢ[n] + aᵢ·wᵢ[n−1]`

where `vᵢ[n]` is the delay-line output for line `i` and `wᵢ[n]` is the filter output, which is then scaled by `gᵢ` and fed to the matrix, exactly at the position ADR-002 fixed. One coefficient, one state value per line.

Coefficients are derived at preparation time, in double precision, from the decay targets at DC and Nyquist:

- `γ₀ = 10^(−3/(f_s·T60₀))` and `γ_π = 10^(−3/(f_s·T60_π))` — ADR-002's `γ_dB = −60/(f_s·T60)` evaluated at each end of the band.
- `gᵢ = γ₀^mᵢ` — unchanged from ADR-002's homogeneous decay law, now understood as the DC decay.
- `βᵢ = (γ_π/γ₀)^mᵢ = 10^(−3·mᵢ·(1/T60_π − 1/T60₀)/f_s)` — the extra attenuation the filter must supply at Nyquist on line `i`.
- `aᵢ = (1 − βᵢ)/(1 + βᵢ)`, clamped to `[0, a_max]` with **`a_max = 0.999`**.

Two coefficient-domain constraints are **binding at the control boundary**, not at render time:

- **`T60_π ≤ T60₀`.** High frequencies may never decay more slowly than DC. This is exactly what makes `βᵢ ∈ (0,1]` and therefore `aᵢ ∈ [0,1)`, which is what makes `Hᵢ` bounded-real. Damping may only shorten decay.
- **`aᵢ ≤ a_max = 0.999`.** Bounds the filter pole away from the unit circle and keeps `(1 − aᵢ)` representable in `float` without catastrophic cancellation. The clamp binds only in the direction of *less* extreme damping, so it can never compromise the bound.

`(1 − aᵢ)` and `aᵢ` are both computed in double and stored as a precomputed pair; `1 − aᵢ` is never formed at render time. "Damping bypassed" (ADR-002's S2 step) is the coefficient value `aᵢ = 0`, giving `Hᵢ(z) = 1`, not a separate code path or branch.

**REJECTED for now:** (a2) the first-order shelf, and (a3) higher-order / proportional-graphic-EQ attenuation filters. Neither is prohibited; (a3) is the named upgrade path if measured mid-band `T60` error exceeds the bound stated under (c).

**Still DEFERRED, unchanged:** `N`, the delay lengths `mᵢ`, and the values of `T60₀`/`T60_π` ranges (Phase 1 row 3). This ADR fixes the filter's *form and coefficient derivation*, not the numbers fed into it.

#### (b) Denormal mitigation: deterministic state-level cutoff, with `FPCR.FZ` as an optional, measured, non-load-bearing adjunct

**Select (b2) as the sole correctness mechanism.** A deterministic cutoff `f_ε(x) = x if |x| ≥ ε else 0`, with **`ε = 1e-20`** (≈ −400 dBFS), is applied at exactly the two recursive memories per line:

1. the value written into delay line `i` (which is the matrix output, so this covers the matrix, the gains and the input injection); and
2. the damping filter state `wᵢ[n−1]`.

These are the only recursive memories in the network; every other quantity is a per-sample temporary derived from them. The cutoff must be expressed as a branchless select so that per-sample work stays data-independent, per ADR-002's bounded-work rule and charter §19.

**`FPCR.FZ` is permitted only as an adjunct**, under three conditions: it must be set and restored around the render callback so the host's floating-point environment is not mutated beyond the call; correctness must be unaffected by whether it is set — every test must pass with it off; and it may only be adopted if a *measured* cost difference justifies it, per ADR-002's requirement that this choice be backed by measurement rather than assumption. **(b3) additive anti-denormal noise is REJECTED.**

#### (c) Numerical bounds and the stress specification

**Parameter validation at the control boundary.** Every parameter is validated and clamped *before* it becomes a coefficient; the render thread is never where an invalid value is discovered.

| Quantity | Rule | Failure behaviour |
|---|---|---|
| `T60₀`, `T60_π` | Must be finite and `> 0`; clamped to `[T60_min, T60_max]`, then `T60_π` further clamped to `≤ T60₀` | Non-finite is rejected, not clamped |
| `T60_max` | Must satisfy `1 − γ₀^{m_min} ≥ δ_margin` with `δ_margin ≥ 1e-3` | This *is* ADR-002's mandatory `γ_max` clamp, in its concrete form |
| `f_s` | Finite, `> 0`, in the supported set | `prepare` fails; prior preparation untouched |
| `mᵢ` | `≥ 1`, mutually distinct, `≤` reserved capacity | `prepare` fails; prior preparation untouched |
| `gᵢ`, `aᵢ`, `1 − aᵢ`, every entry of `A` | Computed in double, each checked finite and inside its stated interval before being stored as `float` | A non-finite derived coefficient is a programming error and fails `prepare`; it must never reach the render thread |
| Input samples | Non-finite input sample replaced by `0` and counted | No magnitude clamp (see Rationale) |

**Stability does not depend on coefficient-set atomicity.** Because `‖ΓA‖₂ ≤ maxᵢ|gᵢ|` holds for *any* combination of individually valid `gᵢ`, and `sup_ω|Hᵢ| ≤ 1` holds for *any* individually valid `aᵢ`, a torn coefficient update (a new `γ` beside an old `aᵢ`) is a correctness and smoothing concern for Phase 1 row 3 — it is not a stability hazard. This ADR records that property deliberately, so the lock-free transport decision is not forced to carry a stability obligation it cannot cheaply discharge.

**Non-finite handling.** An output-side non-finite detector is mandatory. On detection it (i) latches a flag and increments a counter that `reset()` does **not** clear, (ii) triggers the deterministic full reset path at the next block boundary, and (iii) never logs, allocates or formats strings. Per testing.md, any test run that ends with a nonzero counter is a **FAIL**, never a pass, and the intervention is reported, never hidden.

**Named stress cases.** These are the numbers ADR-002's S2 evidence list and testing.md's "Fixed late network" gate need; they add no new gate.

| ID | Case | Bound / required record |
|---|---|---|
| NS-1 | Matrix orthogonality | `‖AᵀA − I‖∞ ≤ 1e-6` in `float`, `≤ 1e-12` in double, computed independently of the construction |
| NS-2 | Bounded-realness, exact | For every `i`: `max(\|Hᵢ(1)\|, \|Hᵢ(−1)\|) ≤ 1`. `\|Hᵢ(1)\| = 1` and `\|Hᵢ(−1)\| = (1−aᵢ)/(1+aᵢ)` |
| NS-3 | Bounded-realness, falsification | Dense grid of ≥ 2¹⁶ points on `[0, π]`: `sup\|Hᵢ(e^{jω})\| ≤ 1 + 1e-6`. Required *even though* NS-2 is exact, per ADR-002: measured, never assumed |
| NS-4 | Margin | Record `ρ = σ_max(ΓA)`, require `ρ ≤ maxᵢ gᵢ·(1 + 1e-6)` and `1 − ρ ≥ δ_margin` |
| NS-5 | Coefficient finiteness | Every stored coefficient finite and inside its stated interval, at every supported rate and at both `T60` extremes |
| NS-6 | Decay law | Measured energy decay curve vs. `10^(−3n/(f_s·T60₀))` at DC, and realized `T60(ω)` vs. target in ≥ 8 bands; **record the maximum band error in percent** and compare it against the reported `T60` JNDs (Schlecht and Habets cite 4–12 % depending on stimulus) |
| NS-7 | Worst-case combination | `T60₀ = T60_max` × minimum damping (`aᵢ = 0`) × full-scale input, ≥ 120 s, across block partitions `{1, 13, 64, 512, ragged}`, at every supported rate. Required: no non-finite sample; detector counter zero; peak internal magnitude recorded; output overshoot reported, never clipped away |
| NS-8 | Finite-time silence | From a full-scale impulse with zero input thereafter, the network reaches **exactly zero** on every state within the bound `T_silence ≤ (m_max/f_s)·ln(ε/‖s₀‖₂)/ln ρ`. Record measured time-to-silence, and time-to-first-denormal with the cutoff disabled |
| NS-9 | Denormal cost | Cost with cutoff, without cutoff, and with/without `FPCR.FZ` — four measurements. `FPCR.FZ` may only be adopted on this evidence |
| NS-10 | Double-precision reference | Same fixture in double, with a justified tolerance (ADR-002 S2 item 7) |
| NS-11 | Determinism | Repeated renders byte-identical (testing.md). The cutoff keeps this true; anti-denormal noise would have put it at risk |

#### (d) Full-network initialization/reset and sample-rate lifecycle

This extends docs/phase1-s1-plan.md's single-delay-line contract. Everything S1 specifies still holds per line; the additions below are what the network adds. No interface is written here — that translation is Terra's, under separate authorization.

**`prepare` takes:** the sample rate; the maximum block size the host may request; and the *time-domain* network specification (maximum delay time, maximum `T60`, the delay-length specification, `N`). It does **not** take delay lengths in samples — those are derived, per ADR-002.

**`prepare` guarantees, on success:**

1. It is the only allocating operation, and it runs off the render thread.
2. Delay storage for all `N` lines is reserved for the longest supported delay time at the **highest** supported sample rate (ADR-002), so no later supported rate can require more.
3. Every `mᵢ` is derived from its time-domain specification at *this* `f_s`; every `gᵢ` and `aᵢ` is derived from *this* `f_s` and *this* `mᵢ`, in double.
4. `A` is fixed, and its orthogonality residual is computed once here and retained as a diagnostic.
5. Every validation rule in (c) is applied. A violation **rejects** the preparation — it does not clamp structural quantities and does not partially apply.
6. The object is left in exactly the state `reset()` defines.
7. **A failed `prepare` changes nothing.** A prior valid preparation remains valid and usable, and an unprepared object remains unprepared. This is S1's rule, and it matters more here: a host probing unsupported formats must not be able to destroy a working configuration.

**`reset` guarantees** (noexcept, allocation-free, bounded work, safe on the render thread):

1. All `N` delay buffers zeroed across their **full reserved capacity**, not merely the active `mᵢ` — otherwise a later length change reads stale samples.
2. All `N` write positions restored to their initial value.
3. **Every damping filter's recursive state set to exactly `0`.** This is the state S1 has no analogue for and the one most likely to be forgotten; a reset that misses it leaves an audible decaying residue and silently breaks the silence-in/silence-out canary.
4. The non-finite detector's latched flag cleared; its **counter is not cleared**, so an intervention cannot be erased by a reset.
5. **Coefficients and configuration are not touched.** `reset` clears state, never configuration.
6. Idempotent and repeatable: `reset(); reset()` is indistinguishable from one `reset()`, and post-reset behaviour does not depend on call history.
7. Because the zero state is an equilibrium (ADR-002), silence in yields *exactly* zero out, bit-for-bit — the canary for any state missed by rules 1–3.
8. Safe before `prepare` (a no-op), matching S1.

**A sample-rate change requires:**

1. A full re-`prepare`. There is no `setSampleRate`, following S1's precedent.
2. Re-derivation of `mᵢ`, `γ₀`, `γ_π`, every `gᵢ` and every `aᵢ`. Caching any of them across a rate change silently changes `T60` — the hazard ADR-002 named — and `aᵢ` is newly exposed to it, since it depends on both `f_s` and `mᵢ`.
3. A full reset. All retained delay contents and filter states are at the wrong rate and are **discarded, not resampled**. The tail is cut. This is a deliberate, documented consequence, not a seamless transition; hosts change rate outside continuous playback, and pretending otherwise would require resampling machinery this ADR does not authorize.
4. An unsupported rate fails `prepare` under rule 7 above.

**AUv3 mapping (for the later integration layer, not decided here):** Apple's extension guide documents `allocateRenderResourcesAndReturnError:` as called by the host before rendering starts and `deallocateRenderResources` after it finishes. `prepare` belongs in the former and nowhere else; `reset` and `process` are the only render-side operations. ADR-001's framework deferral is unaffected.

#### (e) What modulation would require — scoping only, authorizing nothing

**ADR-002's prohibition stands in full. This ADR authorizes no modulation of any kind, and nothing in (a)–(d) makes the network safe to modulate.** Two decisions above must not be misread as progress toward modulation:

- The bounded-real damping filter and the decay law are **LTI arguments**. Their conclusion — loop contraction — is void for a time-varying network.
- The denormal cutoff and the non-finite guard remain *sound* under time variation, because they are memoryless value guards rather than frequency-domain arguments. They remain sound; they prove nothing.

A future ADR proposing modulation is admissible only via one of two paths, each with a stated bar.

**Path A — analytic, via unitary matrix modulation (preferred).** Adopt Schlecht and Habets' result that modulating the feedback matrix along a path that stays unitary is guaranteed stable, and inherit its proof. Admissible only with:

- **A1.** A proof that the chosen parameterization remains unitary at *every sample along the path*, not merely at its endpoints — the parameterization itself must be shown closed under the modulation (e.g. a product of Givens rotations or a matrix exponential), with the orthogonality residual measured on a dense sampling of the path, not only at endpoints.
- **A2.** Delay lengths, per-line gains and damping coefficients held time-invariant. Modulating any of them is outside the cited result and stays unauthorized.
- **A3.** Per-sample cost shown to remain bounded and data-independent per §19; ADR-002's `O(N log N)` matrix budget is the reference point.
- **A4.** A new ADR. ADR-002's matrix-family decision may have to change — a modulation path must lie inside a family closed under the parameterization, which a fixed Hadamard matrix is not. ADR-002's "Revisit When" already anticipates exactly this.

**Path B — empirical, for anything else, notably delay-length modulation.** The step that fails is identifiable and must be named in any such proposal: a time-varying delay *resamples* the signal it stores, so the delay bank is no longer norm-preserving, and the small-gain argument loses its first assumption. Admissible only through testing.md's Modulation experiment gate, with:

- **B1.** A retained unmodulated fixed baseline that remains the default and stays selectable.
- **B2.** Measured evidence at the stability-critical corner: NS-7's combination extended with maximum modulation depth × maximum modulation rate, over ≥ 10·`T60_max`, at every supported rate, across the NS-7 block partitions. No non-finite value, detector counter zero, and a recorded bound on output growth relative to the unmodulated baseline.
- **B3.** Interpolation boundary tests: modulation crossing read-index integer boundaries and sitting at both extremes of its range, plus the interpolator's own magnitude response. A linear interpolator's magnitude never exceeds 1, which is favourable but **does not restore the time-invariant proof**, because the energy change comes from the time variation of the read index, not from the interpolator's frozen response.
- **B4.** An explicit statement, in the proposing ADR, that empirical evidence bounds observed behaviour on a finite tested corpus and is not a stability proof.
- **B5.** Explicit approval before enabling, per testing.md.

Until one of these is satisfied and recorded in a new accepted ADR, charter §13's "slow evolution" remains, as ADR-002 put it, an unfunded requirement.

### Rationale

#### Why the one-pole lowpass, and why it is bounded-real by construction rather than by intent

ADR-002's warning is precise: unity gain at DC does not imply `sup_ω|H| ≤ 1`. The chosen filter answers it structurally.

**Bounded-realness proof.** With `Hᵢ(z) = (1−aᵢ)/(1−aᵢz⁻¹)` and `aᵢ ∈ [0,1)`:

`|Hᵢ(e^{jω})|² = (1−aᵢ)² / (1 + aᵢ² − 2aᵢcos ω)`.

The denominator is affine and strictly increasing in `−cos ω` for `aᵢ > 0`, so it is minimized at `ω = 0`, where it equals `(1−aᵢ)²`. Hence `|Hᵢ|` attains its maximum **1** at DC, decreases monotonically to `(1−aᵢ)/(1+aᵢ)` at Nyquist, and `sup_ω|Hᵢ| = 1` exactly. The pole is at `z = aᵢ` with `|aᵢ| < 1`, so the filter is stable. Both halves of bounded-realness follow from the single constraint `aᵢ ∈ [0,1)` — which the coefficient derivation produces automatically whenever `T60_π ≤ T60₀`. There is no normalization step that could overshoot, because there is no normalization step.

**A general lemma, worth recording because it makes the check exact.** For any real first-order filter `H(z) = (b₀ + b₁z⁻¹)/(1 + a₁z⁻¹)` with `|a₁| < 1`,

`|H(e^{jω})|² = (b₀² + b₁² + 2b₀b₁cos ω) / (1 + a₁² + 2a₁cos ω)`.

Numerator and denominator are both affine in `c = cos ω`, and the denominator equals `|1 + a₁e^{−jω}|² ≥ (1−|a₁|)² > 0` throughout. A ratio of affine functions with non-vanishing denominator is monotone in `c`, and `cos ω` is strictly decreasing on `[0, π]`. Therefore **any first-order real filter's magnitude is monotone in `ω`, and `sup_ω|H| = max(|H(1)|, |H(−1)|)`** — a two-point exact test, not a grid approximation. That is NS-2. NS-3's dense grid is retained anyway, as a falsification test of the *implementation* against the lemma, because ADR-002 requires bounded-realness measured rather than inferred, and the lemma is a statement about the mathematics, not about the code that will eventually realize it.

**A property the alternatives lack.** For `aᵢ ∈ [0,1)` the impulse response `h[n] = (1−aᵢ)aᵢⁿ` is non-negative, so `‖h‖₁ = H(1) = 1`. The filter is therefore non-expansive in the *peak* sense as well as the energy sense. A first-order shelf, whose impulse response changes sign, has `‖h‖₁ > |H(1)|` and is peak-expansive even when bounded-real. Nothing in ADR-002 requires the stronger property, but having it for free — while the alternative costs an extra multiply, an extra state and an extra way to get a sign wrong — is what §43 means by preferring the smaller, more understandable option.

**Why the derivation is exactly Jot's.** Substituting `aᵢ` and `gᵢ` gives the combined per-line response `gᵢHᵢ(z) = [2R₀Rπ/(R₀+Rπ)] / (1 − aᵢz⁻¹)` with `R₀ = γ₀^mᵢ` and `Rπ = γ_π^mᵢ`. That is identical, coefficient for coefficient, to the classic first-order delay-filter design published in Smith's *Physical Audio Signal Processing*: `Hᵢ(z) = gᵢ/(1 − pᵢz⁻¹)` with `pᵢ = (R₀ − Rπ)/(R₀ + Rπ)` and `gᵢ = 2R₀Rπ/(R₀ + Rπ)`. This ADR adopts a known design, factored the way ADR-002 asked for it — one scalar `γ^mᵢ` gain and one separately verifiable bounded-real filter — rather than inventing one. The factoring is not cosmetic: it is what makes ADR-002's stability condition checkable as two independent properties (`maxᵢ gᵢ < 1` and `sup|Hᵢ| ≤ 1`) instead of one fused coefficient.

**The honest limitation, and why it is a safety matter and not only a tuning matter.** Two degrees of freedom match the target decay exactly at DC and at Nyquist, and only approximately in between; the per-line filter shapes differ, so homogeneity across lines is exact at those two frequencies and approximate elsewhere. Schlecht and Habets show this is not a cosmetic error: because `T60(ω)` depends *reciprocally* on the attenuation response, an attenuation filter with only a few dB of approximation error can produce a large `T60` error, and their worked example has an **infinite `T60` at 2 kHz** from a filter whose magnitude error looks small. That result is the strongest available argument for this ADR's whole approach: the way a first-order attenuation filter fails is by letting the realized loop gain approach or reach 1 somewhere in the band. Constraining `sup_ω|gᵢHᵢ| ≤ gᵢ < 1` by construction removes the catastrophic form of that failure — an infinite `T60` becomes impossible — and leaves only the benign form, a `T60(ω)` that misses its target mid-band. NS-6 measures the benign residue and compares it against published just-noticeable differences for reverberation time; if it exceeds them, (a3) is the documented upgrade, not a reason to relax the bound.

**Why not add low-frequency damping now.** A one-pole lowpass gives high-frequency damping only. A "Low Damp" control is a *candidate* in charter §14, not a requirement, and Phase 1 row 3 has not yet decided which controls are justified. Deciding the filter order for a parameter that may not exist would be exactly the premature complexity §43 and §46 rule out. If Low Damp is later accepted, the peak-normalized generalization `Hᵢ(z) = (1 − |aᵢ|)/(1 − aᵢz⁻¹)` extends the same bounded-real guarantee to `aᵢ ∈ (−1, 1)` — its peak is `1` at DC for `aᵢ > 0` and at Nyquist for `aᵢ < 0` — and the monotonicity lemma covers any first-order shelf with a two-point check. The escape hatch is pre-analyzed; it is simply not taken.

#### Why a deterministic cutoff, and why `FPCR.FZ` cannot be the mechanism

ADR-002 established the decisive platform fact: on ARM64 the flush-to-zero control is per-thread and is not inherited by a host-created render thread, so an AUv3 render block may run without it regardless of what the containing app does. Arm's own register description adds a second reason for caution: `FZ`'s effect on instruction *outputs* is conditional on `FPCR.AH`, and it does not apply uniformly to every instruction. A correctness property that depends on a bit we neither own nor control, whose semantics are conditioned on another bit, is not a correctness property. **The design must be correct with denormals present**, which ADR-002 already stated; the cutoff is the mechanism that makes that true rather than aspirational.

**The cutoff preserves ADR-002's proof, and this is the reason to prefer it over anti-denormal noise.** `f_ε` is memoryless with `|f_ε(x)| ≤ |x|` for every `x`, so for any sequence `Σ f_ε(x[n])² ≤ Σ x[n]²`: its ℓ2 gain is at most 1. Inserting a gain-≤1 memoryless map into the loop cannot increase the loop's ℓ2 gain, so `‖ΓA‖₂ ≤ maxᵢ gᵢ < 1` survives unchanged and the network remains a strict contraction. Additive noise does the opposite — it injects energy into a loop whose gain is deliberately pushed close to 1 at maximum decay, which is the one place the margin is thinnest, and it permanently raises the noise floor of a tail whose entire purpose is to be quiet. ADR-002 already called that the least attractive option; the contraction argument makes it the least defensible one.

**The cutoff buys a property nothing else offers: exact silence in bounded time.** With zero input, the state energy contracts by at least `ρ` per circulation, so after `k` circulations `‖s‖₂ ≤ ρᵏ‖s₀‖₂`. Once `‖s‖₂ < ε`, *every* component satisfies `|sⱼ| ≤ ‖s‖₂ < ε` and is flushed to exactly zero; the zero state is an equilibrium, so the network stays exactly zero. Hence the tail terminates exactly, within `T_silence ≤ (m_max/f_s)·ln(ε/‖s₀‖₂)/ln ρ`. ADR-002 observed that in exact arithmetic a stable feedback tail never reaches zero in finite time; the cutoff converts that from a permanent denormal-generating condition into a bounded, testable, bit-exact one. NS-8 tests it.

**Choosing `ε = 1e-20`.** Two constraints bracket it. From below, it must sit far enough above the largest `float` subnormal (`≈ 1.18e-38`) that no product formed in the recursion can land in the subnormal range: the smallest coefficient in the loop is `1 − a_max = 1e-3`, so the smallest non-flushed product is `≈ 1e-23`, still fifteen orders of magnitude above the subnormal threshold. From above, the discarded quantity must be inaudible and unrepresentable by any output format: `ε = 1e-20` is about −400 dBFS, roughly 256 dB below the `float` resolution near unity (`≈ −144 dBFS`) and about 280 dB below a 20-bit noise floor. Per-sample discarded energy is at most `2Nε²`. The margin at both ends is large enough that `ε` is not a tuning parameter.

**Determinism.** testing.md requires byte-identical repeat renders. The cutoff is bit-exact and stateless; noise would have made byte-exactness depend on a seed and added generator state to the reset contract. NS-11 would have become materially harder to satisfy.

**Cost.** `2N` compares-and-selects per sample, branchless, data-independent. NS-9 measures it against the alternatives rather than assuming it is cheap — this is the "measured cost difference, not an assumption" that ADR-002 required, kept as an obligation rather than pre-empted.

#### Why there is no input magnitude clamp

The network is linear and homogeneous: scaling the input scales every internal value exactly. There is therefore no magnitude threshold at which behaviour qualitatively changes short of `float` overflow, and clamping input magnitude would distort legitimately hot signals to defend against a failure mode that does not exist at audio levels. What *is* needed is a non-finite guard, because a dense feedback matrix distributes one NaN to all `N` lines within a single matrix application and never releases it (ADR-002), and because Arm's default-NaN mode standardizes the value produced, not its propagation.

**Quantifying the headroom that makes this safe.** For a sinusoidal input of amplitude `X` at any frequency, the steady-state internal amplitude is bounded by `G·X` with `G = ‖b‖₂/(1 − maxᵢ gᵢ)`, from the same small-gain argument ADR-002 used: the delay bank preserves the ℓ2 norm, `A` is orthogonal, `sup_ω|Hᵢ| ≤ 1`, so the loop's gain is at most `maxᵢ gᵢ` and the closed-loop gain is at most `1/(1 − maxᵢ gᵢ)`. `maxᵢ gᵢ = γ₀^{m_min}` is set by the *shortest* line and the decay clamp. As an illustration on clearly **assumed** values — `N` and the delay lengths remain deferred per ADR-002, and nothing here fixes them — at `f_s = 48 kHz`, an assumed `T60_max = 30 s` and an assumed shortest line of 20 ms (960 samples): `γ₀ = 10^(−2.083e-6)`, `g_max ≈ 0.99540`, so `1 − g_max ≈ 4.6e-3` and `G ≈ 217·‖b‖₂`, about +47 dB. With unit-norm injection and a hot `+24 dBFS` input, internal magnitudes reach order `10³` — utterly unremarkable against `float`'s `≈ 3.4e38` ceiling. Overflow to `Inf` from ordinary signal would require an input around `10³⁵`.

Two conclusions follow, and both are more useful than a vague worry about "Inf propagation". First, **`float` overflow is not a credible failure mode for this structure under the stated clamps**; the credible ones are non-finite values arriving from outside, audible output overshoot, and denormal underflow at the tail — which is exactly what (b) and the detector address. Second, the formula `G = ‖b‖₂/(1 − γ₀^{m_min})` is the concrete quantity that must be computed and recorded once `T60_max`, `f_s` and the delay set are fixed, and it is the honest statement of how much internal gain the maximum-decay setting really carries. The corresponding *peak* bound for arbitrary inputs has no simple closed form — it is finite because the network is BIBO stable, but bounding it requires the ℓ1 norm of the impulse response — so it is **measured** under NS-7 and reported, never asserted. That split is deliberate: prove what is provable, measure what is not, and never let the second masquerade as the first.

#### Why the reset contract grows where it does

S1's single delay line has exactly one kind of state: the ring buffer. The network adds two more. The damping filter's recursive state is the subtle one — it is one `float` per line, it is invisible in any block diagram drawn at topology level, and forgetting it produces a fault that is easy to mistake for correct behaviour, because the residue decays. Rule 7's silence-in/silence-out equality is the canary precisely because it fails on exactly this kind of omission. Zeroing the *full reserved capacity* rather than the active `mᵢ` matters for the same reason: reserving at the highest supported rate (ADR-002) means the active region is usually smaller than the buffer, and a later preparation with a longer `mᵢ` would otherwise read samples from a previous configuration.

The detector counter surviving `reset` is a testing.md obligation, not a debugging convenience: interventions are to be reported, never hidden, and a counter that any reset clears would let a stress run erase its own evidence.

### Evidence

No Aetherfield measurement supports this decision. The two numerical checks below were computed during the drafting of this ADR and are arithmetic on stated assumptions, not measurements of any Aetherfield code, of which none exists.

- **Bounded-realness of the chosen design, verified numerically over its design range.** For assumed `f_s = 48 kHz` and six `(mᵢ, T60₀, T60_π)` combinations spanning `mᵢ ∈ {1201, 2399, 4801}` and `T60₀/T60_π` ratios from `1:1` to `60:0.1`, the derivation `aᵢ = (1−βᵢ)/(1+βᵢ)` produced `aᵢ ∈ [0.0, 0.938]` and a dense 200,001-point grid over `[0, π]` gave `sup_ω|Hᵢ(e^{jω})| = 1.000000000000` in every case, attained at `ω = 0`, with `|Hᵢ(−1)| = (1−aᵢ)/(1+aᵢ)` matching `βᵢ` to printed precision. The `mᵢ` values are illustrative spans, not a delay-length decision.
- **The `a_max` clamp binds only at extreme damping.** `a_max = 0.999` corresponds to `β_min ≈ 5.0e-4` (≈ −66 dB of extra Nyquist attenuation per circulation). On an assumed 50 ms line at 48 kHz with `T60₀ = 4 s`, that is reached only when the requested `T60_π` falls below roughly 45 ms — a setting at which the tail is already effectively dark. The clamp therefore never binds in the ordinary operating range and, when it does bind, moves toward less extreme damping.
- [Smith, *Physical Audio Signal Processing* (CCRMA): "Delay-Line Damping Filter Design"](https://ccrma.stanford.edu/~jos/pasp/Delay_Line_Damping_Filter_Design.html) — the per-line requirement `20·log₁₀|Hᵢ(e^{jωT})| = −60·Mᵢ·T/T60(ω)`, and the observation that because `T60(ω)` varies smoothly with `ω`, the filters `Hᵢ(z)` can be very low order. [First-Order Delay-Filter Design](https://ccrma.stanford.edu/~jos/pasp/First_Order_Delay_Filter_Design.html) — the one-pole form `Hᵢ(z) = gᵢ/(1 − pᵢz⁻¹)` with `pᵢ = (R₀^{Mᵢ} − R_π^{Mᵢ})/(R₀^{Mᵢ} + R_π^{Mᵢ})` and `gᵢ = 2R₀^{Mᵢ}R_π^{Mᵢ}/(R₀^{Mᵢ} + R_π^{Mᵢ})`, with DC gain `gᵢ/(1−pᵢ)` and Nyquist gain `gᵢ/(1+pᵢ)`. This ADR's derivation is algebraically identical, re-factored into ADR-002's `gᵢ · Hᵢ(z)` split.
- [Jot and Chaigne, "Digital Delay Networks for Designing Artificial Reverberators," AES 90th Convention, February 1991](https://aes2.org/publications/elibrary-page/?id=5663) — per-line absorbent filters designed so all lines decay at the same rate at every frequency; cited in ADR-002 and load-bearing again here.
- [Schlecht and Habets, "Accurate Reverberation Time Control in Feedback Delay Networks," Proc. DAFx-17, Edinburgh, UK, September 5–9, 2017, pp. 337–344](https://dafx17.eca.ed.ac.uk/papers/DAFx17_paper_11.pdf) — "in the early days, the most cheaply available filter was a one-pole lowpass filter"; the attenuation response must be proportional to the delay length to give a global attenuation-per-second; and, decisively for this ADR, that the filter approximation error propagates *non-linearly* to the resulting reverberation time — their Figure 2 shows an attenuation filter with a few dB of error yielding an **infinite `T60` at 2 kHz**. Also the source for the reported `T60` just-noticeable differences (≈ 4 % for noise bands; 5–12 % for impulse responses; 3–9 % for speech) used as NS-6's comparison, and for the proportional-graphic-equalizer design named as this ADR's (a3) upgrade path.
- Smith, *Physical Audio Signal Processing*: [FDN Stability](https://ccrma.stanford.edu/~jos/pasp/FDN_Stability.html) and [Achieving Desired Reverberation Times](https://ccrma.stanford.edu/~jos/pasp/Achieving_Desired_Reverberation_Times.html) — the `‖A‖₂ < 1` criterion and the requirement that the in-loop lowpass have gain not exceeding 1 at any frequency. Both already underpin ADR-002; this ADR supplies the filter that satisfies the second.
- [Arm: FPCR, Floating-point Control Register (AArch64)](https://developer.arm.com/documentation/ddi0595/latest/AArch64-Registers/FPCR--Floating-point-Control-Register) — `FZ`, bit [24], the "flushing denormalized numbers to zero control bit": when set, denormalized single- and double-precision *inputs to and outputs from* floating-point instructions are flushed to zero, with the output behaviour conditioned on `FPCR.AH`, and with documented exceptions for `FABS`/`FNEG` and (when `FPCR.AH` is 1) the `FMAX`/`FMIN` family. Combined with ADR-002's established per-thread, non-inherited nature of `FPCR` on ARM64, this is the basis for refusing to make `FZ` load-bearing.
- [de Soras, "Denormal Numbers in Floating Point Signal Processing Applications," 2005](https://ldesoras.fr/doc/articles/denormal-en.pdf) — the standard survey of the denormal slowdown in audio DSP and of the mitigation families evaluated in the (b) table. Cited for the taxonomy and the trade-offs, not for any ARM64-specific or Aetherfield-specific cost claim.
- [Apple, Audio Unit — App Extension Programming Guide](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html) — `allocateRenderResourcesAndReturnError:` is "called before it starts to render audio" and `deallocateRenderResources` "after it has finished rendering audio", with `internalRenderBlock` returning the rendering loop. This is the documented host lifecycle that (d)'s `prepare`/`reset` split maps onto; the framework choice itself remains DEFERRED per ADR-001.
- [Schlecht and Habets, "Time-varying feedback matrices in feedback delay networks and their application in artificial reverberation," *JASA* 138(3), September 2015, p. 1389](https://pubs.aip.org/asa/jasa/article-abstract/138/3/1389/680169/Time-varying-feedback-matrices-in-feedback-delay) — the sole modulated-stability result relied on, covering unitary *feedback-matrix* modulation only. Cited in (e) to scope a future decision, not to authorize one.

### Consequences

ADR-002's two independently checkable stability conditions now have concrete, independently checkable realizations. `sup_ω|Hᵢ| ≤ 1` reduces to an exact two-point evaluation by the monotonicity lemma, backed by a dense-grid falsification run; `maxᵢ gᵢ < 1` reduces to a `T60_max` clamp with a stated minimum margin `δ_margin`. The catastrophic failure mode that Schlecht and Habets document for attenuation filters — an infinite `T60` somewhere in the band — is structurally impossible under this design, and the residual first-order approximation error becomes a measurable tuning quantity with a published comparison standard rather than a stability risk.

The denormal decision moves denormal correctness out of the platform and into the algorithm. The network no longer depends on a control register we do not own, it terminates its tail exactly in bounded time, it stays byte-exactly reproducible, and — because `f_ε` is a contraction — it does all of this without touching ADR-002's stability bound. The cost is `2N` branchless selects per sample, which NS-9 must measure rather than assume, and the obligation that any implementation express the cutoff without a data-dependent branch.

New obligations are created. Damping introduces one recursive state per line that `reset` must clear and that any reset test must actually observe; the silence-in/silence-out equality is now load-bearing rather than decorative. `aᵢ` joins `gᵢ` and `mᵢ` as a quantity that must be re-derived on every sample-rate change, widening the ADR-002 hazard that a cached coefficient silently changes the decay. A sample-rate change now explicitly discards the tail rather than preserving it, which is an audible cut that must be documented in any user-facing material rather than discovered. The non-finite detector's counter must survive `reset`, which constrains how diagnostics are surfaced. `T60_π ≤ T60₀` becomes a product-level constraint: damping can only shorten decay, so a "brighter than the body of the tail" setting is not reachable without adopting the peak-normalized generalization in a later ADR.

One constraint is *relaxed*, deliberately and with proof: because each coefficient is individually clamped, `‖ΓA‖₂ ≤ maxᵢ gᵢ < 1` and `sup|Hᵢ| ≤ 1` hold for any mixture of individually valid coefficients. A torn coefficient update is therefore not a stability hazard, which frees Phase 1 row 3's lock-free transport decision from an obligation it would otherwise have had to discharge.

Nothing above is a sonic result, and the deferrals stand exactly as ADR-002 left them. `N`, the delay lengths, the diffusion stages, stereo extraction, interpolation, parameter names/ranges/mappings, and whether modulation exists at all remain open. Modulation in particular is not moved one step closer to authorization by this ADR; (e) states the bar, and the bar is unmet.

### Revisit When

NS-6 shows the realized `T60(ω)` deviates from target by more than the published just-noticeable differences in bands that matter, at which point the first-order filter is insufficient and (a3) — a proportional graphic equalizer or another higher-order attenuation filter, designed under an explicit per-band magnitude bound — becomes the successor decision; a Low Damp control is accepted in Phase 1 row 3, at which point the peak-normalized one-pole or a first-order shelf must be decided under the same two-point bounded-real test; NS-9 shows the cutoff's cost is material on target hardware and `FPCR.FZ` with save/restore is measurably better, which would make `FZ` an adopted optimization but never the correctness mechanism; NS-7 or NS-8 reveals an internal peak or a time-to-silence far from the predicted bound, indicating the small-gain model does not describe the realized network; a Freeze/Infinite design requires unity loop gain, which voids both the `γ_max` clamp and the finite-time-silence property and therefore needs its own numerical-bounds ADR; `ε = 1e-20` proves to interact badly with a later stage placed after the network (a large output gain could in principle lift a truncation artifact, though the stated margins make this remote); a supported sample rate is added whose ratio to the existing maximum changes the reserved-capacity rule; or modulation is proposed, at which point ADR-002's prohibition and this ADR's (e) govern, the fixed baseline must be retained, and the matrix-family decision may have to be reopened.

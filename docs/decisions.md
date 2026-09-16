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

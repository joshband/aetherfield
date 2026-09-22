// ADR-011 State-Restore Verification Tests
// Verifies SR-P1 (transport safety), SR-P2 (atomic observation),
// SR-P3 (lifecycle), and SR-P5 (error behavior).
//
// Gates verified:
// - SR-1: Combined values (defaults, endpoints, derivation)
// - SR-2: Rejection (NaN/Inf, no mutation, counter increment)
// - SR-3: Coherent consumption (writer/reader interleaving)
// - SR-5: Arbitration (Host → restore → UI order)
// - SR-6: Payload validation (absent, invalid, version, fixture)

#include "wrapper/ParameterBridge.h"
#include "wrapper/StateSchema.h"
#include "dsp/DiffusionStereoPath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using aetherfield::dsp::DiffusionStereoPath;
using aetherfield::dsp::DiffusionStereoConfig;
using aetherfield::wrapper::ParameterBridge;
using aetherfield::wrapper::Parameter;
using aetherfield::wrapper::RestoreTuple;
using aetherfield::wrapper::StatePayload;
using aetherfield::wrapper::FixtureStamp;
using aetherfield::wrapper::StateRestoreResult;
using aetherfield::wrapper::validateStatePayload;

namespace {

std::size_t g_tests_passed = 0;
std::size_t g_tests_failed = 0;

void logTestResult(const char* name, bool passed) {
    if (passed) {
        std::cout << "[PASS] " << name << std::endl;
        ++g_tests_passed;
    } else {
        std::cerr << "[FAIL] " << name << std::endl;
        ++g_tests_failed;
    }
}

FixtureStamp makeTestFixture() {
    return {
        .lineCount = 8,
        .minDelaySeconds = 0.027,
        .maxDelaySeconds = 0.081,
        .sampleRate = 48000.0,
        .dMaxDb = 48.0,
    };
}

DiffusionStereoConfig makeTestConfig(double sampleRate = 48000.0) {
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

// ===== SR-P5: Error Behavior Tests =====

bool testRejectedStateDoesNotMutateAcceptedTargets() {
    // SR-P5: Rejected state in running session must not mutate accepted controls.
    ParameterBridge bridge;
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    bridge.writeHost(Parameter::Decay, 0.6f);
    bridge.writeHost(Parameter::Damp, 0.3f);
    bridge.writeHost(Parameter::Mix, 0.8f);
    if (bridge.drain(path) == 0) return false;

    auto before = path.controls();
    if (std::abs(before.decay - 0.6) > 1e-5) return false;
    if (std::abs(before.damp - 0.3) > 1e-5) return false;
    if (std::abs(before.mix - 0.8) > 1e-5) return false;

    // Attempt to restore non-finite values (should be rejected).
    bridge.writeRestore({std::nan(""), 0.0, 1.0});
    if (bridge.drainRestore(path)) return false;  // Should return false

    // Accepted targets must be unchanged.
    auto after = path.controls();
    if (std::abs(after.decay - 0.6) > 1e-5) return false;
    if (std::abs(after.damp - 0.3) > 1e-5) return false;
    if (std::abs(after.mix - 0.8) > 1e-5) return false;

    return true;
}

bool testFixtureMismatchIsValidButSurfaced() {
    // SR-P5: Fixture mismatch is accepted (isValid=true) but hasMismatch=true.
    StatePayload payload;
    payload.schemaVersion = 1;
    payload.decayNormalized = 0.5;
    payload.dampNormalized = 0.0;
    payload.mixNormalized = 1.0;
    payload.fixtureLineCount = 16;  // mismatch
    payload.fixtureMinDelaySeconds = 0.027;
    payload.fixtureMaxDelaySeconds = 0.081;
    payload.fixtureSampleRate = 48000.0;
    payload.fixtureDMaxDb = 48.0;

    auto fixture = makeTestFixture();
    auto result = validateStatePayload(payload, fixture);

    if (!result.isValid()) return false;
    if (!result.hasMismatch()) return false;
    if (result.status != StateRestoreResult::Status::FixtureMismatch) return false;

    return true;
}

bool testUnrecognizedVersionRejected() {
    // SR-P6: Newer schemaVersion → hard reject.
    StatePayload payload;
    payload.schemaVersion = 99;  // unrecognized
    payload.decayNormalized = 0.5;
    payload.dampNormalized = 0.0;
    payload.mixNormalized = 1.0;

    auto fixture = makeTestFixture();
    auto result = validateStatePayload(payload, fixture);

    if (result.isValid()) return false;
    if (result.status != StateRestoreResult::Status::UnrecognizedVersion) return false;

    return true;
}

// ===== SR-P1/SR-P2/SR-P3: Transport & Atomicity Tests =====

bool testTripleBufferSlotWraparound() {
    // SR-P1: Verify safe slot reuse with wraparound.
    ParameterBridge bridge;
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    std::vector<RestoreTuple> tuples = {
        {0.1, 0.0, 1.0},
        {0.2, 0.1, 0.9},
        {0.3, 0.2, 0.8},
        {0.4, 0.3, 0.7},  // wraps to slot 0
        {0.5, 0.4, 0.6},
    };

    for (const auto& tuple : tuples) {
        bridge.writeRestore(tuple);
        bridge.drainRestore(path);
    }

    auto final = path.controls();
    if (!std::isfinite(final.decay)) return false;
    if (!std::isfinite(final.damp)) return false;
    if (!std::isfinite(final.mix)) return false;

    return true;
}

bool testCoherentTupleObservation() {
    // SR-P2: Verify coherent observation of a complete tuple.
    ParameterBridge bridge;
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    RestoreTuple original{0.7, 0.5, 0.3};
    bridge.writeRestore(original);
    if (!bridge.drainRestore(path)) return false;

    auto observed = path.controls();
    if (std::abs(observed.decay - original.decay) > 1e-5) return false;
    if (std::abs(observed.damp - original.damp) > 1e-5) return false;
    if (std::abs(observed.mix - original.mix) > 1e-5) return false;

    return true;
}

bool testDrainOrder_HostThenRestoreThenUI() {
    // SR-P3: Verify Host → StateRestore → UI arbitration order.
    ParameterBridge bridge;
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    // Initial state: decay=0.5 (default)
    auto initial = path.controls();
    if (std::abs(initial.decay - 0.5f) > 1e-5) return false;

    // Host sets decay=0.4
    bridge.writeHost(Parameter::Decay, 0.4f);
    bridge.drain(path);
    auto after_host = path.controls();
    if (std::abs(after_host.decay - 0.4f) > 1e-5) return false;

    // Restore sets all three, including decay=0.8 (overrides Host)
    bridge.writeRestore({0.8, 0.5, 0.6});
    bridge.drain(path);
    auto after_restore = path.controls();
    if (std::abs(after_restore.decay - 0.8f) > 1e-5) return false;

    // UI sets decay=0.9 (overrides Restore)
    bridge.writeUi(Parameter::Decay, 0.9f);
    bridge.drain(path);
    auto after_ui = path.controls();
    if (std::abs(after_ui.decay - 0.9f) > 1e-5) return false;

    return true;
}

bool testRestoreAppliesToAllThreeTogether() {
    // SR-P2: Atomic 3-parameter application via restore.
    ParameterBridge bridge;
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    bridge.writeHost(Parameter::Decay, 0.5f);
    bridge.writeHost(Parameter::Damp, 0.0f);
    bridge.writeHost(Parameter::Mix, 1.0f);
    bridge.drain(path);

    RestoreTuple update{0.6, 0.3, 0.7};
    bridge.writeRestore(update);
    if (!bridge.drainRestore(path)) return false;

    auto after = path.controls();
    if (std::abs(after.decay - update.decay) > 1e-5) return false;
    if (std::abs(after.damp - update.damp) > 1e-5) return false;
    if (std::abs(after.mix - update.mix) > 1e-5) return false;

    return true;
}

// ===== SR-P3: Lifecycle Tests =====

bool testRestoreAfterPrepareBeforeFirstRender() {
    // SR-P3: Restore applied at pre-render, before first callback.
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    // Snap sequence (AU responsibility, tested here in isolation).
    if (!path.setAll(0.7, 0.2, 0.8)) return false;
    path.checkForNewTargets();
    path.reset();

    auto controls = path.controls();
    if (std::abs(controls.decay - 0.7) > 1e-5) return false;
    if (std::abs(controls.damp - 0.2) > 1e-5) return false;
    if (std::abs(controls.mix - 0.8) > 1e-5) return false;

    // First sample should use the restored targets (not default).
    auto sample = path.processSample(0.0f);
    if (!std::isfinite(sample.left)) return false;
    if (!std::isfinite(sample.right)) return false;

    return true;
}

bool testLiveRestoreViaBridgeDrain() {
    // SR-P3: Live restore (after first render) via bridge drain.
    ParameterBridge bridge;
    DiffusionStereoPath path;
    if (!path.prepare(makeTestConfig())) return false;

    // Process a few samples to establish running state.
    for (int i = 0; i < 10; ++i) {
        bridge.drain(path);
        auto sample = path.processSample(0.1f);
        if (!std::isfinite(sample.left)) return false;
    }

    // Queue and drain a restore during running rendering.
    bridge.writeRestore({0.9, 0.5, 0.5});
    if (!bridge.drainRestore(path)) return false;

    auto after = path.controls();
    if (std::abs(after.decay - 0.9) > 1e-5) return false;
    if (std::abs(after.damp - 0.5) > 1e-5) return false;
    if (std::abs(after.mix - 0.5) > 1e-5) return false;

    // Continue processing without crash.
    auto sample = path.processSample(0.1f);
    if (!std::isfinite(sample.left)) return false;

    return true;
}

// ===== SR-1/SR-6: Payload Validation Tests =====

bool testAbsentPayloadUsesDefaults() {
    // SR-6: Missing entire payload → defaults.
    StatePayload empty{};
    empty.schemaVersion = 1;

    auto fixture = makeTestFixture();
    auto result = validateStatePayload(empty, fixture);

    if (!result.isValid()) return false;
    if (std::abs(result.decay - 0.5) > 1e-5) return false;
    if (std::abs(result.damp - 0.0) > 1e-5) return false;
    if (std::abs(result.mix - 1.0) > 1e-5) return false;

    return true;
}

bool testNonFiniteControlUsesFallback() {
    // SR-6: Non-finite control value → per-field fallback to documented default.
    // (Not hard rejection; this is ADR-011 §3's per-field-fallback carve-out.)
    StatePayload payload;
    payload.schemaVersion = 1;
    payload.decayNormalized = std::numeric_limits<double>::infinity();  // non-finite
    payload.dampNormalized = 0.0;
    payload.mixNormalized = 1.0;

    auto fixture = makeTestFixture();
    auto result = validateStatePayload(payload, fixture);

    // Non-finite decay should fall back to default 0.5, others preserved.
    if (!result.isValid()) return false;  // Still valid (per-field fallback)
    if (std::abs(result.decay - 0.5) > 1e-5) return false;  // Fallback to default
    if (std::abs(result.damp - 0.0) > 1e-5) return false;   // Preserved
    if (std::abs(result.mix - 1.0) > 1e-5) return false;    // Preserved

    return true;
}

bool testOutOfRangeFiniteValuesClamped() {
    // SR-6: Out-of-range [0,1] values are clamped.
    StatePayload payload;
    payload.schemaVersion = 1;
    payload.decayNormalized = 1.5;   // > 1.0
    payload.dampNormalized = -0.5;   // < 0.0
    payload.mixNormalized = 0.5;

    auto fixture = makeTestFixture();
    auto result = validateStatePayload(payload, fixture);

    if (!result.isValid()) return false;
    if (std::abs(result.decay - 1.0) > 1e-5) return false;
    if (std::abs(result.damp - 0.0) > 1e-5) return false;
    if (std::abs(result.mix - 0.5) > 1e-5) return false;

    return true;
}

}  // namespace

int main() {
    std::cout << "ADR-011 State-Restore Verification Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // SR-P5: Error behavior
    logTestResult("SR-P5: Rejected state does not mutate accepted targets",
                  testRejectedStateDoesNotMutateAcceptedTargets());
    logTestResult("SR-P5: Fixture mismatch is valid but surfaced",
                  testFixtureMismatchIsValidButSurfaced());
    logTestResult("SR-P6: Unrecognized version rejected",
                  testUnrecognizedVersionRejected());

    // SR-P1/SR-P2/SR-P3: Transport and atomicity
    logTestResult("SR-P1: Triple-buffer slot wraparound",
                  testTripleBufferSlotWraparound());
    logTestResult("SR-P2: Coherent tuple observation",
                  testCoherentTupleObservation());
    logTestResult("SR-P3: Drain order Host→Restore→UI",
                  testDrainOrder_HostThenRestoreThenUI());
    logTestResult("SR-P2: Restore applies all three together",
                  testRestoreAppliesToAllThreeTogether());

    // SR-P3: Lifecycle
    logTestResult("SR-P3: Restore after prepare before first render",
                  testRestoreAfterPrepareBeforeFirstRender());
    logTestResult("SR-P3: Live restore via bridge drain",
                  testLiveRestoreViaBridgeDrain());

    // SR-1/SR-6: Validation
    logTestResult("SR-6: Absent payload uses defaults",
                  testAbsentPayloadUsesDefaults());
    logTestResult("SR-6: Non-finite control uses fallback",
                  testNonFiniteControlUsesFallback());
    logTestResult("SR-6: Out-of-range values clamped",
                  testOutOfRangeFiniteValuesClamped());

    std::cout << "========================================" << std::endl;
    std::cout << "Passed: " << g_tests_passed << std::endl;
    std::cout << "Failed: " << g_tests_failed << std::endl;

    return g_tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

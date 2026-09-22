#include "wrapper/StateSchema.h"

#include <algorithm>
#include <cmath>

namespace aetherfield::wrapper {

namespace {

constexpr std::uint32_t kCurrentSchemaVersion = 1;

bool isFinite(double value) noexcept {
    return std::isfinite(value);
}

double clamp01(double value) noexcept {
    return std::min(1.0, std::max(0.0, value));
}

} // namespace

StateRestoreResult validateStatePayload(const StatePayload& payload, const FixtureStamp& currentFixture) noexcept {
    StateRestoreResult result;
    result.currentFixture = currentFixture;

    // Carve-out A (ADR-011 §3): missing or unrecognized schemaVersion → reject entire payload.
    if (payload.schemaVersion != kCurrentSchemaVersion) {
        if (payload.schemaVersion == 0) {
            result.status = StateRestoreResult::Status::MissingVersion;
        } else {
            result.status = StateRestoreResult::Status::UnrecognizedVersion;
        }
        return result;
    }

    // Per-field fallback (ADR-011 §3 point 1):
    // - Absent or non-finite → use documented default
    // - Finite out-of-range → clamp to [0,1]
    result.decay = 0.5;   // default
    result.damp = 0.0;    // default
    result.mix = 1.0;     // default

    // Decay validation and recovery.
    if (isFinite(payload.decayNormalized)) {
        result.decay = clamp01(payload.decayNormalized);
    }
    // else: non-finite → stays at default 0.5

    // Damp validation and recovery.
    if (isFinite(payload.dampNormalized)) {
        result.damp = clamp01(payload.dampNormalized);
    }
    // else: non-finite → stays at default 0.0

    // Mix validation and recovery.
    if (isFinite(payload.mixNormalized)) {
        result.mix = clamp01(payload.mixNormalized);
    }
    // else: non-finite → stays at default 1.0

    // Carve-out B (ADR-011 §3 point 3): fixture-stamp mismatch.
    // Accept the normalized values (they're valid), but surface the mismatch.
    result.savedFixture = {
        payload.fixtureLineCount,
        payload.fixtureMinDelaySeconds,
        payload.fixtureMaxDelaySeconds,
        payload.fixtureSampleRate,
        payload.fixtureDMaxDb
    };

    if (result.savedFixture != currentFixture) {
        result.status = StateRestoreResult::Status::FixtureMismatch;
    } else {
        result.status = StateRestoreResult::Status::Valid;
    }

    return result;
}

bool isPayloadMissing(const StatePayload& payload) noexcept {
    // A completely absent/default payload means no state was ever saved.
    // This is the first-launch case and is not an error.
    // We detect it by checking if all fields are at their "never set" defaults.
    // For version 1, a missing payload is one with schemaVersion == 0.
    return payload.schemaVersion == 0;
}

} // namespace aetherfield::wrapper

#pragma once

#include <cstddef>
#include <cstdint>

namespace aetherfield::wrapper {

// ADR-011 §2: persisted state payload shape and versioning.
// The payload is a flat versioned set of scalars: schemaVersion, three
// normalized control values, and a fixture-identity stamp.
// This record handles version-1 format only; future versions require
// explicit migration chains in their own implementations.

// ADR-011 payload structure (field names illustrative per ADR-011 §2).
struct StatePayload {
    std::uint32_t schemaVersion = 1;
    double decayNormalized = 0.5;
    double dampNormalized = 0.0;
    double mixNormalized = 1.0;
    std::size_t fixtureLineCount = 0;
    double fixtureMinDelaySeconds = 0.0;
    double fixtureMaxDelaySeconds = 0.0;
    double fixtureSampleRate = 0.0;
    double fixtureDMaxDb = 0.0;
};

// Fixture identity stamp for compatibility checking (ADR-011 §2).
// Records the build's fixture parameters so a preset's meaning can be
// verified when loaded under a different fixture.
struct FixtureStamp {
    std::size_t lineCount;
    double minDelaySeconds;
    double maxDelaySeconds;
    double sampleRate;
    double dMaxDb;

    bool operator==(const FixtureStamp& other) const noexcept {
        return lineCount == other.lineCount &&
               minDelaySeconds == other.minDelaySeconds &&
               maxDelaySeconds == other.maxDelaySeconds &&
               sampleRate == other.sampleRate &&
               dMaxDb == other.dMaxDb;
    }

    bool operator!=(const FixtureStamp& other) const noexcept {
        return !(*this == other);
    }
};

// Validation result for a restored state payload (ADR-011 §3).
// Separates parsing, validation, and fixture-identity checks from the
// core's own publication logic. The restore path validates off the render
// thread and delivers only valid, finite, clamped values to setAll().
struct StateRestoreResult {
    enum class Status {
        Valid,                    // All fields valid, fixture matches
        MissingPayload,           // No state saved; use defaults
        MissingVersion,           // schemaVersion absent or invalid; reject
        UnrecognizedVersion,      // schemaVersion newer than reader; reject
        FixtureMismatch,          // Identity stamp mismatch; accept but surface
        InvalidControl,           // A control is non-finite; reject entire payload
    };

    Status status = Status::Valid;
    double decay = 0.5;
    double damp = 0.0;
    double mix = 1.0;
    FixtureStamp currentFixture {};
    FixtureStamp savedFixture {};

    bool isValid() const noexcept {
        return status == Status::Valid || status == Status::FixtureMismatch;
    }

    bool hasMismatch() const noexcept {
        return status == Status::FixtureMismatch;
    }
};

// Payload validator (ADR-011 §3 and Design note).
// Takes a StatePayload, the current fixture, and returns a StateRestoreResult.
// Does NOT call any DSP methods; this runs off the render thread during
// allocation and validates only, leaving the core unchanged on any error.
StateRestoreResult validateStatePayload(const StatePayload& payload, const FixtureStamp& currentFixture) noexcept;

// Check if a payload is entirely absent (no state ever saved).
// This is not an error; it's the normal first-launch case.
bool isPayloadMissing(const StatePayload& payload) noexcept;

} // namespace aetherfield::wrapper

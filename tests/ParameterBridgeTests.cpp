#include "wrapper/ParameterBridge.h"
#include "wrapper/ResetRequest.h"

#include "dsp/DiffusionStereoPath.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <vector>

namespace {

std::atomic_size_t gAllocationCount = 0;

void* allocateOrThrow(std::size_t size) {
    ++gAllocationCount;
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::size_t alignment) {
    ++gAllocationCount;
    const std::size_t requested = size == 0 ? 1 : size;
    const std::size_t remainder = requested % alignment;
    if (requested > std::numeric_limits<std::size_t>::max() - (remainder == 0 ? 0 : alignment - remainder)) {
        throw std::bad_alloc();
    }
    const std::size_t rounded = requested + (remainder == 0 ? 0 : alignment - remainder);
    void* pointer = nullptr;
    if (posix_memalign(&pointer, alignment, rounded) == 0) return pointer;
    throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAlignedOrThrow(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

aetherfield::dsp::DiffusionStereoConfig validConfig(double sampleRate) {
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
        .allpassCoefficient = 0.6180340052,
    };
}

// PB-1: a single Host write, drained once, is applied exactly once.
int testHostWriteAppliedOnDrain() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-1 fixture preparation failed");

    ParameterBridge bridge;
    bridge.writeHost(Parameter::Decay, 0.75F);
    const std::size_t applied = bridge.drain(path);
    if (applied != 1) return fail("PB-1 expected exactly one parameter applied");

    // Confirm it actually reached ParameterAutomation: render past the
    // shortest line's circulation and confirm output differs from the
    // Decay=0.5 default (same technique PT-1 already uses: compare
    // against a reference held at a different setting).
    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> left(impulseInput.size());
    std::vector<float> right(impulseInput.size());
    path.process(impulseInput.data(), left.data(), right.data(), impulseInput.size());

    aetherfield::dsp::DiffusionStereoPath reference;
    if (!reference.prepare(validConfig(48000.0))) return fail("PB-1 reference fixture preparation failed");
    std::vector<float> referenceLeft(impulseInput.size());
    std::vector<float> referenceRight(impulseInput.size());
    reference.process(impulseInput.data(), referenceLeft.data(), referenceRight.data(), impulseInput.size());

    if (left == referenceLeft && right == referenceRight) {
        return fail("PB-1 drained Host write did not change automation state");
    }
    std::cout << "PB-1 Host write applied through drain(): output diverged from Decay=0.5 default\n";
    return 0;
}

// PB-2: ADR-008 section 2's tie-break -- if both Host and UI have a
// pending value for the same parameter at drain time, UI wins, because
// it is scanned second and unconditionally overwrites.
int testUiWinsOverSameDrainHostWrite() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath hostOnly;
    aetherfield::dsp::DiffusionStereoPath both;
    if (!hostOnly.prepare(validConfig(48000.0)) || !both.prepare(validConfig(48000.0))) {
        return fail("PB-2 fixture preparation failed");
    }

    ParameterBridge hostOnlyBridge;
    hostOnlyBridge.writeHost(Parameter::Damp, 0.20F);
    hostOnlyBridge.drain(hostOnly);

    ParameterBridge bothBridge;
    bothBridge.writeHost(Parameter::Damp, 0.20F);
    bothBridge.writeUi(Parameter::Damp, 0.90F);
    const std::size_t applied = bothBridge.drain(both);
    if (applied != 1) return fail("PB-2 expected exactly one parameter applied, not one call per role");

    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> hostOnlyLeft(impulseInput.size());
    std::vector<float> hostOnlyRight(impulseInput.size());
    hostOnly.process(impulseInput.data(), hostOnlyLeft.data(), hostOnlyRight.data(), impulseInput.size());

    std::vector<float> bothLeft(impulseInput.size());
    std::vector<float> bothRight(impulseInput.size());
    both.process(impulseInput.data(), bothLeft.data(), bothRight.data(), impulseInput.size());

    if (hostOnlyLeft == bothLeft && hostOnlyRight == bothRight) {
        return fail("PB-2 UI value did not win: output matches Host-only Damp=0.20, expected UI's 0.90");
    }
    std::cout << "PB-2 UI write won over same-drain Host write, as ADR-008 section 2 requires\n";
    return 0;
}

// PB-3: a Host write already consumed by a prior drain() must not be
// re-applied by a later drain() that has no new pending value -- the
// generation counter, not the mailbox's mere existence, gates
// re-application (mirrors ParameterAutomation::checkForNewTargets()'s
// own "if unchanged since consumedGeneration_, do nothing" contract).
int testConsumedHostWriteIsNotReapplied() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-3 fixture preparation failed");

    ParameterBridge bridge;
    bridge.writeHost(Parameter::Mix, 0.10F);
    if (bridge.drain(path) != 1) return fail("PB-3 first drain should apply exactly one parameter");
    if (bridge.drain(path) != 0) return fail("PB-3 second drain with nothing new pending should apply zero");
    std::cout << "PB-3 a consumed Host write is not reapplied on a subsequent empty drain\n";
    return 0;
}

// PB-4: ADR-008 section 3 -- a burst of N events targeting the same
// parameter within one callback collapses to exactly one mailbox write,
// carrying the LAST event's value, before it ever reaches drain().
int testApplyHostEventsCoalescesToLastValuePerParameter() {
    using aetherfield::wrapper::HostParameterEvent;
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-4 fixture preparation failed");

    const HostParameterEvent events[] = {
        {Parameter::Decay, 0.10F},
        {Parameter::Decay, 0.20F},
        {Parameter::Decay, 0.95F}, // last Decay event in the batch: this one must win
        {Parameter::Mix, 0.40F},
    };

    ParameterBridge bridge;
    applyHostEvents(bridge, events, std::size(events));
    const std::size_t applied = bridge.drain(path);
    if (applied != 2) return fail("PB-4 expected exactly two parameters applied (Decay, Mix), not four");

    aetherfield::dsp::DiffusionStereoPath reference;
    if (!reference.prepare(validConfig(48000.0))) return fail("PB-4 reference fixture preparation failed");
    reference.setDecay(0.95);
    reference.setMix(0.40);

    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> left(impulseInput.size());
    std::vector<float> right(impulseInput.size());
    path.process(impulseInput.data(), left.data(), right.data(), impulseInput.size());

    std::vector<float> referenceLeft(impulseInput.size());
    std::vector<float> referenceRight(impulseInput.size());
    reference.process(impulseInput.data(), referenceLeft.data(), referenceRight.data(), impulseInput.size());

    if (left != referenceLeft || right != referenceRight) {
        return fail("PB-4 coalesced result did not match directly setting the LAST batch values");
    }
    std::cout << "PB-4 applyHostEvents() coalesced a 4-event burst to 2 mailbox writes, last value per parameter\n";
    return 0;
}

// PB-5: a zero-length event batch (a callback in which the host delivered
// no AUParameterEvents at all -- the common case) must not touch any
// mailbox cell or generation counter.
int testApplyHostEventsEmptyBatchIsNoOp() {
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-5 fixture preparation failed");

    ParameterBridge bridge;
    applyHostEvents(bridge, nullptr, 0);
    if (bridge.drain(path) != 0) return fail("PB-5 expected zero parameters applied after an empty batch");
    std::cout << "PB-5 an empty event batch produces zero mailbox writes\n";
    return 0;
}

// PB-6: draining a freshly constructed ParameterBridge, with nothing
// ever written to any cell, must apply zero parameters and must not
// crash or read uninitialized generation state.
int testDrainBeforeAnyWriteIsNoOp() {
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-6 fixture preparation failed");

    ParameterBridge bridge;
    if (bridge.drain(path) != 0) return fail("PB-6 expected zero parameters applied before any write");
    std::cout << "PB-6 draining before any write applies zero parameters\n";
    return 0;
}

// PB-7: sustained writeHost()/writeUi()/drain() calls allocate nothing,
// matching PT-8's existing zero-allocation precedent for the underlying
// setDecay/setDamp/setMix path and ADR-008 section 3's "no allocation"
// render-thread-work bound.
int testNoAllocationDuringWriteAndDrain() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-7 fixture preparation failed");
    ParameterBridge bridge;

    const std::size_t before = gAllocationCount.load();
    for (int i = 0; i < 1000; ++i) {
        bridge.writeHost(Parameter::Decay, 0.01F * static_cast<float>(i % 100));
        bridge.writeUi(Parameter::Damp, 0.01F * static_cast<float>(i % 100));
        bridge.drain(path);
    }
    const std::size_t after = gAllocationCount.load();
    if (after != before) return fail("PB-7 write/drain cycle allocated");
    std::cout << "PB-7 allocation delta through 1000 write/drain cycles: " << (after - before) << '\n';
    return 0;
}

// PB-8: a request from a simulated "other thread" is observed and
// cleared by exactly one consumeIfPending() call; a second call with
// nothing new requested returns false.
int testResetRequestRoundTrip() {
    aetherfield::wrapper::ResetRequest request;
    if (request.consumeIfPending()) return fail("PB-8 expected no pending request before any requestFromAnyThread()");
    request.requestFromAnyThread();
    if (!request.consumeIfPending()) return fail("PB-8 expected a pending request after requestFromAnyThread()");
    if (request.consumeIfPending()) return fail("PB-8 expected the request to be cleared after one consumeIfPending()");
    std::cout << "PB-8 ResetRequest round trip: set once, consumed once, cleared\n";
    return 0;
}

} // namespace

int main() {
    if (testHostWriteAppliedOnDrain() != 0) return 1;
    if (testUiWinsOverSameDrainHostWrite() != 0) return 1;
    if (testConsumedHostWriteIsNotReapplied() != 0) return 1;
    if (testApplyHostEventsCoalescesToLastValuePerParameter() != 0) return 1;
    if (testApplyHostEventsEmptyBatchIsNoOp() != 0) return 1;
    if (testDrainBeforeAnyWriteIsNoOp() != 0) return 1;
    if (testNoAllocationDuringWriteAndDrain() != 0) return 1;
    if (testResetRequestRoundTrip() != 0) return 1;
    std::cout << "ParameterBridge tests passed\n";
    return 0;
}

#include "wrapper/ParameterBridge.h"

namespace aetherfield::wrapper {

ParameterBridge::ParameterBridge() noexcept = default;

void ParameterBridge::writeHost(Parameter parameter, float normalizedValue) noexcept {
    const std::size_t index = static_cast<std::size_t>(parameter);
    hostCells_[index].value.store(normalizedValue, std::memory_order_relaxed);
    hostCells_[index].generation.fetch_add(1, std::memory_order_release);
}

void ParameterBridge::writeUi(Parameter parameter, float normalizedValue) noexcept {
    const std::size_t index = static_cast<std::size_t>(parameter);
    uiCells_[index].value.store(normalizedValue, std::memory_order_relaxed);
    uiCells_[index].generation.fetch_add(1, std::memory_order_release);
}

namespace {

bool applySetter(aetherfield::dsp::DiffusionStereoPath& path, Parameter parameter, float value) noexcept {
    switch (parameter) {
        case Parameter::Decay: return path.setDecay(value);
        case Parameter::Damp: return path.setDamp(value);
        case Parameter::Mix: return path.setMix(value);
    }
    return false;
}

} // namespace

void ParameterBridge::writeRestore(const RestoreTuple& tuple) noexcept {
    // StateRestore is control-thread only, single-writer, never concurrent
    // with itself or with Host/UI writes. Compute the next slot index.
    const std::uint32_t nextSlot = (restoreSlotIndex_.load(std::memory_order_relaxed) + 1) % 3;
    // Write the complete tuple into that slot.
    restoreTuples_[nextSlot] = tuple;
    // Publish the slot index, then release-publish the generation so the
    // reader (drain) sees a change and knows to read the new slot.
    restoreSlotIndex_.store(nextSlot, std::memory_order_relaxed);
    restoreGeneration_.fetch_add(1, std::memory_order_release);
}

bool ParameterBridge::drainRestore(aetherfield::dsp::DiffusionStereoPath& path) noexcept {
    // Check if there's a pending restore (generation changed since last consume).
    const std::uint64_t generation = restoreGeneration_.load(std::memory_order_acquire);
    if (generation == consumedRestoreGeneration_) {
        return false;  // No pending restore
    }
    consumedRestoreGeneration_ = generation;

    // Read the slot index and load the tuple from that slot.
    const std::uint32_t slotIndex = restoreSlotIndex_.load(std::memory_order_relaxed);
    const RestoreTuple tuple = restoreTuples_[slotIndex];

    // Apply the complete tuple atomically via setAll().
    return path.setAll(tuple.decay, tuple.damp, tuple.mix);
}

std::size_t ParameterBridge::drain(aetherfield::dsp::DiffusionStereoPath& path) noexcept {
    std::size_t applied = 0;

    // Per-parameter Host-then-UI arbitration (ADR-008 section 2): for each
    // parameter, load Host generation first, then UI generation. If UI has
    // a pending value, it wins. Apply the winning value exactly once per
    // parameter. StateRestore will be applied separately after this loop.
    for (std::size_t index = 0; index < kParameterCount; ++index) {
        bool hasPending = false;
        float winningValue = 0.0F;

        const std::uint64_t hostGeneration = hostCells_[index].generation.load(std::memory_order_acquire);
        if (hostGeneration != consumedHostGeneration_[index]) {
            winningValue = hostCells_[index].value.load(std::memory_order_relaxed);
            consumedHostGeneration_[index] = hostGeneration;
            hasPending = true;
        }

        // UI is scanned second: if both roles have a pending value this
        // drain, the UI value overwrites winningValue and is what gets
        // applied -- ADR-008 section 2's "a live UI touch always wins
        // over a same-pass Host... value."
        const std::uint64_t uiGeneration = uiCells_[index].generation.load(std::memory_order_acquire);
        if (uiGeneration != consumedUiGeneration_[index]) {
            winningValue = uiCells_[index].value.load(std::memory_order_relaxed);
            consumedUiGeneration_[index] = uiGeneration;
            hasPending = true;
        }

        if (hasPending && applySetter(path, static_cast<Parameter>(index), winningValue)) {
            ++applied;
        }
    }

    // ADR-011 §4 item 2: apply pending StateRestore after Host/UI per-parameter
    // arbitration. This preserves the original Host→UI ordering while inserting
    // StateRestore as a complete atomic tuple: a same-drain UI touch still wins
    // over Host (checked above), and will also overwrite individual restored
    // parameters (UI applied after StateRestore in the checks above). The
    // restore triple itself is consumed atomically here.
    if (drainRestore(path)) {
        ++applied;
    }

    return applied;
}

void applyHostEvents(ParameterBridge& bridge, const HostParameterEvent* events, std::size_t count) noexcept {
    if (count == 0) return;

    bool seen[kParameterCount] = {false, false, false};
    float lastValue[kParameterCount] = {0.0F, 0.0F, 0.0F};

    // One pass, O(count): for each of the three parameters, remember only
    // the LAST event targeting it in this batch (ADR-008 section 3, step
    // 1 -- "overwriting any value already recorded there for this same
    // callback... No atomic operation, no allocation").
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = static_cast<std::size_t>(events[i].parameter);
        seen[index] = true;
        lastValue[index] = events[i].normalizedValue;
    }

    // At most three wait-free mailbox writes total, regardless of count
    // (ADR-008 section 3, step 2).
    for (std::size_t index = 0; index < kParameterCount; ++index) {
        if (seen[index]) {
            bridge.writeHost(static_cast<Parameter>(index), lastValue[index]);
        }
    }
}

} // namespace aetherfield::wrapper

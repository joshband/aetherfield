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

std::size_t ParameterBridge::drain(aetherfield::dsp::DiffusionStereoPath& path) noexcept {
    std::size_t applied = 0;
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

        if (hasPending) {
            applySetter(path, static_cast<Parameter>(index), winningValue);
            ++applied;
        }
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

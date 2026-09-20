#pragma once

#include "dsp/DiffusionStereoPath.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace aetherfield::wrapper {

// The three controls this bridge carries, in the same order and meaning
// as DiffusionStereoPath::setDecay/setDamp/setMix (ADR-004). StateRestore
// is deliberately absent: ADR-011 superseded its mailbox cells with a
// separate atomic triple-buffer, out of scope for this plan.
enum class Parameter : std::size_t { Decay = 0, Damp = 1, Mix = 2 };
inline constexpr std::size_t kParameterCount = 3;

// One (parameter, producer) mailbox cell (ADR-008 section 2): a fixed
// atomic value plus a release-published generation counter, identical in
// shape to ParameterAutomation::publish()'s own transport. Not intended
// for direct use outside ParameterBridge; public only so ParameterBridge
// can be an aggregate of plain members with no heap allocation anywhere.
struct MailboxCell {
    std::atomic<float> value {0.0F};
    std::atomic<std::uint64_t> generation {0};
};

// ADR-008's Bridge Controller state: six fixed mailbox cells (Host x 3,
// UI x 3) and the single-writer drain-and-apply step. writeHost()/writeUi()
// are wait-free and callable from any thread, including the render thread
// (for Host, per ADR-008 section 1's render-thread intake). drain() must
// never be called concurrently with itself -- it is the one Bridge
// Controller task ADR-008 section 1 requires -- and is the only thing in
// this class allowed to call DiffusionStereoPath::setDecay/setDamp/setMix.
class ParameterBridge {
public:
    ParameterBridge() noexcept;

    // Wait-free: one relaxed float store, one release generation
    // fetch_add. Safe from any thread. Safe to call concurrently with
    // drain() and with writes to OTHER (parameter, role) cells. Not safe
    // to call concurrently with another writeHost()/writeUi() call to the
    // SAME (parameter, role) cell -- each role is one logical producer,
    // per ADR-008's own model.
    void writeHost(Parameter parameter, float normalizedValue) noexcept;
    void writeUi(Parameter parameter, float normalizedValue) noexcept;

    // The Bridge Controller's drain-and-apply step (ADR-008 sections 1,
    // 2, 5): for each parameter with a pending value from either role,
    // applies at most one call to the corresponding DiffusionStereoPath
    // setter, using the fixed scan order Host-then-UI so a same-drain UI
    // write always wins over a same-drain Host write (ADR-008 section 2).
    // Must never run concurrently with itself. Returns the number of
    // parameters actually applied (0..3), for test/diagnostic use only.
    std::size_t drain(aetherfield::dsp::DiffusionStereoPath& path) noexcept;

private:
    std::array<MailboxCell, kParameterCount> hostCells_;
    std::array<MailboxCell, kParameterCount> uiCells_;
    std::array<std::uint64_t, kParameterCount> consumedHostGeneration_ {};
    std::array<std::uint64_t, kParameterCount> consumedUiGeneration_ {};
};

// ADR-008 section 3's render-thread-side intake coalescing, factored out
// so it is testable without any Apple type: given a batch of (parameter,
// value) events observed in one render callback, in host-delivered
// (time) order, writes at most one Host mailbox update per distinct
// parameter that appeared, using each parameter's LAST event in the
// batch. The Apple-only render block (src/auv3/, a later task) is
// responsible only for translating AURenderEvent/AUParameterEvent into
// HostParameterEvent and calling this once per callback; it must never
// call writeHost() itself per individual event.
struct HostParameterEvent {
    Parameter parameter;
    float normalizedValue;
};

void applyHostEvents(ParameterBridge& bridge, const HostParameterEvent* events, std::size_t count) noexcept;

} // namespace aetherfield::wrapper

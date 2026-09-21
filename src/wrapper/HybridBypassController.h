#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace aetherfield::wrapper {

enum class HybridBypassAction {
    ProcessLive,
    DrainWithZeros,
    DrainWithZerosThenReset,
    StayStopped,
};

// Separates the render-owned bypass state machine from the control-thread
// computation that supplies conservative silence bounds. No method allocates.
class HybridBypassController {
public:
    void publishSilenceBound(std::size_t samples) noexcept;
    void noteLiveInput(float mono) noexcept;
    HybridBypassAction beginBlock(bool bypassed, std::size_t frames) noexcept;
    bool consumeBypassEnvelope(float& envelope) noexcept;
    float currentBypassEnvelope() const noexcept;
    void resetForHostReset(bool bypassed) noexcept;

private:
    enum class State { Running, Stopped };

    std::atomic<std::size_t> publishedBound_ {static_cast<std::size_t>(-1)};
    std::atomic<std::uint64_t> publishedBoundGeneration_ {0};
    std::uint64_t consumedBoundGeneration_ = 0;
    std::size_t activeBound_ = static_cast<std::size_t>(-1);
    std::size_t elapsed_ = 0;
    State state_ = State::Running;
    bool wasBypassed_ = false;

    std::atomic<float> livePeak_ {0.0F};
    std::atomic<float> bypassEnvelope_ {0.0F};
    std::atomic<std::uint64_t> bypassEnvelopeGeneration_ {0};
    std::uint64_t consumedBypassEnvelopeGeneration_ = 0;
};

} // namespace aetherfield::wrapper

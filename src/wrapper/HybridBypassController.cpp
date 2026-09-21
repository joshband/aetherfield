#include "wrapper/HybridBypassController.h"

#include <cmath>
#include <limits>

namespace aetherfield::wrapper {
namespace {

constexpr std::size_t kInfiniteBound = std::numeric_limits<std::size_t>::max();

std::size_t saturatingAdd(std::size_t left, std::size_t right) noexcept {
    return right > kInfiniteBound - left ? kInfiniteBound : left + right;
}

} // namespace

void HybridBypassController::publishSilenceBound(std::size_t samples) noexcept {
    const std::size_t bounded = samples == 0 ? kInfiniteBound : samples;
    publishedBound_.store(bounded, std::memory_order_relaxed);
    publishedBoundGeneration_.fetch_add(1, std::memory_order_release);
}

void HybridBypassController::noteLiveInput(float mono) noexcept {
    if (!std::isfinite(mono)) return;
    const float magnitude = std::fabs(mono);
    float observed = livePeak_.load(std::memory_order_relaxed);
    while (magnitude > observed
           && !livePeak_.compare_exchange_weak(observed, magnitude, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

HybridBypassAction HybridBypassController::beginBlock(bool bypassed, std::size_t frames) noexcept {
    const std::uint64_t generation = publishedBoundGeneration_.load(std::memory_order_acquire);
    if (generation != consumedBoundGeneration_) {
        activeBound_ = publishedBound_.load(std::memory_order_relaxed);
        consumedBoundGeneration_ = generation;
        if (bypassed && state_ == State::Running) elapsed_ = 0;
    }

    if (!bypassed) {
        wasBypassed_ = false;
        state_ = State::Running;
        return HybridBypassAction::ProcessLive;
    }

    if (!wasBypassed_) {
        bypassEnvelope_.store(livePeak_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        bypassEnvelopeGeneration_.fetch_add(1, std::memory_order_release);
        elapsed_ = 0;
        state_ = State::Running;
        wasBypassed_ = true;
    }
    if (state_ == State::Stopped) return HybridBypassAction::StayStopped;

    elapsed_ = saturatingAdd(elapsed_, frames);
    if (elapsed_ >= activeBound_) {
        state_ = State::Stopped;
        livePeak_.store(0.0F, std::memory_order_relaxed);
        return HybridBypassAction::DrainWithZerosThenReset;
    }
    return HybridBypassAction::DrainWithZeros;
}

bool HybridBypassController::consumeBypassEnvelope(float& envelope) noexcept {
    const std::uint64_t generation = bypassEnvelopeGeneration_.load(std::memory_order_acquire);
    if (generation == consumedBypassEnvelopeGeneration_) return false;
    envelope = bypassEnvelope_.load(std::memory_order_relaxed);
    consumedBypassEnvelopeGeneration_ = generation;
    return true;
}

float HybridBypassController::currentBypassEnvelope() const noexcept {
    return bypassEnvelope_.load(std::memory_order_relaxed);
}

void HybridBypassController::resetForHostReset(bool bypassed) noexcept {
    livePeak_.store(0.0F, std::memory_order_relaxed);
    elapsed_ = 0;
    wasBypassed_ = bypassed;
    state_ = bypassed ? State::Stopped : State::Running;
}

} // namespace aetherfield::wrapper

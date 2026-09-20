#pragma once

#include <atomic>

namespace aetherfield::wrapper {

// ADR-008 section 7: a wrapper-owned wait-free flag closing ADR-010's
// named host-reset-concurrency gap. A host's -reset (any thread) calls
// requestFromAnyThread(). The render thread, and only the render thread,
// calls consumeIfPending() at the very start of every callback, before
// anything else; if it returns true, the render thread (and only the
// render thread) then calls DiffusionStereoPath::reset() itself. This
// class never touches DiffusionStereoPath: it is a pure signal.
class ResetRequest {
public:
    void requestFromAnyThread() noexcept {
        requested_.store(true, std::memory_order_release);
    }

    // Not idempotent to call from multiple threads: only the single
    // render thread may call this, matching every other render-thread-
    // only method in this codebase (ADR-004 (d)'s single-writer/single-
    // reader discipline, generalized here to single-consumer).
    bool consumeIfPending() noexcept {
        return requested_.exchange(false, std::memory_order_acquire);
    }

private:
    std::atomic<bool> requested_ {false};
};

} // namespace aetherfield::wrapper

#pragma once

#include <cstddef>

namespace aetherfield::dsp {

// Stateless in-place gain used solely to prove the Phase 0 DSP loop.
// A null buffer is allowed only when count is zero. Callers provide finite
// sample values and a finite gain whose products are representable as float.
void processGain(float* samples, std::size_t count, float gain) noexcept;

} // namespace aetherfield::dsp

#include "dsp/GainProcessor.h"

namespace aetherfield::dsp {

void processGain(float* samples, std::size_t count, float gain) noexcept {
    if (count == 0) {
        return;
    }

    for (std::size_t index = 0; index < count; ++index) {
        samples[index] *= gain;
    }
}

} // namespace aetherfield::dsp

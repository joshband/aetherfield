#include "dsp/DelayLine.h"

#include <algorithm>
#include <cmath>

namespace aetherfield::dsp {

DelayLine::DelayLine() noexcept = default;

bool DelayLine::prepare(double sampleRate, std::size_t maxDelaySamples) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || maxDelaySamples == 0) {
        return false;
    }

    storage_.assign(maxDelaySamples, 0.0F);
    writePos_ = 0;
    return true;
}

void DelayLine::reset() noexcept {
    std::fill(storage_.begin(), storage_.end(), 0.0F);
    writePos_ = 0;
}

void DelayLine::process(const float* input, float* output, std::size_t count) noexcept {
    if (count == 0) {
        return;
    }

    const std::size_t capacity = storage_.size();
    for (std::size_t index = 0; index < count; ++index) {
        const float incoming = input[index];
        const float delayed = storage_[writePos_];
        storage_[writePos_] = incoming;
        output[index] = delayed;

        ++writePos_;
        if (writePos_ == capacity) {
            writePos_ = 0;
        }
    }
}

} // namespace aetherfield::dsp

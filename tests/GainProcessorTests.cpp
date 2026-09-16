#include "dsp/GainProcessor.h"

#include <array>
#include <cstddef>
#include <cmath>
#include <iostream>

namespace {

bool approximatelyEqual(float actual, float expected) {
    constexpr float tolerance = 0.000001F;
    const float difference = actual - expected;
    return difference >= -tolerance && difference <= tolerance;
}

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    std::array<float, 6> samples {-1.0F, -0.5F, 0.0F, 0.5F, 1.0F, 123.0F};
    aetherfield::dsp::processGain(samples.data(), samples.size() - 1, 0.5F);

    constexpr std::array<float, 5> expected {-0.5F, -0.25F, 0.0F, 0.25F, 0.5F};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (!std::isfinite(samples[index]) || !approximatelyEqual(samples[index], expected[index])) {
            return fail("gain result differs from the deterministic expected value");
        }
    }
    if (!approximatelyEqual(samples.back(), 123.0F)) {
        return fail("processor modified a sample beyond the requested count");
    }

    std::array<float, 3> unity {-0.75F, 0.0F, 0.75F};
    aetherfield::dsp::processGain(unity.data(), unity.size(), 1.0F);
    if (!approximatelyEqual(unity[0], -0.75F) || !approximatelyEqual(unity[1], 0.0F)
        || !approximatelyEqual(unity[2], 0.75F)) {
        return fail("unity gain did not preserve samples");
    }

    std::array<float, 3> muted {-0.75F, 0.0F, 0.75F};
    aetherfield::dsp::processGain(muted.data(), muted.size(), 0.0F);
    if (!approximatelyEqual(muted[0], 0.0F) || !approximatelyEqual(muted[1], 0.0F)
        || !approximatelyEqual(muted[2], 0.0F)) {
        return fail("zero gain did not mute samples");
    }

    constexpr std::array<float, 4> input {-1.0F, -0.25F, 0.25F, 1.0F};
    auto first = input;
    auto second = input;
    aetherfield::dsp::processGain(first.data(), first.size(), 0.5F);
    aetherfield::dsp::processGain(second.data(), second.size(), 0.5F);
    if (first != second) {
        return fail("identical input did not produce identical output");
    }

    aetherfield::dsp::processGain(nullptr, 0, 1.0F);
    return 0;
}

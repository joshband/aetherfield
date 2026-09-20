#include "wrapper/ParameterBridge.h"

#include "dsp/DiffusionStereoPath.h"

#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

aetherfield::dsp::DiffusionStereoConfig validConfig(double sampleRate) {
    return {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = 0.027,
        .fdnMaxDelaySeconds = 0.081,
        .t60ZeroSeconds = 4.0,
        .t60PiSeconds = 4.0,
        .dMaxDb = 48.0,
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = 0.6180340052,
    };
}

// PB-1: a single Host write, drained once, is applied exactly once.
int testHostWriteAppliedOnDrain() {
    using aetherfield::wrapper::Parameter;
    using aetherfield::wrapper::ParameterBridge;

    aetherfield::dsp::DiffusionStereoPath path;
    if (!path.prepare(validConfig(48000.0))) return fail("PB-1 fixture preparation failed");

    ParameterBridge bridge;
    bridge.writeHost(Parameter::Decay, 0.75F);
    const std::size_t applied = bridge.drain(path);
    if (applied != 1) return fail("PB-1 expected exactly one parameter applied");

    // Confirm it actually reached ParameterAutomation: render past the
    // shortest line's circulation and confirm output differs from the
    // Decay=0.5 default (same technique PT-1 already uses: compare
    // against a reference held at a different setting).
    std::vector<float> impulseInput(4096, 0.0F);
    impulseInput[0] = 1.0F;
    std::vector<float> left(impulseInput.size());
    std::vector<float> right(impulseInput.size());
    path.process(impulseInput.data(), left.data(), right.data(), impulseInput.size());

    aetherfield::dsp::DiffusionStereoPath reference;
    if (!reference.prepare(validConfig(48000.0))) return fail("PB-1 reference fixture preparation failed");
    std::vector<float> referenceLeft(impulseInput.size());
    std::vector<float> referenceRight(impulseInput.size());
    reference.process(impulseInput.data(), referenceLeft.data(), referenceRight.data(), impulseInput.size());

    if (left == referenceLeft && right == referenceRight) {
        return fail("PB-1 drained Host write did not change automation state");
    }
    std::cout << "PB-1 Host write applied through drain(): output diverged from Decay=0.5 default\n";
    return 0;
}

} // namespace

int main() {
    if (testHostWriteAppliedOnDrain() != 0) return 1;
    std::cout << "ParameterBridge tests passed\n";
    return 0;
}

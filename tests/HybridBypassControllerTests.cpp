#include "wrapper/HybridBypassController.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

int testBypassDrainsThenStopsExactlyOnce() {
    using aetherfield::wrapper::HybridBypassAction;
    using aetherfield::wrapper::HybridBypassController;

    HybridBypassController controller;
    controller.publishSilenceBound(128);
    if (controller.beginBlock(false, 64) != HybridBypassAction::ProcessLive) {
        return fail("HB-1 live input was not processed before bypass");
    }
    if (controller.beginBlock(true, 64) != HybridBypassAction::DrainWithZeros) {
        return fail("HB-1 bypass did not start a zero-input drain");
    }
    if (controller.beginBlock(true, 64) != HybridBypassAction::DrainWithZerosThenReset) {
        return fail("HB-1 drain did not request exactly one reset at the bound");
    }
    if (controller.beginBlock(true, 64) != HybridBypassAction::StayStopped) {
        return fail("HB-1 stopped tail resumed processing without un-bypass");
    }
    if (controller.beginBlock(false, 64) != HybridBypassAction::ProcessLive) {
        return fail("HB-1 un-bypass did not resume live processing from stopped");
    }
    return 0;
}

int testEnvelopeLatchesOnBypassAndIgnoresNonFinite() {
    using aetherfield::wrapper::HybridBypassController;

    HybridBypassController controller;
    controller.noteLiveInput(0.25F);
    controller.noteLiveInput(-0.75F);
    controller.noteLiveInput(std::numeric_limits<float>::infinity());
    controller.beginBlock(true, 1);

    float envelope = 0.0F;
    if (!controller.consumeBypassEnvelope(envelope) || envelope != 0.75F) {
        return fail("HB-2 bypass did not publish the finite live-input envelope");
    }
    if (controller.consumeBypassEnvelope(envelope)) {
        return fail("HB-2 bypass envelope generation was consumed twice");
    }
    return 0;
}

int testNewBoundRestartsRunningDrain() {
    using aetherfield::wrapper::HybridBypassAction;
    using aetherfield::wrapper::HybridBypassController;

    HybridBypassController controller;
    controller.publishSilenceBound(64);
    controller.beginBlock(true, 32);
    controller.publishSilenceBound(128);
    if (controller.beginBlock(true, 32) != HybridBypassAction::DrainWithZeros) {
        return fail("HB-3 new bound did not restart a running drain");
    }
    if (controller.beginBlock(true, 96) != HybridBypassAction::DrainWithZerosThenReset) {
        return fail("HB-3 restarted drain did not use the newly published bound");
    }
    return 0;
}

} // namespace

int main() {
    if (testBypassDrainsThenStopsExactlyOnce() != 0) return 1;
    if (testEnvelopeLatchesOnBypassAndIgnoresNonFinite() != 0) return 1;
    if (testNewBoundRestartsRunningDrain() != 0) return 1;
    std::cout << "Hybrid bypass controller tests passed\n";
    return 0;
}

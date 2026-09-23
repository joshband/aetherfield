#import "AetherfieldAudioUnit.h"

// <AudioToolbox/AudioToolbox.h> (via AetherfieldAudioUnit.h) only forward-
// declares AVAudioFormat and does not declare AVAudioFrameCount at all --
// both are needed by name below (AVAudioFormat's -sampleRate property,
// and AUInternalRenderBlock's frameCount parameter type spelled out
// explicitly). Real definitions live in AVFoundation/AVFAudio.
#import <AVFoundation/AVFoundation.h>

#include "dsp/DiffusionStereoPath.h"
#include "wrapper/ParameterBridge.h"
#include "wrapper/HybridBypassController.h"
#include "wrapper/ResetRequest.h"
#include "wrapper/StateSchema.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

using aetherfield::dsp::DiffusionStereoConfig;
using aetherfield::dsp::DiffusionStereoPath;
using aetherfield::wrapper::HostParameterEvent;
using aetherfield::wrapper::HybridBypassAction;
using aetherfield::wrapper::HybridBypassController;
using aetherfield::wrapper::Parameter;
using aetherfield::wrapper::ParameterBridge;
using aetherfield::wrapper::ResetRequest;

// Marks _bridgeControllerQueue so -dealloc can detect whether it is
// already executing on that queue (see -dealloc's dispatch_sync guard
// below). The address of this variable itself is the key, per GCD's own
// documented idiom for dispatch_queue_set_specific/dispatch_get_specific.
static const void *const kBridgeControllerQueueKey = &kBridgeControllerQueueKey;

@interface AetherfieldAudioUnit () {
    // Owned C++ state. Constructed once in -initWithComponentDescription:
    // and destroyed in dealloc; NOT reconstructed across a
    // deallocateRenderResources/reallocate cycle (ADR-010's named gap:
    // this plan's chosen resolution is "keep the instance alive," which
    // preserves the cumulative fault counter across that cycle without
    // needing to relocate it anywhere -- the simpler of ADR-010's two
    // named options, and the one that does not require inventing a new
    // wrapper-owned counter).
    std::unique_ptr<DiffusionStereoPath> _path;
    std::unique_ptr<ParameterBridge> _bridge;
    std::unique_ptr<ResetRequest> _resetRequest;
    std::unique_ptr<HybridBypassController> _hybridBypass;

    // The last normalized value this AUParameter was set to, for
    // implementorValueProvider to read back. Not a substitute for
    // ADR-011's ParameterAutomation::getAll() (out of scope here) --
    // just what implementorValueProvider needs to answer host/UI reads
    // without touching the render-thread-only automation state.
    std::atomic<float> _lastDecay;
    std::atomic<float> _lastDamp;
    std::atomic<float> _lastMix;

    AUAudioUnitBus* _inputBus;
    AUAudioUnitBus* _outputBus;
    AUAudioUnitBusArray* _inputBusArray;
    AUAudioUnitBusArray* _outputBusArray;
    AUParameterTree* _parameterTree;

    // ADR-009 section 2 (decided part only): a render-thread-readable
    // snapshot of shouldBypassEffect, kept independent of `self` so the
    // render block never touches an Objective-C property getter (which
    // is not documented as render-thread-safe). Updated only by
    // -setShouldBypassEffect: below, off the render thread.
    std::atomic<bool> _bypassed;

    // ADR-008 section 1's Bridge Controller, online case: a strictly
    // serial dispatch queue, woken on a short fixed polling cadence
    // (plan-level decision, ADR-008's own "Remaining decisions" leaves
    // both the mechanism and the cadence to this plan). 1ms is chosen as
    // comfortably finer than the tightest realistic host block period
    // (128 samples @48kHz is about 2.67ms), keeping publication latency
    // within ADR-008 section 5's "one to a few block periods" estimate
    // without polling so fast it wastes CPU on a background queue.
    dispatch_queue_t _bridgeControllerQueue;
    dispatch_source_t _bridgeControllerTimer;

    // A host-input scratch buffer, allocated once in
    // -allocateRenderResourcesAndReturnError: (off the render thread,
    // matching ADR-003 (d)'s "allocation only at prepare" rule) and
    // freed in -deallocateRenderResources. AURenderPullInputBlock's
    // `inputData` parameter is a plain `AudioBufferList *` that the
    // caller must already own -- it is NOT an out-parameter the callee
    // allocates -- so a buffer with real backing storage must exist
    // before -internalRenderBlock's block can pull input at all.
    // The returned render block does not capture this allocation-owned
    // pointer by value: hosts may cache -internalRenderBlock before
    // allocation. It instead loads the current pointer through the
    // AU-lifetime atomic slot below. Allocation publishes the slot only
    // after all scratch storage exists; deallocation clears it before
    // releasing storage.
    AVAudioPCMBuffer* _inputPCMBuffer;
    std::atomic<AudioBufferList*> _inputBufferList;

    // Mono downmix scratch buffer, sized to maximumFramesToRender and
    // allocated once in -allocateRenderResourcesAndReturnError: (off the
    // render thread). Previously this was a function-local
    // `static thread_local std::vector<float>` that called `.resize()`
    // inside the render block itself -- resize() reallocates whenever
    // the requested size exceeds current capacity, which is guaranteed
    // on the very first callback (capacity starts at 0) and on any
    // callback with a larger frameCount than previously seen, i.e. real
    // heap allocation on the real-time render thread. Fixed by moving
    // allocation here, matching the _inputPCMBuffer pattern exactly: the
    // render block only ever writes into existing capacity, never
    // resizes.
    std::vector<float> _monoScratch;
    std::atomic<float*> _monoScratchData;

    // ADR-011 state restore: a pending restore tuple queued via
    // queueStateRestore(). Control-thread only, never concurrent with
    // the bridge controller. Applied during allocateRenderResourcesAndReturnError
    // (pre-render snap) or drained immediately if already allocated (live restore).
    std::atomic<bool> _hasPendingRestore;
    aetherfield::wrapper::RestoreTuple _pendingRestore;

    // ADR-011 Checkpoint 4: fixture identity for serialization. Captured from
    // the config at prepare time so getState() can build a FixtureStamp without
    // running DSP methods. Control-thread reader (getState, setState);
    // allocation-thread writer (allocateRenderResourcesAndReturnError).
    aetherfield::wrapper::FixtureStamp _currentFixture;
}
@end

@implementation AetherfieldAudioUnit

// ADR-010 lifecycle row: construction is off the render thread, at
// instantiation, not at allocateRenderResourcesAndReturnError:.
- (instancetype)initWithComponentDescription:(AudioComponentDescription)description
                                      options:(AudioComponentInstantiationOptions)options
                                        error:(NSError **)outError {
    NSLog(@"[Aetherfield AU] initWithComponentDescription: start");
    self = [super initWithComponentDescription:description options:options error:outError];
    if (self == nil) {
        NSLog(@"[Aetherfield AU] super initWithComponentDescription failed");
        return nil;
    }
    NSLog(@"[Aetherfield AU] super initWithComponentDescription succeeded");

    @try {
        NSLog(@"[Aetherfield AU] Creating C++ objects...");
        _path = std::make_unique<DiffusionStereoPath>();
        NSLog(@"[Aetherfield AU] Created _path");
        _bridge = std::make_unique<ParameterBridge>();
        NSLog(@"[Aetherfield AU] Created _bridge");
        _resetRequest = std::make_unique<ResetRequest>();
        NSLog(@"[Aetherfield AU] Created _resetRequest");
        _hybridBypass = std::make_unique<HybridBypassController>();
        NSLog(@"[Aetherfield AU] Created _hybridBypass");
    } @catch (NSException *exception) {
        NSLog(@"[Aetherfield AU] Exception creating C++ objects: %@", exception);
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"C++ initialization failed: %@", exception.reason]}];
        }
        return nil;
    } @catch (...) {
        NSLog(@"[Aetherfield AU] Unknown exception creating C++ objects");
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Unknown C++ exception during initialization"}];
        }
        return nil;
    }
    NSLog(@"[Aetherfield AU] Initializing atomic values");
    _lastDecay = 0.5F; // matches DiffusionStereoPath/ParameterAutomation's own prepare()-time default
    _lastDamp = 0.0F;
    _lastMix = 1.0F;
    _bypassed = false;
    _inputBufferList.store(nullptr, std::memory_order_relaxed);
    _monoScratchData.store(nullptr, std::memory_order_relaxed);
    _hasPendingRestore.store(false, std::memory_order_relaxed);
    _pendingRestore = {0.5, 0.0, 1.0};
    _currentFixture = {0, 0.0, 0.0, 0.0, 0.0};
    NSLog(@"[Aetherfield AU] Atomic values initialized");

    NSLog(@"[Aetherfield AU] Creating audio format");
    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000.0 channels:2];
    if (format == nil) {
        NSLog(@"[Aetherfield AU] Failed to create AVAudioFormat");
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Failed to create AVAudioFormat"}];
        }
        return nil;
    }
    NSLog(@"[Aetherfield AU] Audio format created: sampleRate=%.0f channels=%u", format.sampleRate, format.channelCount);

    NSLog(@"[Aetherfield AU] Creating input bus");
    _inputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    NSLog(@"[Aetherfield AU] Creating output bus");
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    // AUAudioUnitBus's initializer can fail and return nil (e.g. an
    // invalid format); inserting nil into an @[...] array literal below
    // would raise an exception. Fail the whole initializer rather than
    // crash -- whatever NSError -initWithFormat:error: already populated
    // into outError is propagated as-is, with a fallback generic error
    // if it left outError untouched.
    if (_inputBus == nil || _outputBus == nil) {
        NSLog(@"[Aetherfield AU] Failed to create AUAudioUnitBus(es): input=%@, output=%@", _inputBus, _outputBus);
        if (outError != nil && *outError == nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Failed to construct AUAudioUnitBus"}];
        }
        return nil;
    }
    NSLog(@"[Aetherfield AU] Buses created successfully");
    // ADR-009 section 1 (decided): stereo-in/stereo-out.
    NSLog(@"[Aetherfield AU] Creating bus arrays");
    _inputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                             busType:AUAudioUnitBusTypeInput
                                                              busses:@[_inputBus]];
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                              busType:AUAudioUnitBusTypeOutput
                                                               busses:@[_outputBus]];
    NSLog(@"[Aetherfield AU] Bus arrays created");

    NSLog(@"[Aetherfield AU] Building parameter tree");
    [self buildParameterTree];
    NSLog(@"[Aetherfield AU] Parameter tree built");

    NSLog(@"[Aetherfield AU] Creating dispatch queue");
    _bridgeControllerQueue = dispatch_queue_create("com.aetherfield.bridgecontroller", DISPATCH_QUEUE_SERIAL);
    if (_bridgeControllerQueue == nullptr) {
        NSLog(@"[Aetherfield AU] Failed to create dispatch queue");
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Failed to create dispatch queue"}];
        }
        return nil;
    }
    NSLog(@"[Aetherfield AU] Dispatch queue created");

    // Marks this queue with kBridgeControllerQueueKey so -dealloc can
    // detect, via dispatch_get_specific, whether it is already executing
    // on this exact queue -- see -dealloc's dispatch_sync guard below.
    dispatch_queue_set_specific(_bridgeControllerQueue, kBridgeControllerQueueKey, (void *)kBridgeControllerQueueKey, NULL);
    NSLog(@"[Aetherfield AU] Queue specific set");

    _bridgeControllerTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _bridgeControllerQueue);
    if (_bridgeControllerTimer == nullptr) {
        NSLog(@"[Aetherfield AU] Failed to create dispatch timer source");
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Failed to create dispatch timer source"}];
        }
        return nil;
    }
    NSLog(@"[Aetherfield AU] Dispatch timer created");

    dispatch_source_set_timer(_bridgeControllerTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               1 * NSEC_PER_MSEC, 0);
    NSLog(@"[Aetherfield AU] Timer configured");

    __weak AetherfieldAudioUnit *weakSelf = self;
    dispatch_source_set_event_handler(_bridgeControllerTimer, ^{
        // ADR-008 section 1: the Bridge Controller drain-and-apply step,
        // online case. Never the render thread; this dispatch queue is
        // strictly serial, so it is never concurrent with itself.
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf != nil && strongSelf->_path != nullptr) {
            const auto before = strongSelf->_path->controls();
            strongSelf->_bridge->drain(*strongSelf->_path);
            const auto after = strongSelf->_path->controls();
            float envelope = 0.0F;
            const bool bypassJustEngaged = strongSelf->_hybridBypass->consumeBypassEnvelope(envelope);
            if (bypassJustEngaged || before.decay != after.decay || before.damp != after.damp) {
                if (!bypassJustEngaged) envelope = strongSelf->_hybridBypass->currentBypassEnvelope();
                const std::size_t bound = envelope == 0.0F ? 1 : strongSelf->_path->silenceBoundSamples(envelope);
                strongSelf->_hybridBypass->publishSilenceBound(bound);
            }
        }
    });
    NSLog(@"[Aetherfield AU] Event handler set");

    dispatch_activate(_bridgeControllerTimer);
    NSLog(@"[Aetherfield AU] Timer activated");

    NSLog(@"[Aetherfield AU] initWithComponentDescription: complete");
    return self;
}

- (void)dealloc {
    if (_bridgeControllerTimer != nullptr) {
        // dispatch_source_cancel only guarantees the event handler will
        // not be invoked AGAIN after cancellation -- Apple's own docs:
        // it "does not interrupt an event handler block that is already
        // in progress." The handler captures weakSelf, resolves
        // strongSelf, and calls strongSelf->_bridge->drain(*strongSelf->_path).
        // _bridge/_path are plain std::unique_ptr members destroyed by
        // the compiler-synthesized .cxx_destruct after this method
        // returns, with no ordering relative to the timer queue
        // otherwise -- a handler that is mid-execution (or already
        // enqueued) when the last strong reference drops could race
        // with, or outlive, _bridge/_path's destruction. Synchronously
        // draining the (strictly serial) queue after cancelling
        // guarantees any in-flight/enqueued invocation completes before
        // the C++ members are torn down.
        dispatch_source_cancel(_bridgeControllerTimer);

        // dispatch_sync onto a queue already executing on IS a deadlock
        // (or an assertion crash on newer libdispatch) -- and this can
        // genuinely happen: if this AU's LAST strong reference is
        // released from inside the timer handler's own weak-to-strong-
        // self resolution above (`strongSelf = weakSelf`), -dealloc runs
        // synchronously, nested inside a block already executing on
        // _bridgeControllerQueue. The dispatch_queue_set_specific mark
        // set at queue-creation time lets us detect exactly that case:
        // if we're already on this queue, any handler invocation that
        // could still be "in flight" IS this very call frame, not a
        // concurrent one, so there is nothing left to wait out and
        // skipping the sync is safe rather than merely convenient. In
        // every other case (dealloc triggered from any other thread or
        // queue) the sync still runs and waits out any in-flight/
        // enqueued handler exactly as before.
        if (dispatch_get_specific(kBridgeControllerQueueKey) == nullptr) {
            dispatch_sync(_bridgeControllerQueue, ^{});
        }
    }
}

// ADR-009 section 1: the three normalized [0,1] AUParameters, addresses
// 0/1/2 matching aetherfield::wrapper::Parameter's own indices 1:1.
// implementorValueObserver is the UI producer role (ADR-008 section 1:
// "a UI or AUParameterObserver write off the render thread") -- every
// AUv3 host's own generic parameter view calls through here with no
// custom Aetherfield UI required.
- (void)buildParameterTree {
    AUParameter *decay = [AUParameterTree createParameterWithIdentifier:@"decay"
                                                                     name:@"Decay"
                                                                  address:(AUParameterAddress)Parameter::Decay
                                                                      min:0.0 max:1.0 unit:kAudioUnitParameterUnit_Generic
                                                                  unitName:nil flags:0 valueStrings:nil dependentParameters:nil];
    AUParameter *damp = [AUParameterTree createParameterWithIdentifier:@"damp"
                                                                    name:@"Damp"
                                                                 address:(AUParameterAddress)Parameter::Damp
                                                                     min:0.0 max:1.0 unit:kAudioUnitParameterUnit_Generic
                                                                 unitName:nil flags:0 valueStrings:nil dependentParameters:nil];
    AUParameter *mix = [AUParameterTree createParameterWithIdentifier:@"mix"
                                                                   name:@"Mix"
                                                                address:(AUParameterAddress)Parameter::Mix
                                                                    min:0.0 max:1.0 unit:kAudioUnitParameterUnit_Generic
                                                                unitName:nil flags:0 valueStrings:nil dependentParameters:nil];
    decay.value = 0.5;
    damp.value = 0.0;
    mix.value = 1.0;

    _parameterTree = [AUParameterTree createTreeWithChildren:@[decay, damp, mix]];

    __weak AetherfieldAudioUnit *weakSelf = self;
    _parameterTree.implementorValueObserver = ^(AUParameter *parameter, AUValue value) {
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf == nil) return;
        const auto target = static_cast<Parameter>(parameter.address);
        strongSelf->_bridge->writeUi(target, value);
        switch (target) {
            case Parameter::Decay: strongSelf->_lastDecay.store(value, std::memory_order_relaxed); break;
            case Parameter::Damp: strongSelf->_lastDamp.store(value, std::memory_order_relaxed); break;
            case Parameter::Mix: strongSelf->_lastMix.store(value, std::memory_order_relaxed); break;
        }
    };
    _parameterTree.implementorValueProvider = ^AUValue(AUParameter *parameter) {
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf == nil) return 0.0F;
        switch (static_cast<Parameter>(parameter.address)) {
            case Parameter::Decay: return strongSelf->_lastDecay.load(std::memory_order_relaxed);
            case Parameter::Damp: return strongSelf->_lastDamp.load(std::memory_order_relaxed);
            case Parameter::Mix: return strongSelf->_lastMix.load(std::memory_order_relaxed);
        }
        return 0.0F;
    };
}

- (AUParameterTree *)parameterTree { return _parameterTree; }
- (AUAudioUnitBusArray *)inputBusses { return _inputBusArray; }
- (AUAudioUnitBusArray *)outputBusses { return _outputBusArray; }

// ADR-010 lifecycle row: allocateRenderResourcesAndReturnError: maps
// onto DiffusionStereoPath::prepare(). On failure, populate NSError and
// return NO; never substitute a different configuration silently
// (ADR-003 (d) rule 7's transactional guarantee, restated at the AU
// boundary by ADR-010).
- (BOOL)allocateRenderResourcesAndReturnError:(NSError **)outError {
    _inputBufferList.store(nullptr, std::memory_order_release);
    _monoScratchData.store(nullptr, std::memory_order_release);
    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;

    // ADR-010 (b): {48kHz, 44.1kHz} only. Anything else must be rejected
    // here, not clamped.
    const double sampleRate = self.outputBusses[0].format.sampleRate;
    if (sampleRate != 48000.0 && sampleRate != 44100.0) {
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FormatNotSupported
                                         userInfo:@{NSLocalizedDescriptionKey: @"Aetherfield supports 48kHz and 44.1kHz only"}];
        }
        [self deallocateRenderResources];
        return NO;
    }

    // ADR-009 section 1 (decided): stereo-in/stereo-out only. Without
    // this check, a host configuring a non-stereo format on either bus
    // would only be discovered later, per-callback, inside the render
    // block's `mNumberBuffers < 2` check -- returning an error mid-stream
    // instead of failing fast at allocate time, contradicting this
    // method's own "never substitute a different configuration silently"
    // principle (ADR-003 (d) rule 7 / ADR-010).
    if (self.inputBusses[0].format.channelCount != 2 || self.outputBusses[0].format.channelCount != 2) {
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FormatNotSupported
                                         userInfo:@{NSLocalizedDescriptionKey: @"Aetherfield requires exactly 2 channels on both busses"}];
        }
        [self deallocateRenderResources];
        return NO;
    }

    DiffusionStereoConfig config {
        .sampleRate = sampleRate,
        .lineCount = 8,
        .fdnMinDelaySeconds = 0.027,
        .fdnMaxDelaySeconds = 0.081,
        .t60ZeroSeconds = 4.0,   // overwritten immediately by the parameter tree's own Decay=0.5 default via ParameterAutomation, not a product value
        .t60PiSeconds = 4.0,
        .dMaxDb = 48.0,          // ADR-004/PT plan's test-fixture value; the product D_max remains a deferred Sonic-acceptance decision
        .inputDelaySeconds = {0.001, 0.00215443, 0.00464159, 0.010},
        .leftOutputDelaySeconds = {0.004, 0.00634960},
        .rightOutputDelaySeconds = {0.00503968, 0.008},
        .allpassCoefficient = 0.6180339887498948482,
    };

    if (!_path->prepare(config)) {
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"DiffusionStereoPath::prepare() failed"}];
        }
        [self deallocateRenderResources];
        return NO;
    }

    // ADR-011 Checkpoint 4: capture fixture identity for serialization.
    _currentFixture = {
        .lineCount = config.lineCount,
        .minDelaySeconds = config.fdnMinDelaySeconds,
        .maxDelaySeconds = config.fdnMaxDelaySeconds,
        .sampleRate = config.sampleRate,
        .dMaxDb = config.dMaxDb,
    };

    // ADR-011 Design note item 3: pre-render snap sequence. If a restore is
    // pending, apply it before the first render callback. The prepare() call
    // has already happened (it's safe to call render-thread methods), and no
    // render callbacks have been issued yet (no concurrent access to contend with).
    if (_hasPendingRestore.exchange(false, std::memory_order_acq_rel)) {
        _path->setAll(_pendingRestore.decay, _pendingRestore.damp, _pendingRestore.mix);
        _path->checkForNewTargets();
        _path->reset();
    }

    // Real backing storage for the render block's pullInputBlock() call
    // -- AURenderPullInputBlock's `inputData` parameter is a caller-owned
    // `AudioBufferList *`, not an out-parameter the pull block allocates,
    // so this must exist before -internalRenderBlock's block can pull
    // anything. Sized to maximumFramesToRender and allocated here, off
    // the render thread, per ADR-003 (d)'s "allocation only at prepare"
    // rule.
    _inputPCMBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:self.inputBusses[0].format
                                                     frameCapacity:self.maximumFramesToRender];
    if (_inputPCMBuffer == nil) {
        if (outError != nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Failed to allocate input scratch buffer"}];
        }
        return NO;
    }

    // Mono downmix scratch, sized once here (off the render thread) --
    // see this buffer's ivar comment for why the render block must never
    // resize it.
    _monoScratch.assign(self.maximumFramesToRender, 0.0F);
    _monoScratchData.store(_monoScratch.data(), std::memory_order_release);
    _inputBufferList.store(_inputPCMBuffer.mutableAudioBufferList,
                           std::memory_order_release);

    return YES;
}

- (void)deallocateRenderResources {
    // Named ADR-010 gap, resolved here: the C++ instance is kept alive
    // across this cycle (not destroyed and reconstructed), so
    // nonFiniteCount() survives exactly as DS-B Task 3 requires, with no
    // need to relocate the counter into wrapper-owned state.
    _inputBufferList.store(nullptr, std::memory_order_release);
    _monoScratchData.store(nullptr, std::memory_order_release);
    _inputPCMBuffer = nil;
    _monoScratch.clear();
    _monoScratch.shrink_to_fit();
    [super deallocateRenderResources];
}

// ADR-008 section 7: host-triggered reset. Never touches _path directly
// -- only sets the wait-free flag. May be called from any thread.
- (void)reset {
    [super reset];
    _resetRequest->requestFromAnyThread();
}

// ADR-011 state restore: queue a restore tuple for application at the next
// allocateRenderResourcesAndReturnError (pre-render snap) or immediately if
// already allocated (live restore via bridge drain). Control-thread only.
- (void)queueStateRestore:(double)decay damp:(double)damp mix:(double)mix {
    _pendingRestore = {decay, damp, mix};
    _hasPendingRestore.store(true, std::memory_order_release);
}

// ADR-011 Checkpoint 4: getState implementation. Returns the persisted state
// payload as a flat NSDictionary with schemaVersion, normalized control values,
// and fixture identity fields. Called off the render thread (host-controlled).
- (NSDictionary *)getState {
    // Capture the last-drained accepted parameter values (not pending restore).
    // _lastDecay/Damp/Mix are atomics written by the bridge drain on every
    // cycle (when values are published to DSP), but not by restore itself -- so
    // getState always serializes what the user hears, never speculative pending
    // changes. This matches SR-P8's decision: save captures drained targets.
    const float decay = _lastDecay.load(std::memory_order_relaxed);
    const float damp = _lastDamp.load(std::memory_order_relaxed);
    const float mix = _lastMix.load(std::memory_order_relaxed);

    // Build the flat state dictionary per SR-P4 format.
    return @{
        @"schemaVersion": @1,
        @"decay": @(decay),
        @"damp": @(damp),
        @"mix": @(mix),
        @"fixtureN": @(_currentFixture.lineCount),
        @"fixtureT_min": @(_currentFixture.minDelaySeconds),
        @"fixtureT_max": @(_currentFixture.maxDelaySeconds),
        @"fixtureF_s": @(_currentFixture.sampleRate),
        @"fixtureD_max": @(_currentFixture.dMaxDb),
    };
}

// ADR-011 Checkpoint 4: setState implementation. Parses the fullState dictionary,
// validates it against the current fixture, and queues a restore if valid.
// Called off the render thread (host-controlled). Per SR-P8, queued restores
// are applied on the next drain cycle (live) or pre-render snap (after dealloc/realloc).
- (void)setState:(NSDictionary *)state {
    // Convert Objective-C dictionary to a C++ StatePayload for validation.
    aetherfield::wrapper::StatePayload payload;

    // Extract schemaVersion (required; missing or wrong type = validation error).
    NSNumber *versionObj = state[@"schemaVersion"];
    if (versionObj == nil || ![versionObj isKindOfClass:[NSNumber class]]) {
        NSLog(@"[Aetherfield] setState: missing or invalid schemaVersion");
        return;
    }
    payload.schemaVersion = [versionObj unsignedIntValue];

    // Extract controls (all optional per ADR-011 §3; missing → default value).
    NSNumber *decayObj = state[@"decay"];
    if (decayObj != nil && [decayObj isKindOfClass:[NSNumber class]]) {
        payload.decayNormalized = [decayObj doubleValue];
    }

    NSNumber *dampObj = state[@"damp"];
    if (dampObj != nil && [dampObj isKindOfClass:[NSNumber class]]) {
        payload.dampNormalized = [dampObj doubleValue];
    }

    NSNumber *mixObj = state[@"mix"];
    if (mixObj != nil && [mixObj isKindOfClass:[NSNumber class]]) {
        payload.mixNormalized = [mixObj doubleValue];
    }

    // Extract fixture identity stamp (all required per ADR-011 §2).
    NSNumber *nObj = state[@"fixtureN"];
    if (nObj != nil && [nObj isKindOfClass:[NSNumber class]]) {
        payload.fixtureLineCount = [nObj unsignedLongValue];
    }

    NSNumber *tMinObj = state[@"fixtureT_min"];
    if (tMinObj != nil && [tMinObj isKindOfClass:[NSNumber class]]) {
        payload.fixtureMinDelaySeconds = [tMinObj doubleValue];
    }

    NSNumber *tMaxObj = state[@"fixtureT_max"];
    if (tMaxObj != nil && [tMaxObj isKindOfClass:[NSNumber class]]) {
        payload.fixtureMaxDelaySeconds = [tMaxObj doubleValue];
    }

    NSNumber *fSObj = state[@"fixtureF_s"];
    if (fSObj != nil && [fSObj isKindOfClass:[NSNumber class]]) {
        payload.fixtureSampleRate = [fSObj doubleValue];
    }

    NSNumber *dMaxObj = state[@"fixtureD_max"];
    if (dMaxObj != nil && [dMaxObj isKindOfClass:[NSNumber class]]) {
        payload.fixtureDMaxDb = [dMaxObj doubleValue];
    }

    // Validate and handle the payload per ADR-011 §3.
    auto result = aetherfield::wrapper::validateStatePayload(payload, _currentFixture);

    if (!result.isValid()) {
        // Hard reject (version mismatch, non-finite control). Per ADR-011,
        // log the rejection but do not queue a restore; current state remains unchanged.
        switch (result.status) {
            case aetherfield::wrapper::StateRestoreResult::Status::MissingVersion:
                NSLog(@"[Aetherfield] setState: missing or invalid schemaVersion");
                break;
            case aetherfield::wrapper::StateRestoreResult::Status::UnrecognizedVersion:
                NSLog(@"[Aetherfield] setState: newer schemaVersion=%u not supported", payload.schemaVersion);
                break;
            case aetherfield::wrapper::StateRestoreResult::Status::InvalidControl:
                NSLog(@"[Aetherfield] setState: non-finite control value in restored state");
                break;
            default:
                NSLog(@"[Aetherfield] setState: validation failed (status=%d)", static_cast<int>(result.status));
                break;
        }
        return;
    }

    // Valid payload (or FixtureMismatch, which is accepted per ADR-011).
    if (result.hasMismatch()) {
        // Log mismatch per SR-P6 decision (no new notification channel).
        NSLog(@"[Aetherfield] Loaded state from incompatible fixture (loaded N=%zu, current N=%zu); normalized values preserved",
              result.savedFixture.lineCount, result.currentFixture.lineCount);
    }

    // Queue the validated, normalized restore tuple (with clamping applied).
    [self queueStateRestore:result.decay damp:result.damp mix:result.mix];
}

// ADR-009 section 2 (decided part only): the host sets this off the
// render thread (Apple documents shouldBypassEffect as host-settable,
// not render-thread-settable). Updating the atomic snapshot here, rather
// than reading self.shouldBypassEffect from inside the render block, is
// what makes the render block's bypass check render-thread-safe.
//
// ASSUMPTION, unverified: this override's correctness depends on
// AUAudioUnit routing every write to shouldBypassEffect through this
// Objective-C setter -- including host-initiated ones -- rather than
// through some other internal path this override wouldn't observe.
// Apple documents shouldBypassEffect as a normal, KVO-compliant,
// host-settable property, which is consistent with that assumption, but
// AUAudioUnit's internals are private and this cannot be confirmed by
// source-level review of this codebase alone. Verify with logging (or
// an equivalent on-device check) on the first real host/device test
// (ADR-012 HT-5) that a host's bypass toggle actually reaches here.
- (void)setShouldBypassEffect:(BOOL)shouldBypassEffect {
    [super setShouldBypassEffect:shouldBypassEffect];
    _bypassed.store(shouldBypassEffect, std::memory_order_relaxed);
}

- (AUInternalRenderBlock)internalRenderBlock {
    // Captured by value into the block: raw pointers into AU-lifetime C++
    // state. The block must not touch `self` or Objective-C objects on the
    // render thread.
    DiffusionStereoPath *path = _path.get();
    ParameterBridge *bridge = _bridge.get();
    ResetRequest *resetRequest = _resetRequest.get();
    HybridBypassController *hybridBypass = _hybridBypass.get();
    std::atomic<bool> *bypassed = &_bypassed;
    std::atomic<AudioBufferList*> *inputBufferListSlot = &_inputBufferList;
    std::atomic<float*> *monoScratchDataSlot = &_monoScratchData;
    // The block captures only pointers to AU-lifetime atomic slots, never
    // Objective-C objects or allocation-lifetime resource pointers. This
    // permits a host to fetch/cache the block before resource allocation.

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags *actionFlags,
                               const AudioTimeStamp *timestamp,
                               AVAudioFrameCount frameCount,
                               NSInteger outputBusNumber,
                               AudioBufferList *outputData,
                               const AURenderEvent *realtimeEventListHead,
                               AURenderPullInputBlock pullInputBlock) {
        // ADR-008 section 7, first: drain any pending host-triggered
        // reset, unconditionally, before anything else -- including
        // before the frameCount == 0 check, since reset() itself takes
        // no frame count and there is no reason to delay a host's
        // explicit request past an empty callback.
        if (resetRequest->consumeIfPending()) {
            path->reset();
            hybridBypass->resetForHostReset(bypassed->load(std::memory_order_relaxed));
        }

        // ADR-008 section 3: scan the host event list exactly once,
        // collapsing to at most one HostParameterEvent per parameter,
        // then hand off to the portable, unit-tested coalescing logic.
        std::array<HostParameterEvent, aetherfield::wrapper::kParameterCount> events {};
        std::array<bool, aetherfield::wrapper::kParameterCount> seen {};
        for (const AURenderEvent *event = realtimeEventListHead; event != nullptr; event = event->head.next) {
            if (event->head.eventType != AURenderEventParameter) continue;
            Parameter parameter;
            switch (event->parameter.parameterAddress) {
                case static_cast<AUParameterAddress>(Parameter::Decay): parameter = Parameter::Decay; break;
                case static_cast<AUParameterAddress>(Parameter::Damp): parameter = Parameter::Damp; break;
                case static_cast<AUParameterAddress>(Parameter::Mix): parameter = Parameter::Mix; break;
                default: continue;
            }
            // rampDurationSampleFrames is read (implicitly, by iterating
            // the event) and discarded -- ADR-008 section 4: "not
            // honored, not stored, not forwarded."
            const std::size_t index = static_cast<std::size_t>(parameter);
            events[index] = {parameter, event->parameter.value};
            seen[index] = true;
        }
        std::array<HostParameterEvent, aetherfield::wrapper::kParameterCount> compacted {};
        std::size_t compactedCount = 0;
        for (std::size_t index = 0; index < seen.size(); ++index) {
            if (seen[index]) compacted[compactedCount++] = events[index];
        }
        aetherfield::wrapper::applyHostEvents(*bridge, compacted.data(), compactedCount);

        // Offline/deterministic-adjacent note: this dispatch is always
        // the "online" path (a real internalRenderBlock invocation from
        // a host). The Bridge Controller's SEPARATE synchronous offline
        // drain (ADR-008 section 1's other half) applies only to this
        // project's own render tools calling DiffusionStereoPath
        // directly, not to anything reachable through this block.

        if (frameCount == 0) return noErr;

        AudioBufferList *inputBufferList =
            inputBufferListSlot->load(std::memory_order_acquire);
        float *monoScratch = monoScratchDataSlot->load(std::memory_order_acquire);
        if (inputBufferList == nullptr || monoScratch == nullptr) {
            return kAudioUnitErr_Uninitialized;
        }

        // Tell the pull block exactly how many bytes of our preallocated
        // buffer are being requested this callback (frameCount is
        // host-variable, up to the maximumFramesToRender capacity
        // _inputPCMBuffer was sized for at allocation time).
        for (UInt32 i = 0; i < inputBufferList->mNumberBuffers; ++i) {
            inputBufferList->mBuffers[i].mDataByteSize = frameCount * sizeof(float);
        }
        AudioUnitRenderActionFlags pullFlags = 0;
        const OSStatus pullStatus = pullInputBlock(&pullFlags, timestamp, frameCount, 0, inputBufferList);
        if (pullStatus != noErr || inputBufferList->mNumberBuffers < 2) {
            return pullStatus != noErr ? pullStatus : kAudioUnitErr_NoConnection;
        }
        // Mirrors the inputBufferList check above: outputData is host-
        // supplied and its buffer count is not otherwise validated
        // before dereferencing mBuffers[0]/[1] below.
        if (outputData == nullptr || outputData->mNumberBuffers < 2) {
            return kAudioUnitErr_NoConnection;
        }

        const float *inputLeft = static_cast<const float *>(inputBufferList->mBuffers[0].mData);
        const float *inputRight = static_cast<const float *>(inputBufferList->mBuffers[1].mData);
        float *outputLeft = static_cast<float *>(outputData->mBuffers[0].mData);
        float *outputRight = static_cast<float *>(outputData->mBuffers[1].mData);

        // ADR-009 section 2 (decided part only): bit-exact dry
        // passthrough on bypass. The wet path is NOT skipped/paused here
        // -- this is the always-safe, unoptimized fallback ADR-009
        // itself names as the alternative to the still-unimplemented
        // T_silence-bounded hybrid mechanism (see Non-goals). Reading
        // the atomic snapshot, never self.shouldBypassEffect, keeps this
        // check render-thread-safe.
        const bool bypassedNow = bypassed->load(std::memory_order_relaxed);
        const HybridBypassAction bypassAction = hybridBypass->beginBlock(bypassedNow, frameCount);
        if (bypassedNow) {
            if (bypassAction == HybridBypassAction::DrainWithZeros
                || bypassAction == HybridBypassAction::DrainWithZerosThenReset) {
                std::fill_n(monoScratch, frameCount, 0.0F);
                path->process(monoScratch, outputLeft, outputRight, frameCount);
                if (bypassAction == HybridBypassAction::DrainWithZerosThenReset) path->reset();
            }
            std::copy_n(inputLeft, frameCount, outputLeft);
            std::copy_n(inputRight, frameCount, outputRight);
            return noErr;
        }

        // ADR-009 section 1 (decided): sum-to-mono reduction feeding the
        // existing unmodified mono DiffusionStereoPath. Writes only into
        // monoScratch's existing (preallocated) capacity, up to
        // frameCount elements -- never resizes/reallocates on this
        // thread; see monoScratch's capture-site comment above.
        for (AVAudioFrameCount i = 0; i < frameCount; ++i) {
            monoScratch[i] = 0.5F * (inputLeft[i] + inputRight[i]);
            hybridBypass->noteLiveInput(monoScratch[i]);
        }
        path->process(monoScratch, outputLeft, outputRight, frameCount);

        return noErr;
    };
}

@end

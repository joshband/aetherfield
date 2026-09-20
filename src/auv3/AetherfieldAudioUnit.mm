#import "AetherfieldAudioUnit.h"

// <AudioToolbox/AudioToolbox.h> (via AetherfieldAudioUnit.h) only forward-
// declares AVAudioFormat and does not declare AVAudioFrameCount at all --
// both are needed by name below (AVAudioFormat's -sampleRate property,
// and AUInternalRenderBlock's frameCount parameter type spelled out
// explicitly). Real definitions live in AVFoundation/AVFAudio.
#import <AVFoundation/AVFoundation.h>

#include "dsp/DiffusionStereoPath.h"
#include "wrapper/ParameterBridge.h"
#include "wrapper/ResetRequest.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

using aetherfield::dsp::DiffusionStereoConfig;
using aetherfield::dsp::DiffusionStereoPath;
using aetherfield::wrapper::HostParameterEvent;
using aetherfield::wrapper::Parameter;
using aetherfield::wrapper::ParameterBridge;
using aetherfield::wrapper::ResetRequest;

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
    // -internalRenderBlock extracts this object's raw
    // `mutableAudioBufferList` pointer once, before constructing the
    // block, and captures only that C pointer by value (never the
    // AVAudioPCMBuffer object itself) into the block, matching this
    // file's existing pattern of capturing raw pointers for
    // render-thread use.
    //
    // ASSUMPTION, unverified by this task (out of scope -- no host/device
    // test has run): unlike _path/_bridge/_resetRequest, this buffer is
    // NOT kept alive across a deallocateRenderResources/reallocate cycle
    // -- it is nil'd in -deallocateRenderResources and only reallocated
    // by the next -allocateRenderResourcesAndReturnError:. The raw
    // AudioBufferList* captured into -internalRenderBlock's returned
    // block therefore becomes stale across that cycle. This relies on
    // the standard AUv3 host contract that a host re-fetches
    // -internalRenderBlock after any reconfiguration (deallocate/
    // reallocate) rather than reusing a block obtained before it; if a
    // host ever violated that contract, this pointer would dangle. Needs
    // confirming on first real host/device test (see ADR-012's HT-1/HT-2).
    AVAudioPCMBuffer* _inputPCMBuffer;

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
}
@end

@implementation AetherfieldAudioUnit

// ADR-010 lifecycle row: construction is off the render thread, at
// instantiation, not at allocateRenderResourcesAndReturnError:.
- (instancetype)initWithComponentDescription:(AudioComponentDescription)description
                                      options:(AudioComponentInstantiationOptions)options
                                        error:(NSError **)outError {
    self = [super initWithComponentDescription:description options:options error:outError];
    if (self == nil) return nil;

    _path = std::make_unique<DiffusionStereoPath>();
    _bridge = std::make_unique<ParameterBridge>();
    _resetRequest = std::make_unique<ResetRequest>();
    _lastDecay = 0.5F; // matches DiffusionStereoPath/ParameterAutomation's own prepare()-time default
    _lastDamp = 0.0F;
    _lastMix = 1.0F;
    _bypassed = false;

    AVAudioFormat *format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000.0 channels:2];
    _inputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    // AUAudioUnitBus's initializer can fail and return nil (e.g. an
    // invalid format); inserting nil into an @[...] array literal below
    // would raise an exception. Fail the whole initializer rather than
    // crash -- whatever NSError -initWithFormat:error: already populated
    // into outError is propagated as-is, with a fallback generic error
    // if it left outError untouched.
    if (_inputBus == nil || _outputBus == nil) {
        if (outError != nil && *outError == nil) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                             code:kAudioUnitErr_FailedInitialization
                                         userInfo:@{NSLocalizedDescriptionKey: @"Failed to construct AUAudioUnitBus"}];
        }
        return nil;
    }
    // ADR-009 section 1 (decided): stereo-in/stereo-out.
    _inputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                             busType:AUAudioUnitBusTypeInput
                                                              busses:@[_inputBus]];
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                              busType:AUAudioUnitBusTypeOutput
                                                               busses:@[_outputBus]];

    [self buildParameterTree];

    _bridgeControllerQueue = dispatch_queue_create("com.aetherfield.bridgecontroller", DISPATCH_QUEUE_SERIAL);
    _bridgeControllerTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _bridgeControllerQueue);
    dispatch_source_set_timer(_bridgeControllerTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               1 * NSEC_PER_MSEC, 0);
    __weak AetherfieldAudioUnit *weakSelf = self;
    dispatch_source_set_event_handler(_bridgeControllerTimer, ^{
        // ADR-008 section 1: the Bridge Controller drain-and-apply step,
        // online case. Never the render thread; this dispatch queue is
        // strictly serial, so it is never concurrent with itself.
        AetherfieldAudioUnit *strongSelf = weakSelf;
        if (strongSelf != nil && strongSelf->_path != nullptr) {
            strongSelf->_bridge->drain(*strongSelf->_path);
        }
    });
    dispatch_activate(_bridgeControllerTimer);

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
        // the C++ members are torn down. This cannot deadlock: dealloc
        // never runs on _bridgeControllerQueue itself.
        dispatch_source_cancel(_bridgeControllerTimer);
        dispatch_sync(_bridgeControllerQueue, ^{});
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
        return NO;
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

    return YES;
}

- (void)deallocateRenderResources {
    // Named ADR-010 gap, resolved here: the C++ instance is kept alive
    // across this cycle (not destroyed and reconstructed), so
    // nonFiniteCount() survives exactly as DS-B Task 3 requires, with no
    // need to relocate the counter into wrapper-owned state.
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
    // Captured by value into the block: raw pointers into state this
    // object owns for its own lifetime, matching Apple's own documented
    // pattern for internalRenderBlock (the block must not touch `self`
    // or any Objective-C object on the render thread).
    DiffusionStereoPath *path = _path.get();
    ParameterBridge *bridge = _bridge.get();
    ResetRequest *resetRequest = _resetRequest.get();
    std::atomic<bool> *bypassed = &_bypassed;
    // AURenderPullInputBlock's `inputData` parameter is a caller-owned
    // AudioBufferList* the callee fills in, not an out-parameter the
    // callee allocates -- so the block needs real backing storage,
    // provided by _inputPCMBuffer (allocated once in
    // -allocateRenderResourcesAndReturnError:, see its ivar comment
    // above). Only the raw C AudioBufferList* is captured here, matching
    // this file's existing pattern of capturing raw pointers rather than
    // Objective-C objects for render-thread use.
    //
    // ASSUMPTION, unverified (see _inputPCMBuffer's ivar comment): this
    // raw pointer is only valid until the next
    // deallocateRenderResources/reallocate cycle, since _inputPCMBuffer
    // (unlike _path/_bridge/_resetRequest) is NOT kept alive across one.
    // Relies on the host re-fetching -internalRenderBlock after any
    // reconfiguration rather than reusing a block obtained beforehand.
    AudioBufferList *inputBufferList = _inputPCMBuffer.mutableAudioBufferList;
    // Mono downmix scratch: raw pointer into _monoScratch, sized once in
    // -allocateRenderResourcesAndReturnError: (see its ivar comment) --
    // the render block below only ever writes into existing capacity, up
    // to frameCount elements, and never resizes.
    float *monoScratch = _monoScratch.data();

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
        }

        // ADR-008 section 3: scan the host event list exactly once,
        // collapsing to at most one HostParameterEvent per parameter,
        // then hand off to the portable, unit-tested coalescing logic.
        std::vector<HostParameterEvent> events;
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
            events.push_back({parameter, event->parameter.value});
        }
        aetherfield::wrapper::applyHostEvents(*bridge, events.data(), events.size());

        // Offline/deterministic-adjacent note: this dispatch is always
        // the "online" path (a real internalRenderBlock invocation from
        // a host). The Bridge Controller's SEPARATE synchronous offline
        // drain (ADR-008 section 1's other half) applies only to this
        // project's own render tools calling DiffusionStereoPath
        // directly, not to anything reachable through this block.

        if (frameCount == 0) return noErr;

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
        if (bypassed->load(std::memory_order_relaxed)) {
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
        }
        path->process(monoScratch, outputLeft, outputRight, frameCount);

        return noErr;
    };
}

@end

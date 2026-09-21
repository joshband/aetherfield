// Minimal AVAudioUnitComponentManager discovery + AUAudioUnit instantiation
// harness (2026-09-20). This is evidence-gathering only, not a shipping
// test: it exercises the real host-facing discovery/instantiation path a
// commercial AU host (AUM, Cubasis, Logic) actually uses, one level beyond
// `pluginkit -m`'s registration-only confirmation (docs/testing.md,
// "Bounded repair applied and verified"). The first test method does NOT
// allocate render resources, render audio, exercise parameters, or touch
// lifecycle beyond instantiate/deallocate. The second test method (added
// same day) goes one step further -- format negotiation, resource
// allocation, and exactly one render call -- but still does NOT constitute
// HT-1 or HT-3: no repetition, no stress sizes, no fixed/ragged partition
// sweep, no reference-signal correctness check, no physical device, no
// multiple instances, no parameter automation, no lifecycle/reset/fault
// exercise. See each test method's own comment for its exact scope.
// (docs/phases/phase1-host-device-acceptance-plan.md HT-1, HT-3, HT-9,
// HT-11 all remain separate, unrun categories.)
// Component identifiers below must match platform/apple/project.yml's
// AudioComponents registration exactly (type/subtype/manufacturer), which
// are themselves explicit placeholders, not a product commitment.
#import <XCTest/XCTest.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <cmath>

@interface AetherfieldComponentInstantiationTests : XCTestCase
@end

@implementation AetherfieldComponentInstantiationTests

// Shared by both test methods: discover the registered component and
// instantiate it out-of-process, the same way a real host would. Returns
// nil (after failing an assertion) if either step did not succeed.
- (AUAudioUnit *)instantiateAetherfieldAudioUnit {
  AudioComponentDescription targetDescription = {0};
  targetDescription.componentType = kAudioUnitType_Effect;  // 'aufx'
  targetDescription.componentSubType = 'Aeth';
  targetDescription.componentManufacturer = 'Josh';
  targetDescription.componentFlags = 0;
  targetDescription.componentFlagsMask = 0;

  AVAudioUnitComponentManager *manager =
      [AVAudioUnitComponentManager sharedAudioUnitComponentManager];
  NSArray<AVAudioUnitComponent *> *matches =
      [manager componentsMatchingDescription:targetDescription];

  XCTAssertGreaterThanOrEqual(
      matches.count, 1U,
      @"Expected the Aetherfield AU extension to be discoverable via "
      @"AVAudioUnitComponentManager on this simulator/device -- is "
      @"AetherfieldHost installed?");
  if (matches.count == 0) {
    return nil;
  }

  AVAudioUnitComponent *component = matches.firstObject;

  XCTestExpectation *instantiationExpectation =
      [self expectationWithDescription:@"AUAudioUnit instantiation"];
  __block AUAudioUnit *instantiatedUnit = nil;
  __block NSError *instantiationError = nil;

  [AUAudioUnit
      instantiateWithComponentDescription:component.audioComponentDescription
                                   options:kAudioComponentInstantiation_LoadOutOfProcess
                         completionHandler:^(AUAudioUnit *_Nullable audioUnit,
                                              NSError *_Nullable error) {
                           instantiatedUnit = audioUnit;
                           instantiationError = error;
                           [instantiationExpectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:15.0 handler:nil];

  XCTAssertNil(instantiationError, @"AUAudioUnit instantiation failed: %@",
               instantiationError);
  XCTAssertNotNil(instantiatedUnit, @"Expected a non-nil AUAudioUnit instance");
  return instantiatedUnit;
}

- (void)testDiscoverAndInstantiateAetherfieldAudioUnit {
  AudioComponentDescription targetDescription = {0};
  targetDescription.componentType = kAudioUnitType_Effect;  // 'aufx'
  targetDescription.componentSubType = 'Aeth';
  targetDescription.componentManufacturer = 'Josh';
  targetDescription.componentFlags = 0;
  targetDescription.componentFlagsMask = 0;

  AVAudioUnitComponentManager *manager =
      [AVAudioUnitComponentManager sharedAudioUnitComponentManager];
  NSArray<AVAudioUnitComponent *> *matches =
      [manager componentsMatchingDescription:targetDescription];

  XCTAssertGreaterThanOrEqual(
      matches.count, 1U,
      @"Expected the Aetherfield AU extension to be discoverable via "
      @"AVAudioUnitComponentManager on this simulator/device -- is "
      @"AetherfieldHost installed?");
  if (matches.count == 0) {
    return;
  }

  AVAudioUnitComponent *component = matches.firstObject;
  NSLog(@"[AetherfieldHarness] discovered component: name=%@ manufacturerName=%@ "
        @"version=%ld",
        component.name, component.manufacturerName, (long)component.version);

  XCTestExpectation *instantiationExpectation =
      [self expectationWithDescription:@"AUAudioUnit instantiation"];
  __block AUAudioUnit *instantiatedUnit = nil;
  __block NSError *instantiationError = nil;

  [AUAudioUnit
      instantiateWithComponentDescription:component.audioComponentDescription
                                   options:kAudioComponentInstantiation_LoadOutOfProcess
                         completionHandler:^(AUAudioUnit *_Nullable audioUnit,
                                              NSError *_Nullable error) {
                           instantiatedUnit = audioUnit;
                           instantiationError = error;
                           [instantiationExpectation fulfill];
                         }];

  [self waitForExpectationsWithTimeout:15.0 handler:nil];

  XCTAssertNil(instantiationError, @"AUAudioUnit instantiation failed: %@",
               instantiationError);
  XCTAssertNotNil(instantiatedUnit, @"Expected a non-nil AUAudioUnit instance");
  if (instantiatedUnit == nil) {
    return;
  }

  NSLog(@"[AetherfieldHarness] instantiated: class=%@ audioUnitName=%@ "
        @"manufacturerName=%@ componentName=%@",
        NSStringFromClass([instantiatedUnit class]),
        instantiatedUnit.audioUnitName, instantiatedUnit.manufacturerName,
        instantiatedUnit.componentName);

  // Out-of-process AUv3 instances deallocate asynchronously; give the XPC
  // teardown a moment rather than asserting anything about its timing.
  instantiatedUnit = nil;
}

// One step past discovery/instantiation: cache the AU's render block before
// allocation, then render through that same block before and after a resource
// reallocation. This is still NOT HT-1 or HT-3: one instance, two calls, one
// block size, no repetition, no fixed/ragged
// partition sweep, no comparison against a reference signal (there is no
// oracle here -- silence in is not asserted to produce any particular
// output, only *a* plausibly-shaped output with a benign status), no
// physical device. 48 kHz / stereo matches this project's only measured
// rates (ADR-005/ADR-010) and ADR-009's decided stereo-in/stereo-out
// production bus. 512 frames is drawn from the fixed partition
// `{1,13,64,512,3}` this project has actually exercised elsewhere
// (docs/phases/phase1-host-device-acceptance-plan.md HT-3;
// docs/testing.md DS-11), not invented for this harness.
- (void)testCachedRenderBlockSurvivesResourceReallocation {
  AUAudioUnit *instantiatedUnit = [self instantiateAetherfieldAudioUnit];
  if (instantiatedUnit == nil) {
    return;
  }

  const double sampleRate = 48000.0;
  const AVAudioChannelCount channelCount = 2;
  const AVAudioFrameCount frameCount = 512;

  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sampleRate
                                                      channels:channelCount];
  XCTAssertNotNil(format, @"Failed to construct a 48 kHz stereo AVAudioFormat");
  if (format == nil) {
    return;
  }

  NSError *inputFormatError = nil;
  AUAudioUnitBus *inputBus = instantiatedUnit.inputBusses[0];
  BOOL inputFormatSet = [inputBus setFormat:format error:&inputFormatError];
  XCTAssertTrue(inputFormatSet, @"Failed to set input bus format: %@",
                inputFormatError);

  NSError *outputFormatError = nil;
  AUAudioUnitBus *outputBus = instantiatedUnit.outputBusses[0];
  BOOL outputFormatSet = [outputBus setFormat:format error:&outputFormatError];
  XCTAssertTrue(outputFormatSet, @"Failed to set output bus format: %@",
                outputFormatError);

  if (!inputFormatSet || !outputFormatSet) {
    return;
  }

  instantiatedUnit.maximumFramesToRender = frameCount;

  // AUv3 hosts may obtain and retain this closure before resources are
  // allocated. Keep it for both render calls below; replacing it after either
  // allocation would fail to exercise the repaired lifecycle contract.
  AURenderBlock renderBlock = instantiatedUnit.renderBlock;
  XCTAssertNotNil(renderBlock, @"AUAudioUnit exposed a nil renderBlock before "
                                @"resource allocation");
  if (renderBlock == nil) {
    return;
  }

  NSError *allocationError = nil;
  BOOL allocated =
      [instantiatedUnit allocateRenderResourcesAndReturnError:&allocationError];
  NSLog(@"[AetherfieldHarness] allocateRenderResourcesAndReturnError: "
        @"succeeded=%d error=%@",
        allocated, allocationError);
  XCTAssertTrue(allocated, @"allocateRenderResourcesAndReturnError: failed: %@",
                allocationError);
  if (!allocated) {
    // Per this project's own DS-B/ADR-003 evidence discipline: a failed
    // allocation is a reportable finding on its own, not something to
    // route around by retrying or relaxing the format/rate request.
    return;
  }

  AUAudioFrameCount renderFrameCount = frameCount;
  AudioBufferList *inputBufferList = (AudioBufferList *)calloc(
      1, sizeof(AudioBufferList) + sizeof(AudioBuffer) * (channelCount - 1));
  inputBufferList->mNumberBuffers = channelCount;
  NSMutableArray<NSMutableData *> *inputChannelStorage =
      [NSMutableArray arrayWithCapacity:channelCount];
  for (AVAudioChannelCount channel = 0; channel < channelCount; channel++) {
    NSMutableData *silence =
        [NSMutableData dataWithLength:renderFrameCount * sizeof(float)];
    [inputChannelStorage addObject:silence];
    inputBufferList->mBuffers[channel].mNumberChannels = 1;
    inputBufferList->mBuffers[channel].mDataByteSize =
        (UInt32)(renderFrameCount * sizeof(float));
    inputBufferList->mBuffers[channel].mData = silence.mutableBytes;
  }

  AudioBufferList *outputBufferList = (AudioBufferList *)calloc(
      1, sizeof(AudioBufferList) + sizeof(AudioBuffer) * (channelCount - 1));
  outputBufferList->mNumberBuffers = channelCount;
  NSMutableArray<NSMutableData *> *outputChannelStorage =
      [NSMutableArray arrayWithCapacity:channelCount];
  for (AVAudioChannelCount channel = 0; channel < channelCount; channel++) {
    NSMutableData *outputStorage =
        [NSMutableData dataWithLength:renderFrameCount * sizeof(float)];
    [outputChannelStorage addObject:outputStorage];
    outputBufferList->mBuffers[channel].mNumberChannels = 1;
    outputBufferList->mBuffers[channel].mDataByteSize =
        (UInt32)(renderFrameCount * sizeof(float));
    outputBufferList->mBuffers[channel].mData = outputStorage.mutableBytes;
  }

  AudioTimeStamp timestamp = {0};
  timestamp.mSampleTime = 0;
  timestamp.mFlags = kAudioTimeStampSampleTimeValid;

  AURenderPullInputBlock pullInputBlock = ^AUAudioUnitStatus(
      AudioUnitRenderActionFlags *actionFlags, const AudioTimeStamp *timestampPtr,
      AUAudioFrameCount pulledFrameCount, NSInteger inputBusNumber,
      AudioBufferList *inputData) {
    for (AVAudioChannelCount channel = 0; channel < channelCount; channel++) {
      inputData->mBuffers[channel].mNumberChannels = 1;
      inputData->mBuffers[channel].mDataByteSize =
          inputBufferList->mBuffers[channel].mDataByteSize;
      inputData->mBuffers[channel].mData = inputBufferList->mBuffers[channel].mData;
    }
    return noErr;
  };

  AudioUnitRenderActionFlags actionFlags = 0;
  AUAudioUnitStatus renderStatus = renderBlock(
      &actionFlags, &timestamp, renderFrameCount, 0, outputBufferList,
      pullInputBlock);

  NSLog(@"[AetherfieldHarness] cached render block before reallocation "
        @"returned status=%d (noErr=%d)",
        (int)renderStatus, (int)noErr);
  XCTAssertEqual(renderStatus, noErr, @"Render call returned non-zero OSStatus %d",
                 (int)renderStatus);

  BOOL outputBuffersPlausible = (outputBufferList->mNumberBuffers == channelCount);
  for (AVAudioChannelCount channel = 0; channel < channelCount; channel++) {
    BOOL bufferNonNil = (outputBufferList->mBuffers[channel].mData != NULL);
    UInt32 byteSize = outputBufferList->mBuffers[channel].mDataByteSize;
    outputBuffersPlausible = outputBuffersPlausible && bufferNonNil &&
                              (byteSize == renderFrameCount * sizeof(float));
    NSLog(@"[AetherfieldHarness] output buffer[%u]: nonNil=%d byteSize=%u "
          @"(expected %lu)",
          channel, bufferNonNil, byteSize,
          (unsigned long)(renderFrameCount * sizeof(float)));
  }
  // Deliberately not asserted: any claim about the *values* in
  // outputBufferList. Silent input was never claimed to produce silent or
  // any other particular output; this only checks the render call
  // completed with a benign status and returned a buffer of the shape we
  // asked for.
  NSLog(@"[AetherfieldHarness] output buffers plausibly shaped=%d",
        outputBuffersPlausible);

  [instantiatedUnit deallocateRenderResources];
  NSLog(@"[AetherfieldHarness] deallocateRenderResources called before reallocation");

  allocationError = nil;
  allocated = [instantiatedUnit allocateRenderResourcesAndReturnError:&allocationError];
  NSLog(@"[AetherfieldHarness] reallocateRenderResources succeeded=%d error=%@",
        allocated, allocationError);
  XCTAssertTrue(allocated, @"Resource reallocation failed: %@", allocationError);
  if (allocated) {
    timestamp.mSampleTime += renderFrameCount;
    actionFlags = 0;
    renderStatus = renderBlock(&actionFlags, &timestamp, renderFrameCount, 0,
                               outputBufferList, pullInputBlock);
    NSLog(@"[AetherfieldHarness] cached render block after reallocation "
          @"returned status=%d (noErr=%d)",
          (int)renderStatus, (int)noErr);
    XCTAssertEqual(renderStatus, noErr,
                   @"Cached render block after reallocation returned non-zero "
                    "OSStatus %d",
                   (int)renderStatus);
    [instantiatedUnit deallocateRenderResources];
  }

  free(inputBufferList);
  free(outputBufferList);
}

// A separate, simulator-only probe for the direct-render timeout above. The
// graph is driven by AVAudioEngine's offline rendering API rather than calling
// AUAudioUnit.renderBlock from the XCTest thread. A one-sample mono-positive
// impulse is rendered across repeated {1,13,64,512,3}-frame requests. Default
// Mix is wet-only and the configured FDN has a 27 ms minimum delay, so a
// non-zero output after source frame 0 is an observable delayed effect response,
// not input passthrough. Keeping this distinct from testAllocateAndRenderOneBlock
// preserves the direct-call failure as an independently reproducible finding.
- (void)verifyAVAudioEngineOfflineRenderObservesDelayedWetOutputAtSampleRate:
    (double)sampleRate {
  AudioComponentDescription targetDescription = {0};
  targetDescription.componentType = kAudioUnitType_Effect;  // 'aufx'
  targetDescription.componentSubType = 'Aeth';
  targetDescription.componentManufacturer = 'Josh';

  XCTestExpectation *instantiationExpectation =
      [self expectationWithDescription:@"AVAudioUnit engine instantiation"];
  __block AVAudioUnit *effectUnit = nil;
  __block NSError *instantiationError = nil;
  [AVAudioUnit
      instantiateWithComponentDescription:targetDescription
                                   options:kAudioComponentInstantiation_LoadOutOfProcess
                         completionHandler:^(AVAudioUnit *_Nullable audioUnit,
                                             NSError *_Nullable error) {
                           effectUnit = audioUnit;
                           instantiationError = error;
                           [instantiationExpectation fulfill];
                         }];
  [self waitForExpectationsWithTimeout:15.0 handler:nil];

  XCTAssertNil(instantiationError, @"AVAudioUnit instantiation failed: %@",
               instantiationError);
  XCTAssertNotNil(effectUnit,
                  @"Expected a non-nil AVAudioUnit for the engine graph");
  if (effectUnit == nil) {
    return;
  }

  const AVAudioChannelCount channelCount = 2;
  constexpr std::array<AVAudioFrameCount, 5> renderBlockSequence = {1, 13, 64,
                                                                      512, 3};
  constexpr AVAudioFrameCount maximumRenderFrameCount = 512;
  const AVAudioFrameCount totalFrameCount = 4096;
  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sampleRate
                                                      channels:channelCount];
  XCTAssertNotNil(format, @"Failed to construct a %.1f Hz stereo AVAudioFormat",
                  sampleRate);
  if (format == nil) {
    return;
  }

  AVAudioEngine *engine = [[AVAudioEngine alloc] init];
  AVAudioPlayerNode *sourceNode = [[AVAudioPlayerNode alloc] init];
  [engine attachNode:sourceNode];
  [engine attachNode:effectUnit];
  [engine connect:sourceNode to:effectUnit format:format];
  [engine connect:effectUnit to:engine.mainMixerNode format:format];

  NSError *manualModeError = nil;
  BOOL manualModeEnabled = [engine
               enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline
                          format:format
               maximumFrameCount:maximumRenderFrameCount
                           error:&manualModeError];
  NSLog(@"[AetherfieldHarness] AVAudioEngine offline mode enabled=%d error=%@",
        manualModeEnabled, manualModeError);
  XCTAssertTrue(manualModeEnabled,
                @"Failed to enable offline AVAudioEngine rendering: %@",
                manualModeError);
  if (!manualModeEnabled) {
    return;
  }

  NSError *startError = nil;
  BOOL engineStarted = [engine startAndReturnError:&startError];
  NSLog(@"[AetherfieldHarness] AVAudioEngine started=%d error=%@", engineStarted,
        startError);
  XCTAssertTrue(engineStarted, @"Failed to start AVAudioEngine: %@", startError);
  if (!engineStarted) {
    return;
  }

  AVAudioPCMBuffer *sourceBuffer =
      [[AVAudioPCMBuffer alloc] initWithPCMFormat:format
                                     frameCapacity:totalFrameCount];
  sourceBuffer.frameLength = totalFrameCount;
  AVAudioPCMBuffer *renderBuffer =
      [[AVAudioPCMBuffer alloc] initWithPCMFormat:format
                                     frameCapacity:maximumRenderFrameCount];
  XCTAssertNotNil(sourceBuffer, @"Failed to allocate source PCM buffer");
  XCTAssertNotNil(renderBuffer, @"Failed to allocate render PCM buffer");
  if (sourceBuffer == nil || renderBuffer == nil) {
    [engine stop];
    return;
  }

  float *const sourceLeft = sourceBuffer.floatChannelData[0];
  float *const sourceRight = sourceBuffer.floatChannelData[1];
  XCTAssertTrue(sourceLeft != nullptr,
                @"Expected non-interleaved left source storage");
  XCTAssertTrue(sourceRight != nullptr,
                @"Expected non-interleaved right source storage");
  if (sourceLeft == nil || sourceRight == nil) {
    [engine stop];
    return;
  }
  sourceLeft[0] = 1.0F;

  [sourceNode scheduleBuffer:sourceBuffer atTime:nil options:0 completionHandler:nil];
  [sourceNode play];

  AVAudioFrameCount renderedFrames = 0;
  float latePeak = 0.0F;
  std::size_t renderRequestIndex = 0;
  while (renderedFrames < totalFrameCount) {
    const AVAudioFrameCount requestedFrameCount = std::min(
        renderBlockSequence[renderRequestIndex % renderBlockSequence.size()],
        totalFrameCount - renderedFrames);
    NSError *renderError = nil;
    AVAudioEngineManualRenderingStatus renderStatus =
        [engine renderOffline:requestedFrameCount toBuffer:renderBuffer error:&renderError];
    XCTAssertEqual(renderStatus, AVAudioEngineManualRenderingStatusSuccess,
                   @"Offline AVAudioEngine render at frame %u did not succeed: %@",
                   renderedFrames, renderError);
    if (renderStatus != AVAudioEngineManualRenderingStatusSuccess) {
      break;
    }
    XCTAssertEqual(renderBuffer.frameLength, requestedFrameCount,
                   @"Offline render at frame %u did not produce %u frames",
                   renderedFrames, requestedFrameCount);

    float *const outputLeft = renderBuffer.floatChannelData[0];
    float *const outputRight = renderBuffer.floatChannelData[1];
    XCTAssertTrue(outputLeft != nullptr,
                  @"Expected non-interleaved left render storage");
    XCTAssertTrue(outputRight != nullptr,
                  @"Expected non-interleaved right render storage");
    if (outputLeft == nil || outputRight == nil) {
      break;
    }
    for (AVAudioFrameCount frame = 0; frame < renderBuffer.frameLength; ++frame) {
      if (renderedFrames + frame == 0) {
        continue;
      }
      latePeak = std::max(latePeak, std::fabs(outputLeft[frame]));
      latePeak = std::max(latePeak, std::fabs(outputRight[frame]));
    }
    renderedFrames += renderBuffer.frameLength;
    ++renderRequestIndex;
  }
  NSLog(@"[AetherfieldHarness] AVAudioEngine offline renderedFrames=%u "
        @"requests=%zu sequence={1,13,64,512,3} latePeak=%g",
        renderedFrames, renderRequestIndex, latePeak);
  XCTAssertEqual(renderedFrames, totalFrameCount,
                 @"Offline render did not complete the requested duration");
  XCTAssertGreaterThan(latePeak, 0.0F,
                       @"Expected a delayed wet response after the source impulse; "
                        "a zero tail permits a disconnected or bypassed graph");

  [sourceNode stop];
  [engine stop];
}

- (void)testAVAudioEngineOfflineRenderObservesDelayedWetOutputAt48kHz {
  [self verifyAVAudioEngineOfflineRenderObservesDelayedWetOutputAtSampleRate:48000.0];
}

- (void)testAVAudioEngineOfflineRenderObservesDelayedWetOutputAt44100Hz {
  [self verifyAVAudioEngineOfflineRenderObservesDelayedWetOutputAtSampleRate:44100.0];
}

@end

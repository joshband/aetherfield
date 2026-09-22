// Bounded physical-device HT-1/HT-3 evidence harness.
// This file is intentionally separate from the discovery/render-context probes
// in ComponentInstantiationTests.mm so its lifecycle and bit-exact oracle
// results remain independently attributable.
#import <XCTest/XCTest.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

@interface AetherfieldPhysicalAcceptanceTests : XCTestCase
@end

@implementation AetherfieldPhysicalAcceptanceTests

- (AUAudioUnit *)newAetherfieldUnit {
  AudioComponentDescription description = {0};
  description.componentType = kAudioUnitType_Effect;
  description.componentSubType = 'Aeth';
  description.componentManufacturer = 'Josh';

  AVAudioUnitComponent *component = nil;
  NSArray<AVAudioUnitComponent *> *matches =
      [[AVAudioUnitComponentManager sharedAudioUnitComponentManager]
          componentsMatchingDescription:description];
  XCTAssertGreaterThanOrEqual(matches.count, 1U,
                              @"Aetherfield AU was not discoverable");
  if (matches.count == 0) {
    return nil;
  }
  component = matches.firstObject;

  XCTestExpectation *expectation =
      [self expectationWithDescription:@"AU instantiation"];
  __block AUAudioUnit *unit = nil;
  __block NSError *error = nil;
  [AUAudioUnit
      instantiateWithComponentDescription:component.audioComponentDescription
                                   options:kAudioComponentInstantiation_LoadOutOfProcess
                         completionHandler:^(AUAudioUnit *audioUnit,
                                             NSError *instantiationError) {
                           unit = audioUnit;
                           error = instantiationError;
                           [expectation fulfill];
                         }];
  [self waitForExpectationsWithTimeout:15.0 handler:nil];
  XCTAssertNil(error, @"AU instantiation failed: %@", error);
  XCTAssertNotNil(unit, @"AU instantiation returned nil");
  return unit;
}

- (BOOL)configureUnit:(AUAudioUnit *)unit
           sampleRate:(double)sampleRate
        maximumFrames:(AVAudioFrameCount)maximumFrames
                error:(NSError **)error {
  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sampleRate
                                                       channels:2];
  if (format == nil) {
    return NO;
  }
  NSError *inputError = nil;
  NSError *outputError = nil;
  BOOL inputOK = [unit.inputBusses[0] setFormat:format error:&inputError];
  BOOL outputOK = [unit.outputBusses[0] setFormat:format error:&outputError];
  if (!inputOK || !outputOK) {
    if (error != nullptr) {
      *error = inputError ?: outputError;
    }
    return NO;
  }
  unit.maximumFramesToRender = maximumFrames;
  return YES;
}

- (AUAudioUnitStatus)renderBlock:(AURenderBlock)renderBlock
                         sourceL:(const std::vector<float> &)sourceL
                         sourceR:(const std::vector<float> &)sourceR
                          offset:(size_t)offset
                          frames:(AVAudioFrameCount)frames
                         outputL:(std::vector<float> &)outputL
                         outputR:(std::vector<float> &)outputR
                  sampleTimeBase:(AVAudioFramePosition)sampleTimeBase {
  // AudioBufferList declares mBuffers[1] as a C flexible-array convention;
  // `AudioBufferList output = {0}` only reserves storage for one buffer.
  // Allocate the two-buffer shape explicitly so the harness itself does not
  // write past its stack object while exercising the AU boundary.
  struct StereoOutputBufferList {
    UInt32 mNumberBuffers;
    AudioBuffer mBuffers[2];
  } outputStorage = {};
  AudioBufferList *output =
      reinterpret_cast<AudioBufferList *>(&outputStorage);
  output->mNumberBuffers = 2;
  output->mBuffers[0].mNumberChannels = 1;
  output->mBuffers[0].mDataByteSize = frames * sizeof(float);
  output->mBuffers[0].mData = outputL.data() + offset;
  output->mBuffers[1].mNumberChannels = 1;
  output->mBuffers[1].mDataByteSize = frames * sizeof(float);
  output->mBuffers[1].mData = outputR.data() + offset;

  AURenderPullInputBlock pull = ^AUAudioUnitStatus(
      AudioUnitRenderActionFlags *, const AudioTimeStamp *,
      AUAudioFrameCount pulledFrames, NSInteger, AudioBufferList *input) {
    input->mNumberBuffers = 2;
    input->mBuffers[0].mNumberChannels = 1;
    input->mBuffers[0].mDataByteSize = pulledFrames * sizeof(float);
    input->mBuffers[0].mData = const_cast<float *>(sourceL.data() + offset);
    input->mBuffers[1].mNumberChannels = 1;
    input->mBuffers[1].mDataByteSize = pulledFrames * sizeof(float);
    input->mBuffers[1].mData = const_cast<float *>(sourceR.data() + offset);
    return noErr;
  };

  AudioTimeStamp timestamp = {0};
  timestamp.mFlags = kAudioTimeStampSampleTimeValid;
  timestamp.mSampleTime = sampleTimeBase;
  AudioUnitRenderActionFlags flags = 0;
  return renderBlock(&flags, &timestamp, frames, 0, output, pull);
}

- (BOOL)renderStreamAtSampleRate:(double)sampleRate
                         sourceL:(const std::vector<float> &)sourceL
                         sourceR:(const std::vector<float> &)sourceR
                       partitions:(const std::vector<AVAudioFrameCount> &)partitions
                          outputL:(std::vector<float> &)outputL
                          outputR:(std::vector<float> &)outputR {
  AVAudioFrameCount maximumPartition = 0;
  for (AVAudioFrameCount partition : partitions) {
    maximumPartition = std::max(maximumPartition, partition);
  }
  AUAudioUnit *unit = [self newAetherfieldUnit];
  if (unit == nil) {
    return NO;
  }
  NSError *configurationError = nil;
  BOOL configured = [self configureUnit:unit
                            sampleRate:sampleRate
                         maximumFrames:maximumPartition
                                 error:&configurationError];
  XCTAssertTrue(configured, @"AU configuration failed: %@", configurationError);
  if (!configured) {
    return NO;
  }
  NSError *allocationError = nil;
  BOOL allocated =
      [unit allocateRenderResourcesAndReturnError:&allocationError];
  XCTAssertTrue(allocated, @"AU allocation failed: %@", allocationError);
  if (!allocated) {
    return NO;
  }
  AURenderBlock renderBlock = unit.renderBlock;
  XCTAssertNotNil(renderBlock, @"AU render block was nil");
  if (renderBlock == nil) {
    [unit deallocateRenderResources];
    return NO;
  }

  size_t offset = 0;
  size_t partitionIndex = 0;
  while (offset < sourceL.size()) {
    const AVAudioFrameCount requested =
        partitions[partitionIndex % partitions.size()];
    ++partitionIndex;
    const size_t remaining = sourceL.size() - offset;
    AVAudioFrameCount frames =
        std::min<AVAudioFrameCount>(requested, (AVAudioFrameCount)remaining);
    AUAudioUnitStatus status =
        [self renderBlock:renderBlock
                  sourceL:sourceL
                  sourceR:sourceR
                   offset:offset
                   frames:frames
                  outputL:outputL
                  outputR:outputR
           sampleTimeBase:offset];
    XCTAssertEqual(status, noErr, @"Render failed at offset %zu with status %d",
                   offset, (int)status);
    if (status != noErr) {
      [unit deallocateRenderResources];
      return NO;
    }
    offset += frames;
  }
  // Exercise the trailing zero-frame case in the explicitly ragged sequence
  // after the complete input stream has been consumed.
  if (!partitions.empty() && partitions.back() == 0) {
    AUAudioUnitStatus status =
        [self renderBlock:renderBlock
                  sourceL:sourceL
                  sourceR:sourceR
                   offset:offset
                   frames:0
                  outputL:outputL
                  outputR:outputR
           sampleTimeBase:offset];
    XCTAssertEqual(status, noErr, @"Trailing zero-frame render failed: %d",
                   (int)status);
  }
  [unit deallocateRenderResources];
  XCTAssertEqual(offset, sourceL.size(), @"Partition sequence did not consume input");
  return offset == sourceL.size();
}

- (std::vector<float>)inputVectorWithFrameCount:(size_t)frameCount
                                          right:(BOOL)right {
  std::vector<float> values(frameCount, 0.0F);
  const size_t signalFrames = std::min<size_t>(65536, frameCount);
  for (size_t frame = 0; frame < signalFrames; ++frame) {
    uint32_t state = (uint32_t)(frame * 1664525U + 1013904223U);
    float value = ((float)((state >> 8) & 0xFFFFU) / 32767.5F) - 1.0F;
    values[frame] = right ? -0.73F * value : value;
  }
  return values;
}

- (void)testHT1LifecycleSmokeAtBothRates {
  constexpr std::array<double, 2> rates = {44100.0, 48000.0};
  constexpr int sameInstanceCycles = 100;
  constexpr int instantiateCycles = 100;
  for (double rate : rates) {
    const AVAudioFrameCount frameCount = (AVAudioFrameCount)rate;
    std::vector<float> silence(frameCount, 0.0F);
    AUAudioUnit *unit = [self newAetherfieldUnit];
    if (unit == nil) {
      return;
    }
    NSError *configurationError = nil;
    XCTAssertTrue([self configureUnit:unit
                  sampleRate:rate
                       maximumFrames:frameCount
                               error:&configurationError],
                  @"Same-instance configuration failed: %@", configurationError);
    AURenderBlock renderBlock = unit.renderBlock;
    for (int cycle = 0; cycle < sameInstanceCycles; ++cycle) {
      NSError *allocationError = nil;
      BOOL allocated =
          [unit allocateRenderResourcesAndReturnError:&allocationError];
      XCTAssertTrue(allocated, @"Same-instance allocation %d failed: %@", cycle,
                    allocationError);
      if (!allocated) {
        break;
      }
      std::vector<float> outputL(frameCount, 0.0F);
      std::vector<float> outputR(frameCount, 0.0F);
      AUAudioUnitStatus status =
          [self renderBlock:renderBlock
                    sourceL:silence
                    sourceR:silence
                     offset:0
                     frames:frameCount
                    outputL:outputL
                    outputR:outputR
             sampleTimeBase:(AVAudioFramePosition)cycle * frameCount];
      XCTAssertEqual(status, noErr, @"Same-instance render %d failed: %d", cycle,
                     (int)status);
      [unit deallocateRenderResources];
    }
    for (int cycle = 0; cycle < instantiateCycles; ++cycle) {
      AUAudioUnit *freshUnit = [self newAetherfieldUnit];
      if (freshUnit == nil) {
        return;
      }
      NSError *configurationError = nil;
      XCTAssertTrue([self configureUnit:freshUnit
                            sampleRate:rate
                         maximumFrames:frameCount
                                 error:&configurationError],
                    @"Fresh configuration %d failed: %@", cycle,
                    configurationError);
      NSError *allocationError = nil;
      BOOL allocated =
          [freshUnit allocateRenderResourcesAndReturnError:&allocationError];
      XCTAssertTrue(allocated, @"Fresh allocation %d failed: %@", cycle,
                    allocationError);
      if (allocated) {
        AURenderBlock freshBlock = freshUnit.renderBlock;
        std::vector<float> outputL(frameCount, 0.0F);
        std::vector<float> outputR(frameCount, 0.0F);
        AUAudioUnitStatus status =
            [self renderBlock:freshBlock
                      sourceL:silence
                      sourceR:silence
                       offset:0
                       frames:frameCount
                      outputL:outputL
                      outputR:outputR
               sampleTimeBase:0];
        XCTAssertEqual(status, noErr, @"Fresh render %d failed: %d", cycle,
                       (int)status);
        [freshUnit deallocateRenderResources];
      }
    }
  }
}

- (void)testHT3FixedAndRaggedPartitionsAreBitExactAtBothRates {
  constexpr size_t totalFrames = 131072;
  constexpr std::array<double, 2> rates = {44100.0, 48000.0};
  const std::vector<float> sourceL =
      [self inputVectorWithFrameCount:totalFrames right:NO];
  const std::vector<float> sourceR =
      [self inputVectorWithFrameCount:totalFrames right:YES];
  const std::vector<std::vector<AVAudioFrameCount>> partitionSets = {
      {4096},
      {1, 13, 64, 512, 3},
      {7, 29, 3, 211, 5},
      {0, 1, 13, 64, 512, 977, 1024, 3, 0}};

  NSLog(@"[AetherfieldHarness] HT-3 deterministic input seed=1664525/1013904223 "
        @"frames=%zu signalFrames=65536 zeroTailFrames=65536", totalFrames);

  for (double rate : rates) {
    std::vector<float> referenceL(totalFrames, 0.0F);
    std::vector<float> referenceR(totalFrames, 0.0F);
    XCTAssertTrue([self renderStreamAtSampleRate:rate
                                         sourceL:sourceL
                                         sourceR:sourceR
                                       partitions:{(AVAudioFrameCount)totalFrames}
                                          outputL:referenceL
                                          outputR:referenceR],
                  @"Reference render failed at %.1f Hz", rate);

    for (size_t partitionSetIndex = 0; partitionSetIndex < partitionSets.size();
         ++partitionSetIndex) {
      const auto &partitions = partitionSets[partitionSetIndex];
      std::vector<float> candidateL(totalFrames, 0.0F);
      std::vector<float> candidateR(totalFrames, 0.0F);
      XCTAssertTrue([self renderStreamAtSampleRate:rate
                                           sourceL:sourceL
                                           sourceR:sourceR
                                         partitions:partitions
                                            outputL:candidateL
                                            outputR:candidateR],
                    @"Partitioned render failed at %.1f Hz", rate);
      BOOL leftEqual = std::memcmp(referenceL.data(), candidateL.data(),
                                   referenceL.size() * sizeof(float)) == 0;
      BOOL rightEqual = std::memcmp(referenceR.data(), candidateR.data(),
                                    referenceR.size() * sizeof(float)) == 0;
      if (!leftEqual || !rightEqual) {
        size_t firstLeftMismatch = totalFrames;
        size_t firstRightMismatch = totalFrames;
        for (size_t frame = 0; frame < totalFrames; ++frame) {
          if (firstLeftMismatch == totalFrames &&
              referenceL[frame] != candidateL[frame]) {
            firstLeftMismatch = frame;
          }
          if (firstRightMismatch == totalFrames &&
              referenceR[frame] != candidateR[frame]) {
            firstRightMismatch = frame;
          }
          if (firstLeftMismatch != totalFrames &&
              firstRightMismatch != totalFrames) {
            break;
          }
        }
        NSLog(@"[AetherfieldHarness] HT-3 mismatch rate=%.1f partitionSet=%zu "
              @"firstLeft=%zu firstRight=%zu sequenceCount=%zu first=%u",
              rate, partitionSetIndex, firstLeftMismatch, firstRightMismatch,
              partitions.size(), partitions.empty() ? 0U : partitions.front());
      }
      XCTAssertTrue(leftEqual && rightEqual,
                    @"Bit-exact partition mismatch at %.1f Hz set %zu", rate,
                    partitionSetIndex);
    }
  }
}

// Diagnostic-only isolation test (2026-09-21), not part of the plan's
// required HT-3 matrix: narrows testHT3FixedAndRaggedPartitionsAreBitExactAtBothRates's
// reproduced mismatch on {0,1,13,64,512,977,1024,3,0} to whichever specific
// zero-frame call (leading, trailing, or either) triggers it, at 44100 Hz
// only, to bound the follow-up investigation without repeating the full
// two-rate/four-set matrix. See docs/testing.md's "Zero-frame-partition
// mismatch isolation" section for the interpreted result.
- (void)testHT3ZeroFrameIsolationAt44100Hz {
  constexpr size_t totalFrames = 131072;
  constexpr double rate = 44100.0;
  const std::vector<float> sourceL =
      [self inputVectorWithFrameCount:totalFrames right:NO];
  const std::vector<float> sourceR =
      [self inputVectorWithFrameCount:totalFrames right:YES];

  std::vector<float> referenceL(totalFrames, 0.0F);
  std::vector<float> referenceR(totalFrames, 0.0F);
  XCTAssertTrue([self renderStreamAtSampleRate:rate
                                       sourceL:sourceL
                                       sourceR:sourceR
                                     partitions:{(AVAudioFrameCount)totalFrames}
                                        outputL:referenceL
                                        outputR:referenceR],
                @"Reference render failed");

  const std::vector<std::pair<std::string, std::vector<AVAudioFrameCount>>> variants = {
      {"no_zero", {1, 13, 64, 512, 977, 1024, 3}},
      {"leading_zero_only", {0, 1, 13, 64, 512, 977, 1024, 3}},
      {"trailing_zero_only", {1, 13, 64, 512, 977, 1024, 3, 0}},
      {"both_zeros", {0, 1, 13, 64, 512, 977, 1024, 3, 0}},
  };

  for (const auto &variant : variants) {
    std::vector<float> candidateL(totalFrames, 0.0F);
    std::vector<float> candidateR(totalFrames, 0.0F);
    XCTAssertTrue([self renderStreamAtSampleRate:rate
                                         sourceL:sourceL
                                         sourceR:sourceR
                                       partitions:variant.second
                                          outputL:candidateL
                                          outputR:candidateR],
                  @"Partitioned render failed for variant %s", variant.first.c_str());
    BOOL leftEqual = std::memcmp(referenceL.data(), candidateL.data(),
                                 referenceL.size() * sizeof(float)) == 0;
    BOOL rightEqual = std::memcmp(referenceR.data(), candidateR.data(),
                                  referenceR.size() * sizeof(float)) == 0;
    size_t firstLeftMismatch = totalFrames;
    size_t firstRightMismatch = totalFrames;
    if (!leftEqual || !rightEqual) {
      for (size_t frame = 0; frame < totalFrames; ++frame) {
        if (firstLeftMismatch == totalFrames && referenceL[frame] != candidateL[frame]) {
          firstLeftMismatch = frame;
        }
        if (firstRightMismatch == totalFrames && referenceR[frame] != candidateR[frame]) {
          firstRightMismatch = frame;
        }
        if (firstLeftMismatch != totalFrames && firstRightMismatch != totalFrames) break;
      }
    }
    NSLog(@"[AetherfieldHarness] HT-3 zero-isolation variant=%s leftEqual=%d "
          @"rightEqual=%d firstLeft=%zu firstRight=%zu",
          variant.first.c_str(), leftEqual, rightEqual, firstLeftMismatch,
          firstRightMismatch);
  }
}

// Diagnostic-only follow-up (2026-09-21) to the zero-frame-partition
// mismatch isolated above: reruns the same comparison through an
// AVAudioEngine-managed offline manual-rendering graph instead of calling
// AUAudioUnit.renderBlock directly, to determine whether the mismatch is
// specific to the raw out-of-process direct-call harness or persists under
// a real host-managed render graph. See ComponentInstantiationTests.mm's
// verifyAVAudioEngineOfflineRenderObservesDelayedWetOutputAtSampleRate: for
// the established engine-setup pattern this reuses. See docs/testing.md's
// "Zero-frame-partition mismatch: AVAudioEngine rerun" section for the
// interpreted result.
- (BOOL)renderThroughEngineAtSampleRate:(double)sampleRate
                                sourceL:(const std::vector<float> &)sourceL
                                sourceR:(const std::vector<float> &)sourceR
                             partitions:(const std::vector<AVAudioFrameCount> &)partitions
                       maximumFrameCount:(AVAudioFrameCount)maximumFrameCount
                                outputL:(std::vector<float> &)outputL
                                outputR:(std::vector<float> &)outputR {
  AudioComponentDescription targetDescription = {0};
  targetDescription.componentType = kAudioUnitType_Effect;
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
  if (instantiationError != nil || effectUnit == nil) {
    XCTFail(@"AVAudioUnit instantiation failed: %@", instantiationError);
    return NO;
  }

  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sampleRate channels:2];
  if (format == nil) return NO;

  AVAudioEngine *engine = [[AVAudioEngine alloc] init];
  AVAudioPlayerNode *sourceNode = [[AVAudioPlayerNode alloc] init];
  [engine attachNode:sourceNode];
  [engine attachNode:effectUnit];
  [engine connect:sourceNode to:effectUnit format:format];
  [engine connect:effectUnit to:engine.mainMixerNode format:format];

  NSError *manualModeError = nil;
  BOOL manualModeEnabled =
      [engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline
                                  format:format
                       maximumFrameCount:maximumFrameCount
                                   error:&manualModeError];
  if (!manualModeEnabled) {
    XCTFail(@"Failed to enable offline AVAudioEngine rendering: %@", manualModeError);
    return NO;
  }

  NSError *startError = nil;
  if (![engine startAndReturnError:&startError]) {
    XCTFail(@"Failed to start AVAudioEngine: %@", startError);
    return NO;
  }

  const AVAudioFrameCount totalFrameCount = (AVAudioFrameCount)sourceL.size();
  AVAudioPCMBuffer *sourceBuffer =
      [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:totalFrameCount];
  sourceBuffer.frameLength = totalFrameCount;
  std::copy(sourceL.begin(), sourceL.end(), sourceBuffer.floatChannelData[0]);
  std::copy(sourceR.begin(), sourceR.end(), sourceBuffer.floatChannelData[1]);
  [sourceNode scheduleBuffer:sourceBuffer atTime:nil options:0 completionHandler:nil];
  [sourceNode play];

  AVAudioPCMBuffer *renderBuffer =
      [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:maximumFrameCount];

  outputL.assign(totalFrameCount, 0.0F);
  outputR.assign(totalFrameCount, 0.0F);

  AVAudioFrameCount renderedFrames = 0;
  std::size_t partitionIndex = 0;
  while (renderedFrames < totalFrameCount) {
    const AVAudioFrameCount requested = partitions[partitionIndex % partitions.size()];
    ++partitionIndex;
    const AVAudioFrameCount frames =
        std::min<AVAudioFrameCount>(requested, totalFrameCount - renderedFrames);
    NSError *renderError = nil;
    AVAudioEngineManualRenderingStatus status =
        [engine renderOffline:frames toBuffer:renderBuffer error:&renderError];
    NSLog(@"[AetherfieldHarness] engine renderOffline requested=%u status=%ld error=%@",
          frames, (long)status, renderError);
    if (status != AVAudioEngineManualRenderingStatusSuccess) {
      XCTFail(@"Engine offline render failed at frame %u (requested=%u): status=%ld error=%@",
              renderedFrames, frames, (long)status, renderError);
      [sourceNode stop];
      [engine stop];
      return NO;
    }
    std::copy_n(renderBuffer.floatChannelData[0], renderBuffer.frameLength,
                outputL.begin() + renderedFrames);
    std::copy_n(renderBuffer.floatChannelData[1], renderBuffer.frameLength,
                outputR.begin() + renderedFrames);
    renderedFrames += renderBuffer.frameLength;
  }

  [sourceNode stop];
  [engine stop];
  return renderedFrames == totalFrameCount;
}

- (void)testAVAudioEngineHT3ZeroFramePartitionAt44100Hz {
  constexpr size_t totalFrames = 131072;
  constexpr double rate = 44100.0;
  constexpr AVAudioFrameCount maximumFrameCount = 4096;
  const std::vector<float> sourceL =
      [self inputVectorWithFrameCount:totalFrames right:NO];
  const std::vector<float> sourceR =
      [self inputVectorWithFrameCount:totalFrames right:YES];

  // Named locals, not inline brace literals: the C preprocessor only
  // balances parentheses when splitting macro arguments, not braces, so a
  // multi-element `{0, 1, ...}` literal directly inside an XCTAssertTrue(...)
  // call is misparsed as several macro arguments at its top-level commas.
  const std::vector<AVAudioFrameCount> referencePartitions = {4096};
  const std::vector<AVAudioFrameCount> candidatePartitions =
      {0, 1, 13, 64, 512, 977, 1024, 3, 0};

  std::vector<float> referenceL, referenceR;
  XCTAssertTrue([self renderThroughEngineAtSampleRate:rate
                                              sourceL:sourceL
                                              sourceR:sourceR
                                           partitions:referencePartitions
                                     maximumFrameCount:maximumFrameCount
                                              outputL:referenceL
                                              outputR:referenceR],
                @"Engine reference render failed");

  std::vector<float> candidateL, candidateR;
  XCTAssertTrue([self renderThroughEngineAtSampleRate:rate
                                              sourceL:sourceL
                                              sourceR:sourceR
                                           partitions:candidatePartitions
                                     maximumFrameCount:maximumFrameCount
                                              outputL:candidateL
                                              outputR:candidateR],
                @"Engine candidate render failed");

  BOOL leftEqual = std::memcmp(referenceL.data(), candidateL.data(),
                               referenceL.size() * sizeof(float)) == 0;
  BOOL rightEqual = std::memcmp(referenceR.data(), candidateR.data(),
                                referenceR.size() * sizeof(float)) == 0;
  size_t firstLeftMismatch = totalFrames, firstRightMismatch = totalFrames;
  if (!leftEqual || !rightEqual) {
    for (size_t frame = 0; frame < totalFrames; ++frame) {
      if (firstLeftMismatch == totalFrames && referenceL[frame] != candidateL[frame]) {
        firstLeftMismatch = frame;
      }
      if (firstRightMismatch == totalFrames && referenceR[frame] != candidateR[frame]) {
        firstRightMismatch = frame;
      }
      if (firstLeftMismatch != totalFrames && firstRightMismatch != totalFrames) break;
    }
  }
  NSLog(@"[AetherfieldHarness] engine HT-3 zero-frame comparison leftEqual=%d "
        @"rightEqual=%d firstLeft=%zu firstRight=%zu",
        leftEqual, rightEqual, firstLeftMismatch, firstRightMismatch);
}

// Diagnostic-only follow-up (2026-09-21) to the confirmed zero-frame-partition
// mismatch: instruments the TEST's own pull-input block (not any production
// AU/wrapper source) to record what the AU under test actually requests via
// pullInputBlock on every callback, including nominal zero-frame requests.
// This tests, without needing Instruments, whether the out-of-process AU
// silently pulls real (nonzero) frames during a request the caller declared
// as 0 frames -- which would explain the confirmed divergence without any
// defect in this project's own render-thread source. See docs/testing.md's
// "Zero-frame-partition mismatch: pull-block instrumentation" section for
// the interpreted result.
- (AUAudioUnitStatus)renderBlockInstrumented:(AURenderBlock)renderBlock
                                     sourceL:(const std::vector<float> &)sourceL
                                     sourceR:(const std::vector<float> &)sourceR
                                      offset:(size_t)offset
                                      frames:(AVAudioFrameCount)frames
                                     outputL:(std::vector<float> &)outputL
                                     outputR:(std::vector<float> &)outputR
                              sampleTimeBase:(AVAudioFramePosition)sampleTimeBase {
  struct StereoOutputBufferList {
    UInt32 mNumberBuffers;
    AudioBuffer mBuffers[2];
  } outputStorage = {};
  AudioBufferList *output =
      reinterpret_cast<AudioBufferList *>(&outputStorage);
  output->mNumberBuffers = 2;
  output->mBuffers[0].mNumberChannels = 1;
  output->mBuffers[0].mDataByteSize = frames * sizeof(float);
  output->mBuffers[0].mData = outputL.data() + offset;
  output->mBuffers[1].mNumberChannels = 1;
  output->mBuffers[1].mDataByteSize = frames * sizeof(float);
  output->mBuffers[1].mData = outputR.data() + offset;

  __block int pullInvocationCount = 0;
  AURenderPullInputBlock pull = ^AUAudioUnitStatus(
      AudioUnitRenderActionFlags *, const AudioTimeStamp *,
      AUAudioFrameCount pulledFrames, NSInteger, AudioBufferList *input) {
    ++pullInvocationCount;
    NSLog(@"[AetherfieldHarness] pull-instrumentation requestedFrames=%u "
          @"offset=%zu invocation=%d pulledFrames=%u",
          frames, offset, pullInvocationCount, pulledFrames);
    input->mNumberBuffers = 2;
    input->mBuffers[0].mNumberChannels = 1;
    input->mBuffers[0].mDataByteSize = pulledFrames * sizeof(float);
    input->mBuffers[0].mData = const_cast<float *>(sourceL.data() + offset);
    input->mBuffers[1].mNumberChannels = 1;
    input->mBuffers[1].mDataByteSize = pulledFrames * sizeof(float);
    input->mBuffers[1].mData = const_cast<float *>(sourceR.data() + offset);
    return noErr;
  };

  AudioTimeStamp timestamp = {0};
  timestamp.mFlags = kAudioTimeStampSampleTimeValid;
  timestamp.mSampleTime = sampleTimeBase;
  AudioUnitRenderActionFlags flags = 0;
  AUAudioUnitStatus status = renderBlock(&flags, &timestamp, frames, 0, output, pull);
  if (pullInvocationCount == 0) {
    NSLog(@"[AetherfieldHarness] pull-instrumentation requestedFrames=%u "
          @"offset=%zu: pull block was NOT invoked",
          frames, offset);
  }
  return status;
}

- (void)testHT3ZeroFramePullInstrumentationAt44100Hz {
  constexpr size_t totalFrames = 8192;  // short: only need to observe the first cycle
  constexpr double rate = 44100.0;
  const std::vector<float> sourceL =
      [self inputVectorWithFrameCount:totalFrames right:NO];
  const std::vector<float> sourceR =
      [self inputVectorWithFrameCount:totalFrames right:YES];
  const std::vector<AVAudioFrameCount> partitions =
      {0, 1, 13, 64, 512, 977, 1024, 3};

  AVAudioFrameCount maximumPartition = 0;
  for (AVAudioFrameCount partition : partitions) {
    maximumPartition = std::max(maximumPartition, partition);
  }
  AUAudioUnit *unit = [self newAetherfieldUnit];
  XCTAssertNotNil(unit);
  if (unit == nil) return;
  NSError *configurationError = nil;
  BOOL configured = [self configureUnit:unit
                            sampleRate:rate
                         maximumFrames:maximumPartition
                                 error:&configurationError];
  XCTAssertTrue(configured, @"AU configuration failed: %@", configurationError);
  if (!configured) return;
  NSError *allocationError = nil;
  BOOL allocated = [unit allocateRenderResourcesAndReturnError:&allocationError];
  XCTAssertTrue(allocated, @"AU allocation failed: %@", allocationError);
  if (!allocated) return;
  AURenderBlock renderBlock = unit.renderBlock;
  XCTAssertNotNil(renderBlock);
  if (renderBlock == nil) {
    [unit deallocateRenderResources];
    return;
  }

  // Distinctive sentinel, not 0.0F: lets a "was this slot actually written
  // by the extension?" check be distinguished from a legitimately-silent
  // early wet-tail sample, which is expected to be exactly 0.0F this early.
  constexpr float kSentinel = -999.0F;
  std::vector<float> outputL(totalFrames, kSentinel);
  std::vector<float> outputR(totalFrames, kSentinel);
  size_t offset = 0;
  size_t partitionIndex = 0;
  int callsLogged = 0;
  while (offset < totalFrames && callsLogged < 12) {
    const AVAudioFrameCount requested = partitions[partitionIndex % partitions.size()];
    ++partitionIndex;
    const size_t remaining = totalFrames - offset;
    AVAudioFrameCount frames = std::min<AVAudioFrameCount>(requested, (AVAudioFrameCount)remaining);
    NSLog(@"[AetherfieldHarness] pull-instrumentation about to call renderBlock "
          @"requestedFrames=%u offset=%zu", frames, offset);
    AUAudioUnitStatus status = [self renderBlockInstrumented:renderBlock
                                                       sourceL:sourceL
                                                       sourceR:sourceR
                                                        offset:offset
                                                        frames:frames
                                                       outputL:outputL
                                                       outputR:outputR
                                                sampleTimeBase:offset];
    XCTAssertEqual(status, noErr, @"Render failed at offset %zu status %d", offset, (int)status);
    if (frames > 0) {
      NSLog(@"[AetherfieldHarness] pull-instrumentation post-call offset=%zu "
            @"frames=%u outputL[offset]=%g stillSentinel=%d",
            offset, frames, outputL[offset], outputL[offset] == kSentinel);
    }
    offset += frames;
    ++callsLogged;
  }
  [unit deallocateRenderResources];
}

// Diagnostic-only (2026-09-21): lists every AUv3 audio-unit component
// (any type: effect/instrument/music-effect/generator) actually installed
// and discoverable on this device, to identify a third-party AU -- JUCE-
// based or not -- that the confirmed zero-frame-partition finding could be
// retested against, to determine whether it is a property of Apple's own
// out-of-process AU hosting layer (would reproduce on any AU) or specific
// to this project's own AU. Not part of the plan's required matrix.
- (void)testEnumerateInstalledAudioComponents {
  AudioComponentDescription wildcard = {0};  // all-zero matches everything
  NSArray<AVAudioUnitComponent *> *matches =
      [[AVAudioUnitComponentManager sharedAudioUnitComponentManager]
          componentsMatchingDescription:wildcard];
  NSLog(@"[AetherfieldHarness] installed component count=%lu", (unsigned long)matches.count);
  for (AVAudioUnitComponent *component in matches) {
    AudioComponentDescription description = component.audioComponentDescription;
    char type[5] = {0}, subtype[5] = {0}, manufacturer[5] = {0};
    *(UInt32 *)type = CFSwapInt32HostToBig(description.componentType);
    *(UInt32 *)subtype = CFSwapInt32HostToBig(description.componentSubType);
    *(UInt32 *)manufacturer = CFSwapInt32HostToBig(description.componentManufacturer);
    NSLog(@"[AetherfieldHarness] installed component name=%@ manufacturerName=%@ "
          @"type=%s subtype=%s manufacturer=%s",
          component.name, component.manufacturerName, type, subtype, manufacturer);
  }
}

// Diagnostic-only (2026-09-21), answering "is the confirmed zero-frame
// render-skip a property of Apple's own out-of-process AU hosting layer, or
// specific to this project's own AU?": instantiates a THIRD-PARTY AUv3
// (already installed on this device, not built by this project) by exact
// AudioComponentDescription, and reruns the same leading-zero-frame pull-
// instrumentation probe against it. No source from the third-party plugin
// is touched or vendored; only the public AVAudioUnitComponentManager/
// AUAudioUnit discovery-and-render APIs this project already uses are
// exercised. See docs/testing.md's "Third-party AU cross-check" section
// for the interpreted result.
- (void)runThirdPartyZeroFrameProbeWithType:(OSType)type
                                    subtype:(OSType)subtype
                               manufacturer:(OSType)manufacturer
                                       name:(NSString *)name {
  AudioComponentDescription description = {0};
  description.componentType = type;
  description.componentSubType = subtype;
  description.componentManufacturer = manufacturer;

  NSArray<AVAudioUnitComponent *> *matches =
      [[AVAudioUnitComponentManager sharedAudioUnitComponentManager]
          componentsMatchingDescription:description];
  if (matches.count == 0) {
    NSLog(@"[AetherfieldHarness] third-party probe %@: not discoverable, skipping", name);
    return;
  }

  XCTestExpectation *expectation =
      [self expectationWithDescription:[NSString stringWithFormat:@"third-party instantiation %@", name]];
  __block AUAudioUnit *unit = nil;
  __block NSError *instantiationError = nil;
  [AUAudioUnit
      instantiateWithComponentDescription:matches.firstObject.audioComponentDescription
                                   options:kAudioComponentInstantiation_LoadOutOfProcess
                         completionHandler:^(AUAudioUnit *audioUnit, NSError *error) {
                           unit = audioUnit;
                           instantiationError = error;
                           [expectation fulfill];
                         }];
  [self waitForExpectationsWithTimeout:15.0 handler:nil];
  if (instantiationError != nil || unit == nil) {
    NSLog(@"[AetherfieldHarness] third-party probe %@: instantiation failed: %@",
          name, instantiationError);
    return;
  }

  const double rate = 44100.0;
  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:rate channels:2];
  NSError *formatError = nil;
  BOOL inputOK = unit.inputBusses.count > 0
      ? [unit.inputBusses[0] setFormat:format error:&formatError]
      : YES;  // some effects have no input bus of their own (e.g. generators); not our target case
  BOOL outputOK = [unit.outputBusses[0] setFormat:format error:&formatError];
  if (!inputOK || !outputOK) {
    NSLog(@"[AetherfieldHarness] third-party probe %@: format rejected: %@", name, formatError);
    return;
  }
  unit.maximumFramesToRender = 1024;
  NSError *allocationError = nil;
  if (![unit allocateRenderResourcesAndReturnError:&allocationError]) {
    NSLog(@"[AetherfieldHarness] third-party probe %@: allocation failed: %@",
          name, allocationError);
    return;
  }
  AURenderBlock renderBlock = unit.renderBlock;
  if (renderBlock == nil) {
    NSLog(@"[AetherfieldHarness] third-party probe %@: nil renderBlock", name);
    [unit deallocateRenderResources];
    return;
  }

  constexpr size_t totalFrames = 8192;
  const std::vector<float> sourceL = [self inputVectorWithFrameCount:totalFrames right:NO];
  const std::vector<float> sourceR = [self inputVectorWithFrameCount:totalFrames right:YES];
  const std::vector<AVAudioFrameCount> partitions = {0, 1, 13, 64, 512, 977, 1024, 3};

  std::vector<float> outputL(totalFrames, 0.0F);
  std::vector<float> outputR(totalFrames, 0.0F);
  size_t offset = 0;
  size_t partitionIndex = 0;
  int pullSkipCount = 0;
  int callsIssued = 0;
  while (offset < totalFrames && callsIssued < 16) {
    const AVAudioFrameCount requested = partitions[partitionIndex % partitions.size()];
    ++partitionIndex;
    const size_t remaining = totalFrames - offset;
    AVAudioFrameCount frames = std::min<AVAudioFrameCount>(requested, (AVAudioFrameCount)remaining);

    __block int pullInvocationCount = 0;
    struct StereoOutputBufferList {
      UInt32 mNumberBuffers;
      AudioBuffer mBuffers[2];
    } outputStorage = {};
    AudioBufferList *output = reinterpret_cast<AudioBufferList *>(&outputStorage);
    output->mNumberBuffers = 2;
    output->mBuffers[0].mNumberChannels = 1;
    output->mBuffers[0].mDataByteSize = frames * sizeof(float);
    output->mBuffers[0].mData = outputL.data() + offset;
    output->mBuffers[1].mNumberChannels = 1;
    output->mBuffers[1].mDataByteSize = frames * sizeof(float);
    output->mBuffers[1].mData = outputR.data() + offset;

    AURenderPullInputBlock pull = ^AUAudioUnitStatus(
        AudioUnitRenderActionFlags *, const AudioTimeStamp *,
        AUAudioFrameCount pulledFrames, NSInteger, AudioBufferList *input) {
      ++pullInvocationCount;
      input->mNumberBuffers = 2;
      input->mBuffers[0].mNumberChannels = 1;
      input->mBuffers[0].mDataByteSize = pulledFrames * sizeof(float);
      input->mBuffers[0].mData = const_cast<float *>(sourceL.data() + offset);
      input->mBuffers[1].mNumberChannels = 1;
      input->mBuffers[1].mDataByteSize = pulledFrames * sizeof(float);
      input->mBuffers[1].mData = const_cast<float *>(sourceR.data() + offset);
      return noErr;
    };

    AudioTimeStamp timestamp = {0};
    timestamp.mFlags = kAudioTimeStampSampleTimeValid;
    timestamp.mSampleTime = (AVAudioFramePosition)offset;
    AudioUnitRenderActionFlags flags = 0;
    AUAudioUnitStatus status = renderBlock(&flags, &timestamp, frames, 0, output, pull);
    if (frames > 0 && pullInvocationCount == 0) {
      ++pullSkipCount;
      NSLog(@"[AetherfieldHarness] third-party probe %@: pull SKIPPED at offset=%zu "
            @"frames=%u (following requestedFrames=%u previous call) status=%d",
            name, offset, frames, partitions[(partitionIndex - 2) % partitions.size()],
            (int)status);
    }
    offset += frames;
    ++callsIssued;
  }
  NSLog(@"[AetherfieldHarness] third-party probe %@: callsIssued=%d pullSkipCount=%d",
        name, callsIssued, pullSkipCount);
  [unit deallocateRenderResources];
}

- (void)testThirdPartyZeroFrameCrossCheck {
  // Eventide: proprietary in-house DSP framework, not JUCE -- a clean
  // control for "is this host-level, independent of the plugin framework."
  [self runThirdPartyZeroFrameProbeWithType:kAudioUnitType_Effect
                                    subtype:'HOLE'
                               manufacturer:'TIDE'
                                       name:@"Blackhole (Eventide)"];
  // Audio Damage: widely reported to build its iOS AUv3 ports with JUCE.
  [self runThirdPartyZeroFrameProbeWithType:kAudioUnitType_Effect
                                    subtype:'ADe2'
                               manufacturer:'AuDa'
                                       name:@"Eos 2 (Audio Damage)"];
  // Apple's own system AU: in-process reference point.
  [self runThirdPartyZeroFrameProbeWithType:kAudioUnitType_Effect
                                    subtype:'dely'
                               manufacturer:'appl'
                                       name:@"AUDelay (Apple)"];
}

// HT-2: Rate Negotiation (ADR-012 / ADR-010)
// Tests that the AU rejects unsupported sample rates at allocation time (per
// AetherfieldAudioUnit.mm:313–344) with the correct NSError (domain, code,
// localizedDescription), and that the prior configuration can render identically
// after a rejected rate-change attempt. Satisfaction of ADR-012's HT-2 gate text
// requires: (1) inspecting the AU's shouldChangeToFormat BOOL + NSError return,
// (2) confirming prior-configuration output is bit-identical after the rejection.
// Runs at both supported rates (48kHz, 44.1kHz per ADR-010) to exercise both the
// "prior config survives rejection" property for each.
- (void)testHT2RateNegotiationRejectsUnsupportedRateAndPreservesPriorOutput {
  constexpr size_t frameCount = 65536;
  constexpr std::array<double, 2> priorRates = {48000.0, 44100.0};

  for (double priorRate : priorRates) {
    NSLog(@"[AetherfieldHarness] HT-2 testing unsupported-rate rejection with "
          @"prior config at %.1f Hz", priorRate);

    // Step 1: instantiate, configure at supported rate, allocate and render.
    AUAudioUnit *unit = [self newAetherfieldUnit];
    XCTAssertNotNil(unit, @"HT-2: AU instantiation failed");
    if (unit == nil) return;

    NSError *configurationError = nil;
    BOOL configured = [self configureUnit:unit
                              sampleRate:priorRate
                           maximumFrames:(AVAudioFrameCount)frameCount
                                   error:&configurationError];
    XCTAssertTrue(configured, @"HT-2: initial configuration failed: %@",
                  configurationError);
    if (!configured) return;

    NSError *allocationError = nil;
    BOOL allocated = [unit allocateRenderResourcesAndReturnError:&allocationError];
    XCTAssertTrue(allocated, @"HT-2: initial allocation failed: %@", allocationError);
    if (!allocated) return;

    std::vector<float> sourceL = [self inputVectorWithFrameCount:frameCount right:NO];
    std::vector<float> sourceR = [self inputVectorWithFrameCount:frameCount right:YES];
    std::vector<float> referenceL(frameCount, 0.0F);
    std::vector<float> referenceR(frameCount, 0.0F);

    AURenderBlock renderBlock = unit.renderBlock;
    XCTAssertNotNil(renderBlock, @"HT-2: renderBlock was nil");
    if (renderBlock == nil) {
      [unit deallocateRenderResources];
      return;
    }

    // Render the reference output at the prior (supported) rate.
    AUAudioUnitStatus status =
        [self renderBlock:renderBlock
                  sourceL:sourceL
                  sourceR:sourceR
                   offset:0
                   frames:(AVAudioFrameCount)frameCount
                  outputL:referenceL
                  outputR:referenceR
           sampleTimeBase:0];
    XCTAssertEqual(status, noErr, @"HT-2: reference render failed");
    [unit deallocateRenderResources];

    // Step 2: Reconfigure to unsupported rate (96 kHz), attempt allocation.
    NSError *unsupportedConfigError = nil;
    BOOL unsupportedConfigured = [self configureUnit:unit
                                         sampleRate:96000.0
                                      maximumFrames:(AVAudioFrameCount)frameCount
                                              error:&unsupportedConfigError];
    XCTAssertTrue(unsupportedConfigured,
                  @"HT-2: bus format configuration at 96 kHz should succeed "
                  "(rejection only at allocation time): %@",
                  unsupportedConfigError);

    NSError *unsupportedAllocationError = nil;
    BOOL unsupportedAllocated =
        [unit allocateRenderResourcesAndReturnError:&unsupportedAllocationError];
    XCTAssertFalse(unsupportedAllocated,
                   @"HT-2: allocation at 96 kHz should fail but succeeded");

    // Step 3: Inspect the NSError returned by the failed allocation.
    XCTAssertNotNil(unsupportedAllocationError,
                    @"HT-2: allocation rejection should have returned NSError");
    if (unsupportedAllocationError != nil) {
      NSString *expectedDescription = @"Aetherfield supports 48kHz and 44.1kHz only";
      XCTAssertEqualObjects(unsupportedAllocationError.domain,
                            NSOSStatusErrorDomain,
                            @"HT-2: NSError domain should be NSOSStatusErrorDomain");
      XCTAssertEqual((NSInteger)unsupportedAllocationError.code,
                     (NSInteger)kAudioUnitErr_FormatNotSupported,
                     @"HT-2: NSError code should be kAudioUnitErr_FormatNotSupported");
      XCTAssertEqualObjects(unsupportedAllocationError.localizedDescription,
                            expectedDescription,
                            @"HT-2: NSError description mismatch");

      NSLog(@"[AetherfieldHarness] HT-2 unsupported-rate rejection: "
            @"domain=%@ code=%ld description=%@",
            unsupportedAllocationError.domain,
            (long)unsupportedAllocationError.code,
            unsupportedAllocationError.localizedDescription);
    }

    // Step 4: Reconfigure back to prior (supported) rate, re-allocate, re-render.
    NSError *reconfigurationError = nil;
    BOOL reconfigured = [self configureUnit:unit
                                sampleRate:priorRate
                             maximumFrames:(AVAudioFrameCount)frameCount
                                     error:&reconfigurationError];
    XCTAssertTrue(reconfigured, @"HT-2: reconfiguration to prior rate failed: %@",
                  reconfigurationError);
    if (!reconfigured) return;

    NSError *reallocationError = nil;
    BOOL reallocated = [unit allocateRenderResourcesAndReturnError:&reallocationError];
    XCTAssertTrue(reallocated, @"HT-2: reallocation at prior rate failed: %@",
                  reallocationError);
    if (!reallocated) return;

    std::vector<float> candidateL(frameCount, 0.0F);
    std::vector<float> candidateR(frameCount, 0.0F);
    AURenderBlock rerenderedBlock = unit.renderBlock;
    XCTAssertNotNil(rerenderedBlock, @"HT-2: re-render block was nil");
    if (rerenderedBlock == nil) {
      [unit deallocateRenderResources];
      return;
    }

    status = [self renderBlock:rerenderedBlock
                       sourceL:sourceL
                       sourceR:sourceR
                        offset:0
                        frames:(AVAudioFrameCount)frameCount
                       outputL:candidateL
                       outputR:candidateR
                sampleTimeBase:0];
    XCTAssertEqual(status, noErr, @"HT-2: re-render at prior rate failed");

    // Step 5: bit-exact comparison — prior configuration output must be unchanged.
    BOOL leftEqual = std::memcmp(referenceL.data(), candidateL.data(),
                                 referenceL.size() * sizeof(float)) == 0;
    BOOL rightEqual = std::memcmp(referenceR.data(), candidateR.data(),
                                  referenceR.size() * sizeof(float)) == 0;
    XCTAssertTrue(leftEqual && rightEqual,
                  @"HT-2: output mismatch after rate-rejection recovery at %.1f Hz",
                  priorRate);

    NSLog(@"[AetherfieldHarness] HT-2 prior-config recovery at %.1f Hz: %s",
          priorRate, leftEqual && rightEqual ? "PASS" : "FAIL");

    [unit deallocateRenderResources];
  }

  NSLog(@"[AetherfieldHarness] HT-2 testHT2RateNegotiationRejectsUnsupportedRateAndPreservesPriorOutput PASS");
}

@end

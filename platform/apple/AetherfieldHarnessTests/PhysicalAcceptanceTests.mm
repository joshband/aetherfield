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

@end

#pragma once

#import <AudioToolbox/AudioToolbox.h>

// ADR-007: native AUAudioUnit, no JUCE. Owns exactly one
// aetherfield::dsp::DiffusionStereoPath and one
// aetherfield::wrapper::ParameterBridge (ADR-008) plus one
// aetherfield::wrapper::ResetRequest (ADR-008 section 7). No Apple or
// Objective-C type crosses into src/dsp/ or src/wrapper/ -- this header
// and its .mm are the only place that boundary is bridged.
@interface AetherfieldAudioUnit : AUAudioUnit

// ADR-011 state restore: queue a validated restore tuple to be applied at
// the next allocateRenderResourcesAndReturnError (pre-render snap) or
// drained immediately if already allocated (live restore). Control-thread
// only; never called concurrently with itself or the bridge controller.
- (void)queueStateRestore:(double)decay damp:(double)damp mix:(double)mix;

@end

#pragma once

#import <AudioToolbox/AudioToolbox.h>

// ADR-007: native AUAudioUnit, no JUCE. Owns exactly one
// aetherfield::dsp::DiffusionStereoPath and one
// aetherfield::wrapper::ParameterBridge (ADR-008) plus one
// aetherfield::wrapper::ResetRequest (ADR-008 section 7). No Apple or
// Objective-C type crosses into src/dsp/ or src/wrapper/ -- this header
// and its .mm are the only place that boundary is bridged.
@interface AetherfieldAudioUnit : AUAudioUnit
@end

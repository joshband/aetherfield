# The single-sourced list of portable DSP core files. Both the CMake
# build and the Xcode project under platform/apple/ read this exact
# list (ADR-010, "Source-list ownership between the portable build and
# the future Apple build"). Never duplicate these paths elsewhere.
set(AETHERFIELD_DSP_SOURCES
    src/dsp/GainProcessor.cpp
    src/dsp/DelayLine.cpp
    src/dsp/FeedbackDelayNetwork.cpp
    src/dsp/ParameterAutomation.cpp
    src/dsp/TailSilenceBound.cpp
    src/dsp/SchroederAllpass.cpp
    src/dsp/DiffusionStereoPath.cpp
)

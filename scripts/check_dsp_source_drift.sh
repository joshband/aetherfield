#!/usr/bin/env bash
# Fails if cmake/DspSources.cmake and the Xcode project's shared-file
# group for the DSP core name a different set of src/dsp/ paths.
# ADR-010: "a CI check (or, until CI exists, a recorded manual
# verification step)... that confirms the portable CMake target's
# source list and the Apple target's shared-file list are generated
# from, or diffed against, the same single manifest rather than
# maintained as two hand-edited lists."
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

CMAKE_LIST=$(grep -oE 'src/dsp/[A-Za-z0-9_]+\.cpp' cmake/DspSources.cmake | sort -u) || {
    echo "FAIL: no DSP sources found in cmake/DspSources.cmake"
    exit 1
}

XCODE_PROJECT="platform/apple/Aetherfield.xcodeproj/project.pbxproj"
if [[ ! -f "$XCODE_PROJECT" ]]; then
    echo "SKIP: $XCODE_PROJECT does not exist yet (Task 3 not implemented)."
    exit 0
fi

XCODE_LIST=$(grep -oE 'src/dsp/[A-Za-z0-9_]+\.cpp' "$XCODE_PROJECT" | sort -u) || {
    echo "FAIL: no DSP sources found in $XCODE_PROJECT"
    exit 1
}

if [[ "$CMAKE_LIST" != "$XCODE_LIST" ]]; then
    echo "FAIL: cmake/DspSources.cmake and $XCODE_PROJECT disagree on the DSP source list."
    diff <(echo "$CMAKE_LIST") <(echo "$XCODE_LIST") || true
    exit 1
fi

echo "OK: DSP source lists match ($(echo "$CMAKE_LIST" | wc -l | tr -d ' ') files)."

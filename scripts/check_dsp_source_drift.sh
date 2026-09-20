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

# Compared as bare filenames (see XCODE_LIST below for why).
CMAKE_LIST=$(grep -oE 'src/dsp/[A-Za-z0-9_]+\.cpp' cmake/DspSources.cmake | sed 's#^src/dsp/##' | sort -u) || {
    echo "FAIL: no DSP sources found in cmake/DspSources.cmake"
    exit 1
}

XCODE_PROJECT="platform/apple/Aetherfield.xcodeproj/project.pbxproj"
if [[ ! -f "$XCODE_PROJECT" ]]; then
    echo "SKIP: $XCODE_PROJECT does not exist yet (Task 3 not implemented)."
    exit 0
fi

# xcodegen (Task 3) does not repeat the full "src/dsp/Name.cpp" relative
# path inside each PBXFileReference -- it writes only the bare filename
# (`path = Name.cpp;`) and encodes the directory nesting once, on the
# enclosing PBXGroup itself (`name = dsp; path = ../../src/dsp;`).
# Confirmed by direct inspection of a real xcodegen-generated
# project.pbxproj (2026-09-20): a full "src/dsp/...cpp" string never
# appears anywhere in the file, which is why the original regex here
# (written for a hand-authored/GUI project that might spell out the full
# relative path per file) always found zero matches -- a path-format
# false mismatch, not real drift; the two build systems can still be
# checked against the same manifest by comparing bare filenames scoped
# to the "dsp" PBXGroup specifically, so a same-named file accidentally
# added to some other group (src/wrapper, src/auv3) would not be
# mistaken for a src/dsp/ file.
XCODE_LIST=$(sed -n '/\/\* dsp \*\/ = {/,/^\t\t};/p' "$XCODE_PROJECT" | grep -oE '[A-Za-z0-9_]+\.cpp' | sort -u)
if [[ -z "$XCODE_LIST" ]]; then
    echo "FAIL: no DSP sources found in $XCODE_PROJECT"
    exit 1
fi

if [[ "$CMAKE_LIST" != "$XCODE_LIST" ]]; then
    echo "FAIL: cmake/DspSources.cmake and $XCODE_PROJECT disagree on the DSP source list."
    diff <(echo "$CMAKE_LIST") <(echo "$XCODE_LIST") || true
    exit 1
fi

echo "OK: DSP source lists match ($(echo "$CMAKE_LIST" | wc -l | tr -d ' ') files)."

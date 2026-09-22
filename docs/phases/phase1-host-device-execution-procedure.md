# Host/device acceptance — execution procedure

**Device:** iPhone 16 Pro Max, iOS 27.0 (build 24A437) — newest-tier iPhone  
**Host:** AUM (capability matrix to verify: rate changes, offline export, automation, bypass, transport-triggered reset)  
**Harness:** `AetherfieldHarnessTests` (discovery, instantiation, HT-1, HT-3)  
**Configuration:** Release, signed with Team ID `W2VVZU52J6`

---

## Setup checklist

- [ ] Device attached and developer mode enabled
- [ ] AUM installed and verified working (open, confirm UI responds)
- [ ] Xcode 16.x confirmed installed with iOS 27 SDK
- [ ] Latest source at commit `99a02b2` (or later, if the baseline has moved)
- [ ] `~/Library/Developer/Xcode/DerivedData` and prior build artifacts cleared (optional but recommended for clean run)

---

## Build command

```sh
cd /Users/artbox/Documents/Repos/aetherfield

# Configure and build Release suite (portable baseline verification)
cmake -S . -B build/host-device-acceptance -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-device-acceptance --parallel
ctest --test-dir build/host-device-acceptance --output-on-failure
bash scripts/check_dsp_source_drift.sh
```

**Expected outcome:**
- CTest: 8/8 suites passed
- DSP source drift: 7 files matched
- All unsigned AU Debug compile succeeded

---

## Sign and build the extension/container for device

```sh
# Generate Xcode project from pinned XcodeGen
xcodegen generate --spec platform/apple/project.yml --project-root platform/apple

# Build and sign for device (Release configuration)
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldAUExtension \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  DEVELOPMENT_TEAM=W2VVZU52J6 \
  CODE_SIGN_STYLE=Automatic \
  -allowProvisioningUpdates \
  clean build

xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHost \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  DEVELOPMENT_TEAM=W2VVZU52J6 \
  CODE_SIGN_STYLE=Automatic \
  -allowProvisioningUpdates \
  clean build
```

**Expected outcome:**
- Both exit code 0
- `codesign -dvv` on resulting `.app` and `.appex` show `TeamIdentifier=W2VVZU52J6`
- No code-signing warnings

---

## Install to device

```sh
# Locate the built container app
APP_PATH=$(find ~/Library/Developer/Xcode/DerivedData -name "AetherfieldHost.app" -type d | head -1)

# Install via xcodebuild
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHost \
  -configuration Release \
  -destination 'id=<device-id>' \
  install

# Verify installation
# Option 1: Manual check via Xcode Organizer or iPhone Settings > General > Apps
# Option 2: Verify extension registration
pluginkit -m | grep -i aetherfield
```

**Expected outcome:**
- App installs successfully without errors
- Extension visible in system plugin registry: `...AetherfieldHost.AetherfieldAUExtension`

---

## Run HT-1 (allocation/render/deallocation cycles)

**Purpose:** Verify 100 same-instance cycles + 100 fresh instantiate/render/destroy cycles at both rates. No sample-value oracle; this is lifecycle/resource stability only.

```sh
# Run focused HT-1 test on device
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHost \
  -configuration Release \
  -destination 'id=<device-id>' \
  test \
  -testPlan AetherfieldHarnessTests \
  -only-testing 'AetherfieldHarnessTests/PhysicalAcceptanceTests/testHT1LifecycleStress'
```

**Expected outcome:**
- Test passed (0 failures)
- 200 total allocation/render/deallocation cycles completed (100 same-instance + 100 fresh) at both 44.1 kHz and 48 kHz
- No crashes, timeouts, or resource exhaustion

**What to watch for:**
- Any assertion failures or exceptions in the test output
- Device becoming unresponsive during the test
- XCTest runner timeout (indicates a hung render)

---

## Run HT-3 (partitioned rendering, bit-exact comparison)

**Purpose:** Verify bit-exact output across multiple partition sizes, including the zero-frame edge case (known Apple defect, expected to mismatch on partition `{0,1,13,64,512,977,1024,3,0}` mid-stream after ~1191 samples).

```sh
# Run focused HT-3 test on device
xcodebuild -project platform/apple/Aetherfield.xcodeproj \
  -scheme AetherfieldHost \
  -configuration Release \
  -destination 'id=<device-id>' \
  test \
  -testPlan AetherfieldHarnessTests \
  -only-testing 'AetherfieldHarnessTests/PhysicalAcceptanceTests/testHT3PartitionedRenderingBitExact'
```

**Expected outcome:**
- Test execution completes
- Bit-exact passes on: `{4096}`, and two other fixed partition sets at both rates
- Known mismatch on: `{0,1,13,64,512,977,1024,3,0}` diverging mid-stream (~1191 samples in, coinciding with FDN 27 ms minimum delay)
- Zero-frame edge case documented as **accepted external constraint** (Apple out-of-process AU proxy pull-skip defect, verified cross-product: Eventide Blackhole, Audio Damage Eos 2)

**What to watch for:**
- Any crash or timeout (indicates a new problem, not the known defect)
- Output file I/O errors or assertion failures outside the expected mismatch
- If the mismatch location changes significantly (suggests a new issue)

---

## Collect evidence

After both tests pass (or document known failures), capture the run results:

```sh
# Get the XCTest result bundle path (printed by xcodebuild)
RESULT_BUNDLE="/tmp/path/to/Test-*.xcresult"

# Create run directory with UTC timestamp and short SHA
RUN_ID="$(date -u +%Y%m%d-%H%M%S)-$(git rev-parse --short HEAD)"
mkdir -p artifacts/host-device/$RUN_ID/{logs,audio}

# Copy test results
cp -r "$RESULT_BUNDLE" artifacts/host-device/$RUN_ID/logs/

# Extract test logs
xcrun xcresulttool get --path "$RESULT_BUNDLE" --format json > \
  artifacts/host-device/$RUN_ID/logs/xcresult.json

# Collect any audio outputs (if harness writes them)
# Check for audio files in the harness working directory and copy to artifacts/host-device/$RUN_ID/audio/
```

---

## Fill the execution manifest

Create `artifacts/host-device/$RUN_ID/manifest.json`:

```json
{
  "run_id": "YYYYMMDD-HHMMSS-<short-sha>",
  "timestamp_utc": "2026-09-21T22:30:00Z",
  "source_sha": "<full-commit-hash>",
  "source_dirty": false,
  "device": {
    "model": "iPhone 16 Pro Max",
    "chip": "A18 Pro",
    "os_name": "iOS",
    "os_version": "27.0",
    "os_build": "24A437"
  },
  "host": {
    "name": "AUM",
    "version": "<as-shown-in-app-settings>",
    "capabilities": {
      "rate_changes": true_or_false,
      "offline_export": true_or_false,
      "automation": true_or_false,
      "bypass": true_or_false,
      "transport_reset": true_or_false
    }
  },
  "build": {
    "configuration": "Release",
    "signing_team": "W2VVZU52J6",
    "xcode_version": "<output-of-xcodebuild -version>",
    "sdk_version": "iOS 27.0"
  },
  "tests": {
    "ht1_lifecycle": {
      "status": "pass_or_fail_or_blocked",
      "cycles": 200,
      "rates_tested": [44100, 48000],
      "notes": ""
    },
    "ht3_partitioned": {
      "status": "pass_with_known_constraint",
      "partition_sets_exact": 3,
      "known_mismatch": {
        "partition_set": [0, 1, 13, 64, 512, 977, 1024, 3, 0],
        "divergence_onset_samples": 1191,
        "reason": "Apple out-of-process AU proxy pull-skip after zero-frame call (verified cross-product: Eventide Blackhole, Audio Damage Eos 2)"
      },
      "notes": ""
    },
    "device_matrix_coverage": {
      "device_corners_tested": 1,
      "device_corners_total": 4,
      "tested_corners": ["newest-iPhone"],
      "unmeasured_corners": ["oldest-iPhone-A13", "oldest-iPad-A12", "newest-iPad-Pro"],
      "reason": "Single device available; full matrix authorization exists, execution deferred pending hardware availability"
    }
  },
  "notes": "Execution procedure: docs/phases/phase1-host-device-execution-procedure.md"
}
```

---

## Reporting back

Once tests complete, provide:

1. **Exit codes** for both HT-1 and HT-3 tests
2. **Manifest JSON** (filled with actual values)
3. **Any unexpected failures** or deviations from the known mismatch
4. **AUM capability matrix** (which features work: rate changes, offline export, etc.)

Update `docs/testing.md` with a new section:

```markdown
### Host/device acceptance physical run (2026-09-21)

**Device:** iPhone 16 Pro Max, iOS 27.0, build 24A437  
**Execution procedure:** [phase1-host-device-execution-procedure.md](phases/phase1-host-device-execution-procedure.md)  
**Manifest:** `artifacts/host-device/<run-id>/manifest.json`  
**Result:** [see manifest and logs]

- [ ] HT-1 passed
- [ ] HT-3 passed (with known zero-frame constraint documented)
- [ ] Matrix coverage recorded (1/4 corners)
- [ ] AUM capabilities verified
```

---

## Known issues and constraints

| Issue | Scope | Mitigation |
|-------|-------|-----------|
| Zero-frame input-pull skip | HT-3 partition `{0,1,13,64,512,977,1024,3,0}` mid-stream mismatch | Documented as accepted external constraint (Apple AUv3 proxy, not product code) |
| Single device availability | Only newest-iPhone tested; oldest-iPhone A13, iPad A12, iPad Pro unmeasured | Record as partial matrix; full authorization exists for future testing |
| AUM host capabilities | Offline automation, bypass transition behavior untested | Record in manifest; coverage gap noted if unsupported |

---

## Next steps after this run

1. Report results (exit codes, manifest, AUM capabilities)
2. Document in `docs/testing.md` under "Host/device acceptance physical run"
3. If all HT-1/HT-3 cases pass (with known constraints documented), Task 1 is **complete for available hardware**
4. Remaining HT-2, HT-4, HT-5, HT-6, HT-11, HT-12 are deferred pending full four-device matrix availability or separate authorization
5. No implementation changes are authorized by test results; findings become a separate scoped-repair process if needed

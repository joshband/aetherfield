#!/bin/bash
# Task 1 physical-device HT-1/HT-3 test execution
# Device prerequisite: iPhone 16 Pro Max connected, developer mode enabled
# Run from project root: bash scripts/run_ht1_ht3_physical.sh

set -e

# Detect connected iOS device using Xcode-compatible format
# Extract from xcodebuild -showdestinations output
DEVICE_INFO=$(xcodebuild -project platform/apple/Aetherfield.xcodeproj -scheme AetherfieldHarness -showdestinations 2>&1 | grep "{ platform:iOS" | grep -v "Simulator" | head -1)
if [ -z "$DEVICE_INFO" ]; then
  echo "[Task 1] ERROR: No connected iOS device found"
  echo "[Task 1] Connect an iOS device and ensure developer mode is enabled"
  exit 1
fi
# Extract device ID from format: { platform:iOS, arch:arm64, id:00008140-001A6D9C21BB001C, name:Josh's iPhone }
DEVICE_ID=$(echo "$DEVICE_INFO" | grep -oE "id:[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}" | cut -d: -f2)
DEVICE_NAME=$(echo "$DEVICE_INFO" | grep -oE "name:[^}]*" | cut -d: -f2-)
DEVICE_DESTINATION="generic/platform=iOS"
SCHEME="AetherfieldHarness"
TEST_TARGET="AetherfieldHarnessTests"
CONFIGURATION="Release"

echo "[Task 1] Starting HT-1/HT-3 physical-device acceptance tests"
echo "[Task 1] Device UUID: $DEVICE_UUID"
echo "[Task 1] Device destination: $DEVICE_DESTINATION"
echo "[Task 1] Configuration: $CONFIGURATION"
echo ""

# HT-1: Lifecycle/instantiation smoke test (100 same-instance, 100 fresh cycles at both rates)
echo "[Task 1] Running HT-1 lifecycle/instantiation cycles..."
xcodebuild test \
  -project platform/apple/Aetherfield.xcodeproj \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  -destination "$DEVICE_DESTINATION" \
  -only-testing "AetherfieldHarnessTests/AetherfieldPhysicalAcceptanceTests/testHT1LifecycleSmokeAtBothRates" \
  -allowProvisioningUpdates \
  2>&1 | tee /tmp/ht1_result.log
HT1_EXIT=${PIPESTATUS[0]}
echo "[Task 1] HT-1 exit code: $HT1_EXIT"
if [ $HT1_EXIT -ne 0 ]; then
  echo "[Task 1] WARNING: HT-1 returned non-zero exit; check /tmp/ht1_result.log"
fi
echo ""

# HT-3: Partition bit-exactness at both rates with four partition sets
#   - {4096} — single large buffer
#   - {1, 13, 64, 512, 3} — fixed small partitions
#   - {7, 29, 3, 211, 5} — ragged small partitions
#   - {0, 1, 13, 64, 512, 977, 1024, 3, 0} — with leading/trailing zeros (known Apple host-layer issue)
echo "[Task 1] Running HT-3 partition determinism tests..."
xcodebuild test \
  -project platform/apple/Aetherfield.xcodeproj \
  -scheme "$SCHEME" \
  -configuration "$CONFIGURATION" \
  -destination "$DEVICE_DESTINATION" \
  -only-testing "AetherfieldHarnessTests/AetherfieldPhysicalAcceptanceTests/testHT3FixedAndRaggedPartitionsAreBitExactAtBothRates" \
  -allowProvisioningUpdates \
  2>&1 | tee /tmp/ht3_result.log
HT3_EXIT=${PIPESTATUS[0]}
echo "[Task 1] HT-3 exit code: $HT3_EXIT"
if [ $HT3_EXIT -ne 0 ]; then
  echo "[Task 1] WARNING: HT-3 returned non-zero exit; check /tmp/ht3_result.log"
fi
echo ""

# Summary
echo "=========================================="
echo "[Task 1] Test Results Summary"
echo "=========================================="
echo "HT-1 (lifecycle):        $([ $HT1_EXIT -eq 0 ] && echo 'PASS' || echo 'FAIL') (exit $HT1_EXIT)"
echo "HT-3 (partitions):       $([ $HT3_EXIT -eq 0 ] && echo 'PASS' || echo 'FAIL') (exit $HT3_EXIT)"
echo ""
echo "Log files:"
echo "  HT-1: /tmp/ht1_result.log"
echo "  HT-3: /tmp/ht3_result.log"
echo ""

if [ $HT1_EXIT -eq 0 ] && [ $HT3_EXIT -eq 0 ]; then
  echo "[Task 1] All tests PASSED ✓"
  exit 0
else
  echo "[Task 1] Some tests FAILED — review logs above"
  exit 1
fi

#!/bin/bash
# ci-audio-test.sh — Audio driver CI/CD test runner
#
# Usage:
#   ./ci-audio-test.sh                      # Run on Nucleo (requires hardware)
#   ./ci-audio-test.sh --help               # Show this message
#
# This script:
#   1. Builds the audio test suite
#   2. Flashes to Nucleo-L476RG
#   3. Captures UART output
#   4. Parses test results
#   5. Returns exit code 0 if all tests PASS, 1 if any FAIL

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/cosmo-stm32"
BUILD_LOG="${SCRIPT_DIR}/build-audio-test.log"
UART_LOG="${SCRIPT_DIR}/uart-audio-test.log"
TEST_TIMEOUT_SECS=60

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_usage() {
    cat << 'EOF'
Audio Driver CI/CD Test Runner

Usage:
  ./ci-audio-test.sh          Build, flash, and run audio tests
  ./ci-audio-test.sh --help   Show this message
  ./ci-audio-test.sh --build-only   Just build, don't flash or test
  ./ci-audio-test.sh --no-flash     Build and monitor, don't flash

Environment:
  SKIP_FLASH=1          Skip flashing (use existing firmware)
  TEST_TIMEOUT=120      Override test timeout (default: 60 seconds)

EOF
}

log_info() {
    echo -e "${GREEN}[INFO]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

log_step() {
    echo -e "\n${GREEN}════════════════════════════════════════${NC}"
    echo -e "${GREEN}  $*${NC}"
    echo -e "${GREEN}════════════════════════════════════════${NC}\n"
}

# Parse command line
BUILD_ONLY=0
NO_FLASH=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --help)
            print_usage
            exit 0
            ;;
        --build-only)
            BUILD_ONLY=1
            shift
            ;;
        --no-flash)
            NO_FLASH=1
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Override timeout if specified
TEST_TIMEOUT="${TEST_TIMEOUT_SECS}"

log_step "Audio Driver CI Test Suite"
log_info "Project directory: ${PROJECT_DIR}"
log_info "Build log: ${BUILD_LOG}"
log_info "UART log: ${UART_LOG}"

# Step 1: Check prerequisites
log_step "Step 1: Checking prerequisites"

if ! command -v pio &> /dev/null; then
    log_error "PlatformIO not found. Install with: pip install platformio"
    exit 1
fi

if ! command -v python3 &> /dev/null; then
    log_error "Python 3 not found"
    exit 1
fi

log_info "PlatformIO version: $(pio --version)"

# Step 2: Build
log_step "Step 2: Building audio test suite"

cd "${PROJECT_DIR}"

if ! pio run -e nucleo_l476rg_audio_test -v 2>&1 | tee "${BUILD_LOG}"; then
    log_error "Build failed. See ${BUILD_LOG}"
    exit 1
fi

if ! grep -q "audio_driver_tests.c" "${BUILD_LOG}"; then
    log_warn "audio_driver_tests.c not found in build log. Check build_src_filter."
fi

if ! grep -q "audio_driver_tests.h" "${BUILD_LOG}"; then
    log_warn "audio_driver_tests.h not found in build log. Build may be incomplete."
fi

log_info "✓ Build successful"

if [[ $BUILD_ONLY -eq 1 ]]; then
    log_step "Build-only mode: exiting"
    exit 0
fi

# Step 3: Flash (unless skipped)
if [[ -z "${SKIP_FLASH:-}" ]] && [[ $NO_FLASH -eq 0 ]]; then
    log_step "Step 3: Flashing board"
    
    if ! pio run -e nucleo_l476rg_audio_test -t upload 2>&1 | tee -a "${BUILD_LOG}"; then
        log_error "Flash failed"
        exit 1
    fi
    
    log_info "✓ Flash successful"
    # Give board time to reset
    sleep 2
else
    log_warn "Skipping flash (using existing firmware)"
fi

# Step 4: Capture and parse UART output
log_step "Step 4: Running tests (timeout: ${TEST_TIMEOUT}s)"

# Create a Python script to capture UART with timeout
UART_CAPTURE_SCRIPT=$(mktemp)
cat > "${UART_CAPTURE_SCRIPT}" << 'PYTHON_SCRIPT'
import sys
import time
import serial
from serial.tools.list_ports import comports

timeout = int(sys.argv[1]) if len(sys.argv) > 1 else 60

# Find ST-LINK serial port
port = None
for p in comports():
    if 'ST-LINK' in p.description or 'STM32' in p.description or 'ttyACM' in p.device:
        port = p.device
        break

if not port:
    print("ERROR: ST-LINK serial port not found", file=sys.stderr)
    sys.exit(1)

print(f"[INFO] Opening {port}...", file=sys.stderr)

try:
    ser = serial.Serial(port, 115200, timeout=1)
    ser.reset_input_buffer()
    
    start_time = time.time()
    output = ""
    
    while time.time() - start_time < timeout:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            output += chunk
            sys.stdout.write(chunk)
            sys.stdout.flush()
            
            # Check for test completion markers
            if "✓ ALL TESTS PASSED" in output or "✗" in output and "FAIL" in output:
                time.sleep(1)  # Wait a bit for any trailing output
                break
        else:
            time.sleep(0.1)
    
    ser.close()
    
    # Write output to file
    with open(sys.argv[2], 'w') as f:
        f.write(output)
    
    sys.exit(0)

except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(1)
PYTHON_SCRIPT

if python3 "${UART_CAPTURE_SCRIPT}" "${TEST_TIMEOUT}" "${UART_LOG}" 2>&1; then
    log_info "✓ UART capture successful"
else
    log_warn "UART capture script exited with error"
fi

# Step 5: Parse results
log_step "Step 5: Parsing test results"

if [[ ! -f "${UART_LOG}" ]]; then
    log_error "UART log not found: ${UART_LOG}"
    exit 1
fi

# Extract summary line
if grep -q "ALL TESTS PASSED" "${UART_LOG}"; then
    log_info "✓✓✓ ALL TESTS PASSED ✓✓✓"
    PASSED_COUNT=$(grep "Passed:" "${UART_LOG}" | grep -oE '[0-9]+' | head -1)
    FAILED_COUNT=$(grep "Failed:" "${UART_LOG}" | grep -oE '[0-9]+' | head -1)
    SKIPPED_COUNT=$(grep "Skipped:" "${UART_LOG}" | grep -oE '[0-9]+' | head -1)
    echo ""
    echo "Results:"
    echo "  Passed:  ${PASSED_COUNT:-?}"
    echo "  Failed:  ${FAILED_COUNT:-0}"
    echo "  Skipped: ${SKIPPED_COUNT:-?}"
    echo ""
    TEST_RESULT=0
else
    log_error "Test failures detected"
    grep "FAIL" "${UART_LOG}" || true
    TEST_RESULT=1
fi

# Step 6: Summary
log_step "Test Suite Complete"

if [[ $TEST_RESULT -eq 0 ]]; then
    log_info "Status: PASSED"
    log_info "UART output: ${UART_LOG}"
    exit 0
else
    log_error "Status: FAILED"
    log_error "UART output: ${UART_LOG}"
    echo ""
    echo "Last 50 lines of UART output:"
    tail -50 "${UART_LOG}"
    exit 1
fi

rm -f "${UART_CAPTURE_SCRIPT}"

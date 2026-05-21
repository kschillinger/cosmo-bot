#!/usr/bin/env python3
"""
Quick test runner for audio driver tests
"""
import subprocess
import sys
import os

os.chdir(r"C:\Users\kschi\Documents\GitHub\cosmo-bot.worktrees\agents-audio-driver-testing-guide\cosmo-stm32")

print("=" * 70)
print("AUDIO DRIVER TEST SUITE — Quick Test Runner")
print("=" * 70)
print()

print("[1/3] Building test suite...")
print("      Command: pio run -e nucleo_l476rg_audio_test")
print()

try:
    result = subprocess.run(
        ["pio", "run", "-e", "nucleo_l476rg_audio_test"],
        capture_output=True,
        text=True,
        timeout=180
    )
    
    if result.returncode == 0:
        print("✓ Build successful!")
        print()
        
        # Show key build artifacts
        if "audio_driver_tests.c" in result.stdout or "audio_driver_tests.c" in result.stderr:
            print("✓ audio_driver_tests.c compiled")
        if "audio_driver_tests.h" in result.stdout or "audio_driver_tests.h" in result.stderr:
            print("✓ audio_driver_tests.h included")
        if "audio_test_main.c" in result.stdout or "audio_test_main.c" in result.stderr:
            print("✓ audio_test_main.c compiled")
            
    else:
        print("✗ Build failed!")
        print()
        print("STDERR:")
        print(result.stderr[-1000:] if len(result.stderr) > 1000 else result.stderr)
        sys.exit(1)
        
except subprocess.TimeoutExpired:
    print("✗ Build timeout (>3 min)")
    sys.exit(1)
except Exception as e:
    print(f"✗ Error: {e}")
    sys.exit(1)

print()
print("[2/3] Checking board connection...")
print("      Looking for Nucleo-L476RG...")
print()

try:
    result = subprocess.run(
        ["pio", "device", "list"],
        capture_output=True,
        text=True,
        timeout=30
    )
    
    if "ST-LINK" in result.stdout or "STM32" in result.stdout or "nucleo" in result.stdout.lower():
        print("✓ Board detected!")
        print()
        print("Device list:")
        for line in result.stdout.split("\n"):
            if line.strip():
                print(f"  {line}")
    else:
        print("⚠ Board not detected (may not be connected)")
        print()
        print("To run tests, connect Nucleo-L476RG via USB and run:")
        print("  pio run -e nucleo_l476rg_audio_test -t upload")
        print("  pio device monitor --baud 115200")
        
except Exception as e:
    print(f"⚠ Could not check devices: {e}")

print()
print("[3/3] Next steps...")
print()
print("To complete testing:")
print()
print("1. Connect Nucleo-L476RG via USB (if not already connected)")
print()
print("2. Flash the test suite:")
print("   pio run -e nucleo_l476rg_audio_test -t upload")
print()
print("3. Watch test results on UART:")
print("   pio device monitor --baud 115200")
print()
print("Expected output (after flashing):")
print("   ╔═══════════════════════════════════════════════════════════╗")
print("   ║         Audio Driver Test Suite (Bring-up Guide)          ║")
print("   ╚═══════════════════════════════════════════════════════════╝")
print()
print("   Phase 0-1: Initialization")
print("   ───────────────────────────────────────────────────────────")
print("   [✓ PASS] audio_init() completes")
print("   [✓ PASS] Playback initial state (DONE)")
print("   ... (more tests)")
print()
print("   ✓ ALL TESTS PASSED")
print()
print("=" * 70)

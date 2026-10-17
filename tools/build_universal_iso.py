#!/usr/bin/env python3
"""
Builds the Remastered Universal Wii ISO and Dolphin RVZ featuring:
1. High-definition remastered cutscenes converted from NVIDIA Shield masters.
2. Multi-language support (English, French, Spanish) with in-game / console language swap.
3. Compatibility with Dolphin Emulator, USB Loaders, and original Wii hardware.
"""
import os
import subprocess
import sys
import time

def build_universal_iso():
    tools_dir = os.path.abspath("tools")
    wit_exe = os.path.join(tools_dir, "RiivolutionIsoBuilder", "RiivolutionIsoBuilder", "bin", "Release", "tools", "wit.exe")
    dolphintool_exe = os.path.join(tools_dir, "dolphin", "DolphinTool.exe")
    data_dir = os.path.abspath("extracted/DATA")
    output_iso = os.path.abspath("roms/Super_Mario_Galaxy_Universal_Remastered.iso")
    output_rvz = os.path.abspath("roms/Super_Mario_Galaxy_Universal_Remastered.rvz")

    if not os.path.exists(wit_exe):
        print(f"Error: wit.exe not found at {wit_exe}")
        return False

    if not os.path.isdir(data_dir):
        print(f"Error: Extracted disc directory not found at {data_dir}")
        return False

    print("=============================================================")
    print("BUILDING REMASTERED UNIVERSAL WII ISO (CUTSCENES + MULTI-LANG)")
    print("=============================================================")
    print(f"Source Directory : {data_dir}")
    print(f"Target ISO       : {output_iso}")
    print(f"Target RVZ       : {output_rvz}")
    print("-------------------------------------------------------------")

    # Step 1: Build the ISO using wit copy
    print("\n[1/2] Packaging 1:1 Wii ISO with Wiimms ISO Tools (wit)...")
    start_time = time.time()
    wit_cmd = [
        wit_exe,
        "copy",
        data_dir,
        output_iso,
        "--id", "RMGE01",
        "--name", "Super Mario Galaxy Remastered Universal",
        "-vv"
    ]
    res_wit = subprocess.run(wit_cmd, capture_output=True, text=True)
    if res_wit.returncode != 0:
        print("wit error:\n", res_wit.stderr)
        return False

    if os.path.exists(output_iso):
        iso_size = os.path.getsize(output_iso) / (1024 * 1024 * 1024)
        elapsed = time.time() - start_time
        print(f"Successfully generated {output_iso} ({iso_size:.2f} GB) in {elapsed:.1f}s")
    else:
        print("Error: Output ISO was not created.")
        return False

    # Step 2: Convert to Dolphin RVZ format
    if os.path.exists(dolphintool_exe):
        print("\n[2/2] Converting ISO to Dolphin RVZ format (Zstandard Level 5, 128 KiB blocks)...")
        start_rvz = time.time()
        rvz_cmd = [
            dolphintool_exe,
            "convert",
            "-i", output_iso,
            "-o", output_rvz,
            "-f", "rvz",
            "-c", "zstd",
            "-l", "5",
            "-b", "131072"
        ]
        res_rvz = subprocess.run(rvz_cmd, capture_output=True, text=True)
        if res_rvz.returncode == 0 and os.path.exists(output_rvz):
            rvz_size = os.path.getsize(output_rvz) / (1024 * 1024 * 1024)
            elapsed_rvz = time.time() - start_rvz
            print(f"Successfully created {output_rvz} ({rvz_size:.2f} GB) in {elapsed_rvz:.1f}s")
        else:
            print("Note: RVZ conversion failed or skipped:", res_rvz.stderr)
    else:
        print(f"DolphinTool.exe not found at {dolphintool_exe}; skipping RVZ creation.")

    print("\n=============================================================")
    print("UNIVERSAL REMASTERED DISC IMAGES READY!")
    print("=============================================================")
    return True

if __name__ == "__main__":
    build_universal_iso()

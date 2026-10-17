#!/usr/bin/env python3
"""
Tool to overlay NVIDIA Shield Chinese Remaster HD assets onto an original
Wii Super Mario Galaxy extracted filesystem, and provide instructions / automation
for building a remastered Wii ISO and compressed Dolphin RVZ disc image.
"""
import argparse
import os
import shutil
import sys

def merge_assets(wii_files_dir: str, shield_unpacked_dir: str, dry_run: bool = False):
    print("=== Super Mario Galaxy Remaster Asset Merger ===")
    print(f"Original Wii Files Directory : {wii_files_dir}")
    print(f"NVIDIA Shield Assets         : {shield_unpacked_dir}")
    print("------------------------------------------------")

    if not os.path.isdir(wii_files_dir):
        print(f"Error: Wii files directory does not exist: {wii_files_dir}")
        return False

    if not os.path.isdir(shield_unpacked_dir):
        print(f"Error: NVIDIA Shield assets directory does not exist: {shield_unpacked_dir}")
        return False

    replaced_count = 0
    added_count = 0

    for root, _, files in os.walk(shield_unpacked_dir):
        rel_path = os.path.relpath(root, shield_unpacked_dir)
        target_dir = os.path.normpath(os.path.join(wii_files_dir, rel_path))

        for file in files:
            src_file = os.path.join(root, file)
            dst_file = os.path.join(target_dir, file)

            if os.path.exists(dst_file):
                replaced_count += 1
                action = "REPLACE"
            else:
                added_count += 1
                action = "ADD    "

            if not dry_run:
                os.makedirs(target_dir, exist_ok=True)
                shutil.copy2(src_file, dst_file)

            if (replaced_count + added_count) <= 25 or (replaced_count + added_count) % 100 == 0:
                print(f"[{action}] {os.path.relpath(dst_file, wii_files_dir)}")

    print("------------------------------------------------")
    print(f"Summary: Replaced {replaced_count} files, Added {added_count} files.")
    if dry_run:
        print("(Dry run: no changes written to disk)")
    return True

def print_rvz_instructions(disc_root_dir: str, output_iso: str = "smg_remastered.iso", output_rvz: str = "smg_remastered.rvz"):
    print("\n" + "=" * 60)
    print("HOW TO BUILD THE REMASTERED WII ISO AND RVZ")
    print("=" * 60)
    print("Step 1: Pack the extracted/merged disc directory into an ISO using Wiimms ISO Tools (wit):")
    print(f"   wit copy \"{disc_root_dir}\" \"{output_iso}\"")
    print("\nStep 2: Convert the resulting ISO to Dolphin RVZ format:")
    print("   Option A - Via Dolphin GUI:")
    print("     1. Open Dolphin Emulator.")
    print("     2. Add the folder containing " + output_iso + " to Dolphin's game list.")
    print("     3. Right-click the game in the list -> 'Convert File...'.")
    print("     4. Choose Format: 'RVZ', Compression: 'Zstandard', Level: '5', Block Size: '128 KiB'.")
    print(f"     5. Click 'Convert...' and save as {output_rvz}.")
    print("\n   Option B - Via Dolphin Command Line:")
    print(f"     dolphin-emu-nogui -c \"{output_iso}\" -o \"{output_rvz}\"")
    print("\nStep 3: Play in Dolphin or native engine:")
    print("   - For Dolphin: Load the RVZ to enjoy 1080p remastered UI and assets.")
    print("   - For Native Android/PC Decomp: Run `python tools/cook/cook.py` directly on the merged folder")
    print("     to convert assets directly into native Vulkan endianness without disc emulation overhead.")
    print("=" * 60)

def main():
    parser = argparse.ArgumentParser(description="Merge NVIDIA Shield assets with Wii game files and guide RVZ packaging.")
    parser.add_argument("--wii-files", default="disc_extracted/files", help="Path to extracted Wii files/ directory")
    parser.add_argument("--shield-assets", default="shield_extracted/unpacked_assets", help="Path to unpacked Shield assets")
    parser.add_argument("--disc-root", default="disc_extracted", help="Root directory of the extracted Wii disc (contains DATA/ or files/ and sys/)")
    parser.add_argument("--dry-run", action="store_true", help="Simulate merge without writing changes")
    args = parser.parse_args()

    merge_assets(args.wii_files, args.shield_assets, dry_run=args.dry_run)
    print_rvz_instructions(args.disc_root)

if __name__ == '__main__':
    main()

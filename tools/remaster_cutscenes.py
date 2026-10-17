#!/usr/bin/env python3
"""
Converts all NVIDIA Shield Remastered 1080p MP4 cutscenes into
high-definition, artifact-free Nintendo Wii THP files for the Universal Remastered ISO.
"""
import glob
import os
import shutil
import subprocess
import sys
import time

def remaster_all_cutscenes(shield_movie_dir: str, target_movie_dir: str):
    tools_dir = os.path.abspath("tools")
    thpconv_exe = os.path.join(tools_dir, "THPConv.exe")
    dsptool_dll = os.path.join(tools_dir, "dsptool.dll")

    if not os.path.exists(thpconv_exe):
        print("Error: THPConv.exe not found in tools/")
        return False

    if not os.path.exists(dsptool_dll):
        print("Error: dsptool.dll not found in tools/")
        return False

    os.makedirs(target_movie_dir, exist_ok=True)
    mp4_files = sorted(glob.glob(os.path.join(shield_movie_dir, "*.mp4")))
    if not mp4_files:
        print(f"Error: No MP4 files found in {shield_movie_dir}")
        return False

    print("=======================================================")
    print("REMASTERING WII THP CUTSCENES FROM NVIDIA SHIELD MASTERS")
    print("=======================================================")
    print(f"Source Directory : {shield_movie_dir}")
    print(f"Target Directory : {target_movie_dir}")
    print(f"Found {len(mp4_files)} cutscenes to remaster.\n")

    temp_dir = os.path.join(tools_dir, "temp_cutscene_build")

    for idx, mp4_path in enumerate(mp4_files, 1):
        base_name = os.path.splitext(os.path.basename(mp4_path))[0]
        if base_name.lower() == "dummy":
            continue

        output_thp = os.path.join(target_movie_dir, f"{base_name}.thp")
        print(f"[{idx}/{len(mp4_files)}] Processing: {base_name}.mp4 -> {base_name}.thp")
        start_time = time.time()

        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir, ignore_errors=True)
        os.makedirs(temp_dir, exist_ok=True)

        # 1. Extract frames using FFmpeg with high-quality Lanczos scaling and q=1 JPEG
        # 640x368 is the native 16:9 widescreen buffer for Super Mario Galaxy
        print("  - Extracting video frames (Lanczos 16:9, max JPEG quality)...")
        ffmpeg_v = [
            "ffmpeg", "-y", "-i", mp4_path,
            "-vf", "scale=640:368:flags=lanczos",
            "-q:v", "1",
            os.path.join(temp_dir, "f_%05d.jpg")
        ]
        res_v = subprocess.run(ffmpeg_v, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if res_v.returncode != 0:
            print(f"  Error extracting frames: {res_v.stderr.decode('utf-8', errors='ignore')}")
            continue

        # 2. Extract 32000Hz 16-bit stereo PCM audio
        print("  - Extracting 32kHz stereo audio...")
        audio_wav = os.path.join(temp_dir, "audio.wav")
        ffmpeg_a = [
            "ffmpeg", "-y", "-i", mp4_path,
            "-ar", "32000", "-ac", "2",
            audio_wav
        ]
        subprocess.run(ffmpeg_a, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # 3. Pack into THP using THPConv.exe
        print("  - Compiling into Nintendo Wii THP container...")
        thp_cmd = [
            thpconv_exe,
            "-j", os.path.join("temp_cutscene_build", "f_*.jpg"),
            "-d", os.path.join("temp_cutscene_build", "out.thp"),
            "-s", os.path.join("temp_cutscene_build", "audio.wav"),
            "-r", "59.94"
        ]
        res_thp = subprocess.run(thp_cmd, cwd=tools_dir, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        temp_thp = os.path.join(temp_dir, "out.thp")

        if os.path.exists(temp_thp):
            shutil.copy2(temp_thp, output_thp)
            size_mb = os.path.getsize(output_thp) / (1024 * 1024)
            elapsed = time.time() - start_time
            print(f"  Successfully remastered {base_name}.thp ({size_mb:.1f} MB) in {elapsed:.1f}s\n")
        else:
            print(f"  Failed to build {base_name}.thp\n")

    if os.path.exists(temp_dir):
        shutil.rmtree(temp_dir, ignore_errors=True)

    print("=======================================================")
    print("ALL CUTSCENES REMASTERED SUCCESSFULLY!")
    print("=======================================================")
    return True

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "shield_extracted/assets_all/MovieData"
    dst = sys.argv[2] if len(sys.argv) > 2 else "extracted/DATA/files/MovieData"
    remaster_all_cutscenes(src, dst)

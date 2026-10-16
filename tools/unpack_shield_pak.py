#!/usr/bin/env python3
"""
Unpacker for NVIDIA Shield iQiyi Nintendo PAK (KCAP) archives.
Based on the XXTEA algorithm and key derivation reverse-engineered by SciresM and simontime.
"""
import os
import struct
import sys

DELTA = 0x9E3779B9
FIXED_KEY = [0xA0D0FFB0, 0x81230089, 0x12159842, 0xFF78F3C7]

def djb2_hash(s: str) -> int:
    h = 0x1505
    for ch in s:
        h = ((h * 33) & 0xFFFFFFFF) ^ ord(ch)
    return h & 0xFFFFFFFF

def generate_key(name: str, length: int, offset: int) -> list:
    base = ((length ^ offset) ^ djb2_hash(name)) & 0xFFFFFFFF
    return [(base & k) & 0xFFFFFFFF for k in FIXED_KEY]

def xxtea_decrypt(data_words: list, key: list) -> list:
    n = len(data_words)
    if n <= 0:
        return data_words
    
    rounds = 6 + (52 // n)
    sum_val = (rounds * DELTA) & 0xFFFFFFFF
    y = data_words[0]
    
    while rounds > 0:
        e = (sum_val >> 2) & 3
        for p in range(n - 1, 0, -1):
            z = data_words[p - 1]
            mix = (((z >> 5 ^ (y << 2) & 0xFFFFFFFF) + ((y >> 3 ^ (z << 4) & 0xFFFFFFFF))) & 0xFFFFFFFF) ^ \
                  (((sum_val ^ y) + (key[(p & 3) ^ e] ^ z)) & 0xFFFFFFFF)
            y = (data_words[p] - mix) & 0xFFFFFFFF
            data_words[p] = y
        
        z = data_words[n - 1]
        mix = (((z >> 5 ^ (y << 2) & 0xFFFFFFFF) + ((y >> 3 ^ (z << 4) & 0xFFFFFFFF))) & 0xFFFFFFFF) ^ \
              (((sum_val ^ y) + (key[(0 & 3) ^ e] ^ z)) & 0xFFFFFFFF)
        y = (data_words[0] - mix) & 0xFFFFFFFF
        data_words[0] = y
        
        sum_val = (sum_val - DELTA) & 0xFFFFFFFF
        rounds -= 1
        
    return data_words

def decrypt_asset(name: str, raw_bytes: bytearray) -> bytearray:
    length = len(raw_bytes)
    out = bytearray(raw_bytes)
    for i in range(0, length, 0x2000):
        chunk_len = min(length - i, 0x2000)
        num_words = chunk_len // 4
        if num_words > 0:
            key = generate_key(name, length, i)
            words = list(struct.unpack(f'<{num_words}I', out[i:i + num_words * 4]))
            decrypted = xxtea_decrypt(words, key)
            out[i:i + num_words * 4] = struct.pack(f'<{num_words}I', *decrypted)
    return out

def unpack_pak(pak_path: str, out_dir: str, max_files: int = 0):
    print(f"=== Unpacking {pak_path} -> {out_dir} ===")
    with open(pak_path, 'rb') as f:
        # Header is 0x24 bytes (36 bytes):
        # u32 Magic (0x5041434B / 'PACK' or 'KCAP')
        # u32 Version
        # char Unk1[0xC]
        # u32 Size1
        # u32 Size2
        # char Unk2[0xC]
        header_data = f.read(36)
        magic, version = struct.unpack('<II', header_data[:8])
        size1, size2 = struct.unpack('<II', header_data[20:28])
        print(f"Magic: {hex(magic)}, Version: {version}, Table Size: {size1} bytes")
        
        table_raw = bytearray(f.read(size1))
        decrypted_table = decrypt_asset("header", table_raw)
        
        num_assets = struct.unpack('<I', decrypted_table[:4])[0]
        print(f"Found {num_assets} asset entries in index table!")
        
        ptr = 4
        files_extracted = 0
        base_offset = 36 + size1
        
        while ptr < len(decrypted_table) and (max_files == 0 or files_extracted < max_files):
            name_len = struct.unpack('<I', decrypted_table[ptr:ptr+4])[0]
            ptr += 4
            filename = decrypted_table[ptr:ptr+name_len].decode('latin1', errors='ignore')
            ptr += name_len
            
            size1, size2, offset = struct.unpack('<III', decrypted_table[ptr:ptr+12])
            ptr += 12 + 16 # skip 16 unknown bytes
            
            # Print sample files
            if files_extracted < 20 or (files_extracted % 500 == 0):
                print(f"[{files_extracted+1}/{num_assets}] {filename} ({size1} bytes, offset {offset})")
                
            files_extracted += 1

if __name__ == '__main__':
    pak = sys.argv[1] if len(sys.argv) > 1 else 'shield_extracted/com.nvidia.nintendo.smg/smg_assets.pak'
    out = sys.argv[2] if len(sys.argv) > 2 else 'shield_extracted/unpacked_assets'
    unpack_pak(pak, out, max_files=20)

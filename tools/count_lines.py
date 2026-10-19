import os
import sys

def scan_dir(dir_path):
    oversized = []
    all_files = []
    for root, dirs, files in os.walk(dir_path):
        for f in files:
            if f.endswith(('.c', '.cpp', '.h', '.hpp')):
                p = os.path.join(root, f)
                with open(p, 'r', encoding='utf-8', errors='ignore') as fp:
                    lines = len(fp.readlines())
                all_files.append((p, lines))
                if lines > 300:
                    oversized.append((p, lines))
    return all_files, oversized

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else 'platform'
    all_files, oversized = scan_dir(target)
    for p, lines in sorted(all_files):
        print(f"{lines:4d} lines : {p}")
    print("\n--- OVERSIZED (> 300 LOC) ---")
    for p, lines in sorted(oversized, key=lambda x: -x[1]):
        print(f"{lines:4d} lines : {p}")
    print(f"Total files: {len(all_files)}, Oversized: {len(oversized)}")

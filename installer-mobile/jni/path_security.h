#pragma once

#include <cstdio>
#include <string>

namespace wii {

// Validates whether a file or directory component name is strictly safe:
// - Not empty, not "." and not ".."
// - Does not contain '/', '\', ':', or control characters (< 0x20, 0x7F)
// - Does not end with space or dot (prevents Windows alias & traversal bypasses)
// - Length <= 255 bytes
bool isSafeName(const char* name);

// Checks if the given path is a symlink, reparse point, or junction.
bool isReparseOrSymlink(const std::string& path);

// Checks if path exists and is a genuine directory (and NOT a symlink).
bool isRealDirectory(const std::string& path);

// Ensures that all directory components under rootDir/relDir are safely created
// or exist as real directories, rejecting any symlink or escaping component.
bool ensureSafeDirectories(const std::string& rootDir, const std::string& relDir);

// Safely opens a file for writing beneath rootDir:
// - Verifies that all parent directory components are safe real directories
// - Refuses to open or truncate if the target path is a pre-existing symlink
// - Uses O_NOFOLLOW on POSIX to prevent symlink races
FILE* openSafeFileForWrite(const std::string& rootDir, const std::string& relPath,
                           std::string* outFullPath = nullptr);

// Deletes a partially written file on error/abort
void removePartialFile(const std::string& fullPath);

// Normalizes path separators (replaces '\' with '/') and strips trailing slashes
std::string normalizePath(const std::string& path);

} // namespace wii

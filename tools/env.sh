#!/bin/bash
# Finds the tools the build scripts use; sourced by build_android.sh,
# tools/package_apk.sh and the device scripts.  Everything can be given
# through the environment:
#   ANDROID_NDK_HOME  Android NDK (r29)
#   ANDROID_HOME      Android SDK (build-tools, platforms/android-3x, platform-tools)
#   JAVA_HOME         a JDK (17 or newer) for keytool and apksigner
#   CMAKE, NINJA, PYTHON, ADB   the programs themselves
# Otherwise they are looked up on PATH, in the usual install places, and in
# ../tools next to the repository (where the portable toolchains of the
# original development setup live).

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LOCAL_TOOLS=$(cd "$REPO/.." && pwd)/tools

# Git Bash / MSYS on Windows: programs end in .exe and some take Windows paths.
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*) IS_WINDOWS=1; EXE=.exe ;;
  *) IS_WINDOWS=; EXE= ;;
esac

# A path as native programs want it (C:/... on Windows).
native_path() {
  if [ -n "$IS_WINDOWS" ]; then
    if command -v cygpath > /dev/null 2>&1; then
      cygpath -m "$1"
    else
      echo "$1" | sed -E 's|^/([a-zA-Z])/|\1:/|'
    fi
  else
    echo "$1"
  fi
}

first_dir() {
  for d in "$@"; do
    if [ -n "$d" ] && [ -d "$d" ]; then echo "$d"; return 0; fi
  done
  return 1
}

find_ndk() {
  first_dir "$ANDROID_NDK_HOME" "$ANDROID_NDK_ROOT" "$ANDROID_NDK" "$LOCAL_TOOLS/android-ndk-r29" \
    $(ls -d "$(find_sdk 2> /dev/null)"/ndk/29.* 2> /dev/null | sort -V | tail -1) \
    $(ls -d "$(find_sdk 2> /dev/null)"/ndk/* 2> /dev/null | sort -V | tail -1)
}

find_sdk() {
  first_dir "$ANDROID_HOME" "$ANDROID_SDK_ROOT" "$HOME/AppData/Local/Android/Sdk" "$LOCALAPPDATA/Android/Sdk" "$HOME/Android/Sdk" \
    "$HOME/Library/Android/sdk"
}

# The NDK's own LLVM tools (llvm-strip, llvm-addr2line).
ndk_llvm_bin() {
  ls -d "$(find_ndk)"/toolchains/llvm/prebuilt/*/bin 2> /dev/null | head -1
}

find_program() {  # name, then fallbacks
  local name=$1
  shift
  if command -v "$name" > /dev/null; then command -v "$name"; return 0; fi
  for p in "$@"; do
    if [ -x "$p" ]; then echo "$p"; return 0; fi
  done
  return 1
}

find_cmake() { [ -n "$CMAKE" ] && echo "$CMAKE" || find_program cmake $(ls "$LOCAL_TOOLS"/cmake-*/bin/cmake$EXE 2> /dev/null | tail -1) $(ls -d "$(find_sdk 2> /dev/null)"/cmake/*/bin/cmake$EXE 2> /dev/null | sort -V | tail -1); }
find_ninja() { [ -n "$NINJA" ] && echo "$NINJA" || find_program ninja "$LOCAL_TOOLS/ninja/ninja$EXE" $(ls -d "$(find_sdk 2> /dev/null)"/cmake/*/bin/ninja$EXE 2> /dev/null | sort -V | tail -1); }

find_adb() {
  if [ -n "$ADB" ]; then echo "$ADB"; return 0; fi
  find_program adb "$(find_sdk 2> /dev/null)/platform-tools/adb$EXE"
}

# A Python 3 that runs (on Windows "python" may be the Microsoft Store stub).
find_python() {
  if [ -n "$PYTHON" ]; then echo "$PYTHON"; return 0; fi
  for p in python3 python "$LOCAL_TOOLS/python/python$EXE" $(ls "$(find_ndk 2> /dev/null)"/toolchains/llvm/prebuilt/*/python3/python$EXE 2> /dev/null | tail -1); do
    if command -v "$p" > /dev/null 2>&1 || [ -x "$p" ]; then
      if "$p" -c "import sys; sys.exit(sys.version_info < (3, 8))" 2> /dev/null; then
        echo "$p"
        return 0
      fi
    fi
  done
  if command -v py > /dev/null && py -3 -c "" 2> /dev/null; then
    echo "py -3"
    return 0
  fi
  return 1
}

# Runs Python 3 (see find_python) with the given arguments.
py3() {
  local p
  p=$(find_python) || { echo "Python 3 not found: put it on PATH or set PYTHON" >&2; return 1; }
  if [ "$p" = "py -3" ]; then py -3 "$@"; else "$p" "$@"; fi
}

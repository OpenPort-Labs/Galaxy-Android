cmake_minimum_required(VERSION 3.24)
project(GalaxyQuest C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DECOMP ${CMAKE_SOURCE_DIR}/decomp)

if(ANDROID)
  set(PORT_PLATFORM android)
else()
  set(PORT_PLATFORM desktop)
endif()

# ---------------------------------------------------------------------------
# Decompiled game sources
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/Game/*.cpp ${DECOMP}/src/Game/*.c)
file(GLOB_RECURSE JSYSTEM_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/JSystem/*.cpp)
file(GLOB_RECURSE NW4R_SRCS CONFIGURE_DEPENDS ${DECOMP}/src/nw4r/*.cpp)

# SDK libraries that are pure software and are compiled from the decomp as-is.
set(SDK_MODULES gx gd mem arc tpl aralt)
set(SDK_SRCS)
foreach(m ${SDK_MODULES})
  file(GLOB_RECURSE _s CONFIGURE_DEPENDS ${DECOMP}/src/RVL_SDK/${m}/*.c)
  list(APPEND SDK_SRCS ${_s})
endforeach()

list(FILTER SDK_SRCS EXCLUDE REGEX "/gx/GXFifo\\.c$|/gx/GXPerf\\.c$")
# OS pieces that are pure software on top of the thread API.
list(APPEND SDK_SRCS ${DECOMP}/src/RVL_SDK/os/OSMutex.c ${DECOMP}/src/RVL_SDK/os/OSMessage.c)
# THP movie audio is plain C (the video decoder is replaced by platform/src/sdk/thp_video.cpp).
list(APPEND SDK_SRCS ${DECOMP}/src/RVL_SDK/thp/THPAudio.c)

set(DECOMP_INCLUDES
  ${CMAKE_SOURCE_DIR}/platform/include
  ${DECOMP}/include
  ${DECOMP}/libs/JSystem/include
  ${DECOMP}/libs/RVL_SDK/include
  ${DECOMP}/libs/nw4r/include
  ${DECOMP}/libs/RVLFaceLib/include
)

option(TARGET_MOBILE "Build flat-screen Android mobile target without OpenXR" OFF)

set(PORT_DEFS TARGET_PC=1 NDEBUG=1 VERSION=0)
if(TARGET_MOBILE)
  list(APPEND PORT_DEFS TARGET_MOBILE=1)
endif()

# Paths in the binaries (__FILE__, debug info) relative to the repository
# and the NDK, not the machine that built them.
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}/= -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
if(ANDROID)
  add_compile_options(-ffile-prefix-map=${ANDROID_NDK}=ndk)
endif()

set(PORT_COMMON_FLAGS
  -fshort-wchar
  -fsigned-char
  -fno-strict-aliasing
  -fwrapv
  -ffp-contract=on
  -Wno-register
  -Wno-multichar
  -Wno-invalid-offsetof
  -Wno-c++11-narrowing
  -Wno-writable-strings
  -Wno-null-conversion
  -Wno-deprecated-declarations
  -Wno-int-to-pointer-cast
  -Wno-pointer-to-int-cast
  -Wno-invalid-source-encoding
  -Wno-unused-value
  -Wno-return-type
  -Wno-switch
  -Wno-parentheses
  -Wno-dangling-else
  -Wno-tautological-compare
  -Wno-tautological-undefined-compare
  -Wno-undefined-bool-conversion
  -Wno-constant-conversion
  -Wno-shift-negative-value
  -Wno-implicit-function-declaration
  -Wno-int-conversion
  -Wno-incompatible-pointer-types
  -Wno-incompatible-function-pointer-types
  -Wno-pointer-sign
  -Wno-unknown-pragmas
  -Wno-ignored-attributes
  -Wno-microsoft-template
  -fms-extensions
  -Wno-void-pointer-to-int-cast
  -Wno-microsoft-cast
  -Wno-microsoft-anon-tag
  -Wno-microsoft-include
  -Wno-language-extension-token
  -ferror-limit=0
  -include ${CMAKE_SOURCE_DIR}/platform/include/port/compat.h
)
set(PORT_CXX_FLAGS -fno-exceptions -fno-rtti)
set(PORT_C_FLAGS)

add_library(decomp OBJECT ${GAME_SRCS} ${JSYSTEM_SRCS} ${NW4R_SRCS} ${SDK_SRCS})
target_include_directories(decomp PRIVATE ${DECOMP_INCLUDES})
target_compile_definitions(decomp PRIVATE ${PORT_DEFS})
target_compile_options(decomp PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)


# ---------------------------------------------------------------------------
# Port platform layer + final game library
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PLATFORM_SRCS CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/platform/src/*.c
  ${CMAKE_SOURCE_DIR}/platform/src/*.cpp)

if(TARGET_MOBILE)
  list(FILTER PLATFORM_SRCS EXCLUDE REGEX "/xr/(xr_app|vr_game|vr_settings|vr_setup|xr_sim|ui_canvas)\\.cpp$")
  file(GLOB_RECURSE MOBILE_SRCS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/platform/mobile/*.cpp
    ${CMAKE_SOURCE_DIR}/platform/mobile/*.c)
  list(APPEND PLATFORM_SRCS ${MOBILE_SRCS})
endif()

add_library(game SHARED $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS})
target_include_directories(game PRIVATE ${DECOMP_INCLUDES} ${CMAKE_SOURCE_DIR}/platform/src)
target_compile_definitions(game PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1)
if(TARGET_MOBILE)
  target_compile_definitions(game PRIVATE TARGET_MOBILE=1)
endif()
target_compile_options(game PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)
target_link_options(game PRIVATE -Wl,--no-undefined -Wl,--error-limit=0)
if(ANDROID)
  # -Bsymbolic: game code must bind to the port's operator new/delete, not the
  # system C++ runtime's.
  target_link_options(game PRIVATE -Wl,-Bsymbolic -Wl,--gc-sections)
  target_link_libraries(game PRIVATE log android)
endif()

if(ANDROID)
  set(GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

  if(TARGET_MOBILE)
    target_include_directories(game PRIVATE ${GLUE_DIR})
    target_link_libraries(game PRIVATE EGL GLESv3 aaudio)
  else()
    # The Khronos OpenXR loader (tools/fetch_openxr.sh puts it there).
    set(OPENXR_DIR ${CMAKE_SOURCE_DIR}/third_party/openxr/prefab/modules)

    add_library(openxr_loader SHARED IMPORTED)
    set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION ${OPENXR_DIR}/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so)

    target_include_directories(game PRIVATE ${OPENXR_DIR}/headers/include ${GLUE_DIR})
    target_link_libraries(game PRIVATE openxr_loader EGL GLESv3 aaudio)
  endif()

  # Launcher: reserves the 32-bit game window and loads libgame.so into it.
  add_library(main SHARED platform/android/launcher.c ${GLUE_DIR}/android_native_app_glue.c)
  target_include_directories(main PRIVATE ${GLUE_DIR})
  target_link_libraries(main PRIVATE android log dl)
  target_link_options(main PRIVATE -u ANativeActivity_onCreate)

  # Preloaded into the app process by wrap.sh: reserves the game window before
  # ART lays out its heap.
  add_library(reserve SHARED platform/android/reserve.c)
  target_link_libraries(reserve PRIVATE log)

  # Bring-up tool: boots libgame.so from an adb shell without VR.
  add_executable(petari_headless platform/android/headless.c)
  target_link_libraries(petari_headless PRIVATE log dl m)
  target_include_directories(petari_headless PRIVATE platform/include)
endif()

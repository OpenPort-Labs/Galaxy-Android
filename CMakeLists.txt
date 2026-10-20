cmake_minimum_required(VERSION 3.24)
project(GalaxyAndroid C CXX)

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

option(TARGET_MOBILE "Build flat-screen Android mobile target without OpenXR" ON)

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
  -O3
  -fno-math-errno
  -fno-trapping-math
  -fomit-frame-pointer
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
  ${CMAKE_SOURCE_DIR}/platform/src/*.cpp
  ${CMAKE_SOURCE_DIR}/platform/mobile/*.c
  ${CMAKE_SOURCE_DIR}/platform/mobile/*.cpp)

add_library(game SHARED $<TARGET_OBJECTS:decomp> ${PLATFORM_SRCS})
target_include_directories(game PRIVATE ${DECOMP_INCLUDES} ${CMAKE_SOURCE_DIR}/platform ${CMAKE_SOURCE_DIR}/platform/src)
target_compile_definitions(game PRIVATE ${PORT_DEFS} PORT_PLATFORM_CODE=1)
if(TARGET_MOBILE)
  target_compile_definitions(game PRIVATE TARGET_MOBILE=1)
endif()
target_compile_options(game PRIVATE ${PORT_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:${PORT_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:${PORT_C_FLAGS}>)
target_link_options(game PRIVATE -Wl,--no-undefined -Wl,--error-limit=0)
if(ANDROID)
  # -Bsymbolic: game code must bind to the port's operator new/delete, not the
  # system C++ runtime's.
  target_link_options(game PRIVATE -Wl,-Bsymbolic -Wl,--gc-sections -Wl,-O3)
  target_link_libraries(game PRIVATE log android)
endif()

if(ANDROID)
  if(NOT DEFINED ANDROID_NDK)
    if(DEFINED CMAKE_ANDROID_NDK)
      set(ANDROID_NDK ${CMAKE_ANDROID_NDK})
    elseif(DEFINED ENV{ANDROID_NDK_HOME})
      set(ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    elseif(DEFINED ENV{ANDROID_NDK_ROOT})
      set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    endif()
  endif()
  set(GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)

  set(SHADERC_STATIC "${ANDROID_NDK}/sources/third_party/shaderc/libs/c++_static/${ANDROID_ABI}/libshaderc.a")
  if(NOT EXISTS "${SHADERC_STATIC}")
    find_program(NDK_BUILD_CMD NAMES ndk-build ndk-build.cmd PATHS "${ANDROID_NDK}" NO_DEFAULT_PATH)
    if(NDK_BUILD_CMD)
      message(STATUS "libshaderc.a not found in NDK; auto-building via ndk-build...")
      execute_process(
        COMMAND "${NDK_BUILD_CMD}" -C "${ANDROID_NDK}/sources/third_party/shaderc"
                NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=Android.mk APP_ABI=${ANDROID_ABI} APP_STL=c++_static APP_PLATFORM=android-30 libshaderc_combined
        RESULT_VARIABLE NDK_BUILD_RES
      )
    endif()
  endif()
  target_include_directories(game PRIVATE ${GLUE_DIR} ${ANDROID_NDK}/sources/third_party/shaderc/include)
  target_link_libraries(game PRIVATE aaudio vulkan "${SHADERC_STATIC}")

  # Launcher: reserves the 32-bit game window and loads libgame.so into it.
  add_library(main SHARED platform/android/launcher.c ${GLUE_DIR}/android_native_app_glue.c)
  target_include_directories(main PRIVATE ${GLUE_DIR})
  target_link_libraries(main PRIVATE android log dl)
  target_link_options(main PRIVATE -u ANativeActivity_onCreate)

  # Preloaded into the app process by wrap.sh: reserves the game window before
  # ART lays out its heap.
  add_library(reserve SHARED platform/android/reserve.c)
  target_link_libraries(reserve PRIVATE log dl)

  # Bring-up tool: boots libgame.so from an adb shell without VR.
  add_executable(petari_headless platform/android/headless.c)
  target_link_libraries(petari_headless PRIVATE log dl m)
  target_include_directories(petari_headless PRIVATE platform/include)

  # In-app disc converter and Python bridge library (libinstaller.so)
  if(TARGET_MOBILE)
    add_library(python310 SHARED IMPORTED)
    set_target_properties(python310 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/installer-mobile/vendor/python/lib/arm64-v8a/libpython3.10.so
    )
    add_library(installer SHARED
        installer-mobile/jni/py_bridge.cpp
        installer-mobile/jni/disc_bridge.cpp
        installer-mobile/jni/bridge_common.cpp
        installer-mobile/jni/wii_extractor.cpp
        installer-mobile/jni/wii_disc_reader.cpp
        installer-mobile/jni/wii_partition.cpp
        installer-mobile/jni/aes.c
        installer-mobile/jni/aes_modes.c
        installer-mobile/jni/yaz0.c
    )
    target_include_directories(installer PRIVATE
        ${CMAKE_SOURCE_DIR}/installer-mobile/vendor/python/include
        ${CMAKE_SOURCE_DIR}/installer-mobile/jni
    )
    target_link_libraries(installer PRIVATE
        python310
        log
    )
  endif()

  add_executable(vk_inventory_test tests/test_vk_inventory.cpp platform/src/gx/gx_shader_uid.cpp platform/src/gx/vk_shader_manifest.cpp platform/src/gx/vk_shader_catalog.cpp)
  target_include_directories(vk_inventory_test PRIVATE platform platform/src platform/include decomp/include)
  target_link_libraries(vk_inventory_test PRIVATE log)
endif()

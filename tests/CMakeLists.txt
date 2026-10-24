# Host-side tests for untrusted-input parsers and platform helpers.
# dvd_host_compat.h supplies the POSIX calls MinGW lacks; bionic has them.

add_executable(dvd_fst_test
  test_dvd_fst.cpp
  test_dvd_io.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/dvd/dvd_fst.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/dvd/dvd_read.cpp
)
target_include_directories(dvd_fst_test PRIVATE
  ${CMAKE_SOURCE_DIR}/platform/include
  ${CMAKE_SOURCE_DIR}/platform/src
  ${DECOMP}/include
  ${DECOMP}/libs/RVL_SDK/include
)
target_compile_options(dvd_fst_test PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/dvd_host_compat.h)
add_test(NAME dvd_fst COMMAND dvd_fst_test)

add_executable(extraction_security_test
  test_extraction_security.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/path_security.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/wii_extractor.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/wii_partition.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/wii_disc_reader.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/aes.c
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/aes_modes.c
)
target_include_directories(extraction_security_test PRIVATE ${CMAKE_SOURCE_DIR}/installer-mobile/jni)
add_test(NAME extraction_security COMMAND extraction_security_test)

add_executable(wii_extraction_limits_test
  test_wii_extraction_limits.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/path_security.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/wii_extractor.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/wii_partition.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/wii_disc_reader.cpp
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/aes.c
  ${CMAKE_SOURCE_DIR}/installer-mobile/jni/aes_modes.c
)
target_include_directories(wii_extraction_limits_test PRIVATE ${CMAKE_SOURCE_DIR}/installer-mobile/jni)
add_test(NAME wii_extraction_limits COMMAND wii_extraction_limits_test)

add_executable(guest_memory_test
  test_guest_memory.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/os/guest_layout.c
  ${CMAKE_SOURCE_DIR}/platform/src/os/guest_maps.c
)
target_include_directories(guest_memory_test PRIVATE
  ${CMAKE_SOURCE_DIR}/platform/include
  ${CMAKE_SOURCE_DIR}/platform/src
  ${DECOMP}/include
  ${DECOMP}/libs/RVL_SDK/include
)
add_test(NAME guest_memory COMMAND guest_memory_test)

add_executable(timing_pacing_test test_timing_pacing.cpp)
target_include_directories(timing_pacing_test PRIVATE
  ${CMAKE_SOURCE_DIR}/platform/include
  ${CMAKE_SOURCE_DIR}/platform/src
  ${CMAKE_SOURCE_DIR}/platform/mobile/display
)
add_test(NAME timing_pacing COMMAND timing_pacing_test)

add_executable(thp_bounds_test
  test_thp_bounds.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/sdk/thp_idct.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/sdk/thp_scan.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/sdk/thp_video.cpp
)
target_include_directories(thp_bounds_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/src/sdk)
target_compile_definitions(thp_bounds_test PRIVATE THP_STANDALONE=1)
add_test(NAME thp_bounds COMMAND thp_bounds_test)

add_executable(heap_alignment_test
  test_heap_alignment.cpp
  ${DECOMP}/src/JSystem/JKernel/JKRHeap.cpp
  ${DECOMP}/src/JSystem/JKernel/JKRExpHeap.cpp
  ${DECOMP}/src/JSystem/JKernel/JKRDisposer.cpp
  ${DECOMP}/src/JSystem/JSupport/JSUList.cpp
)
target_include_directories(heap_alignment_test PRIVATE
  ${CMAKE_SOURCE_DIR}/platform/include ${DECOMP}/include
  ${DECOMP}/libs/JSystem/include ${DECOMP}/libs/RVL_SDK/include
)
target_compile_definitions(heap_alignment_test PRIVATE TARGET_PC=1 _Static_assert=static_assert)
target_compile_options(heap_alignment_test PRIVATE
  -O1 -fpermissive -fms-extensions -fno-strict-aliasing
  -Wno-narrowing -Wno-deprecated-declarations -Wno-macro-redefined
  -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
)
add_test(NAME heap_alignment COMMAND heap_alignment_test)

add_executable(save_sync_test
  test_save_sync.cpp
  ${CMAKE_SOURCE_DIR}/platform/mobile/ui/SaveStorage.cpp
  ${CMAKE_SOURCE_DIR}/platform/mobile/ui/SaveSyncManager.cpp
  ${CMAKE_SOURCE_DIR}/platform/mobile/ui/DolphinSaveConverter.cpp
)
target_include_directories(save_sync_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/mobile/ui)
add_test(NAME save_sync COMMAND save_sync_test)

add_executable(dolphin_converter_test
  test_dolphin_converter.cpp
  ${CMAKE_SOURCE_DIR}/platform/mobile/ui/DolphinSaveConverter.cpp
)
target_include_directories(dolphin_converter_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/mobile/ui)
add_test(NAME dolphin_converter COMMAND dolphin_converter_test)

# wstring.cpp implements the game's 16-bit wchar_t ABI.
add_executable(wstring_format_test
  test_wstring_format.cpp
  ${CMAKE_SOURCE_DIR}/platform/src/port/wstring.cpp
)
target_include_directories(wstring_format_test PRIVATE ${CMAKE_SOURCE_DIR}/platform/include)
if(NOT WIN32)
  target_compile_options(wstring_format_test PRIVATE -fshort-wchar)
endif()
add_test(NAME wstring_format COMMAND wstring_format_test)

# Run under CTest without depending on which libstdc++ happens to be first on
# PATH; an older MSYS2 runtime can lack symbols the compiler emits.
if(WIN32)
  get_property(_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _test_targets)
    target_link_options(${_target} PRIVATE -static)
  endforeach()
endif()

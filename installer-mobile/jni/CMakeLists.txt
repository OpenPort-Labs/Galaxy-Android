cmake_minimum_required(VERSION 3.22)
project(galaxy_installer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/python/include
)

add_library(python310 SHARED IMPORTED)
set_target_properties(python310 PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/python/lib/arm64-v8a/libpython3.10.so
)

add_library(installer SHARED
    py_bridge.cpp
    wii_extractor.cpp
    aes.c
    yaz0.c
)

target_link_libraries(installer
    python310
    log
)

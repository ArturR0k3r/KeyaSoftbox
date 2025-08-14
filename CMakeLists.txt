# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Set the board target
set(BOARD esp32s3_devkitm)

# Find Zephyr package and initialize
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Project definition
project(keyasoftbox VERSION 1.0.0)

# Source files
target_sources(app PRIVATE 
    src/main.c
    src/led_effects.c
    src/mesh_handler.c
    src/gatt_service.c
)

# Include directories
target_include_directories(app PRIVATE 
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler definitions
target_compile_definitions(app PRIVATE
    -DKEYASOFTBOX_VERSION_MAJOR=1
    -DKEYASOFTBOX_VERSION_MINOR=0
    -DKEYASOFTBOX_VERSION_PATCH=0
)

# Compiler options for optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(app PRIVATE
        -Os  # Optimize for size
        -ffunction-sections
        -fdata-sections
    )
    
    target_link_options(app PRIVATE
        -Wl,--gc-sections
    )
endif()

# Add custom build timestamp
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)
target_compile_definitions(app PRIVATE
    -DBUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
)

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/version.h
    @ONLY
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
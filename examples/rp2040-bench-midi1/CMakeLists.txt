cmake_minimum_required(VERSION 3.14)  # FetchContent_MakeAvailable

# rp2040-bench-midi1, MIDI 1.0 bytestream to UMP conformance bench.
# Targets any Raspberry Pi Pico (RP2040); results print on the USB serial
# console (stdio USB CDC), no USB MIDI identity involved. Lives at
# midi2cpp/examples/rp2040-bench-midi1; consumes the vendored midi2 core
# directly from ../../src. Builds standalone with the Pico SDK toolchain.
#
# A battery of malformed MIDI 1.0 byte vectors is converted on-target by the
# midi2 core and self-checked against expected UMP words derived from
# M2-104-UM. Pass -DWITH_AM_MIDI2=ON to fetch AM_MIDI2.0Lib at configure
# time and print its bytestreamToUMP conversion side by side with midi2's
# for every vector.

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(rp2040-bench-midi1 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

option(WITH_AM_MIDI2 "Also run AM_MIDI2.0Lib bytestreamToUMP side by side" OFF)
set(AM_MIDI2_GIT_TAG "main" CACHE STRING "AM_MIDI2.0Lib ref to compare against")

# midi2 C99 core, vendored amalgam from the parent library tree.
set(MIDI2CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(rp2040-bench-midi1
    src/main.cpp
    ${MIDI2CPP_ROOT}/src/midi2.c
)

target_include_directories(rp2040-bench-midi1 PRIVATE
    ${MIDI2CPP_ROOT}/src
)

if(WITH_AM_MIDI2)
    include(FetchContent)
    FetchContent_Declare(
        am_midi2
        GIT_REPOSITORY https://github.com/midi2-dev/AM_MIDI2.0Lib.git
        GIT_TAG        ${AM_MIDI2_GIT_TAG}
        GIT_SHALLOW    FALSE
    )
    # Populate only: bytestreamToUMP is header-only and the library's own
    # CMake targets are host-side test programs that do not cross-compile.
    FetchContent_GetProperties(am_midi2)
    if(NOT am_midi2_POPULATED)
        FetchContent_Populate(am_midi2)
    endif()
    target_include_directories(rp2040-bench-midi1 PRIVATE
        ${am_midi2_SOURCE_DIR}/include
    )
    target_compile_definitions(rp2040-bench-midi1 PRIVATE WITH_AM_MIDI2)
    # AM_MIDI2.0Lib's utils.h carries inline float-sprintf helpers; newlib's
    # float printf family references the syscall stubs. nosys provides them.
    target_link_libraries(rp2040-bench-midi1 nosys)
endif()

target_link_libraries(rp2040-bench-midi1 pico_stdlib)

pico_enable_stdio_usb(rp2040-bench-midi1 1)
pico_enable_stdio_uart(rp2040-bench-midi1 0)

pico_add_extra_outputs(rp2040-bench-midi1)

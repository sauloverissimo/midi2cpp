cmake_minimum_required(VERSION 3.14)

# Common helper for tests (TX capture hook, assertions)
add_library(midi2_cpp_test_common INTERFACE)
target_include_directories(midi2_cpp_test_common INTERFACE .)

# Test builder: each test_*.cpp compiles to an independent executable.
function(add_midi2_cpp_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE midi2_cpp midi2_cpp_test_common)
    target_compile_options(${name} PRIVATE -Wall -Wextra -g -O0)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Individual test files registered as they are created.
add_midi2_cpp_test(test_midi2_device)
add_midi2_cpp_test(test_midi2_scaling)
add_midi2_cpp_test(test_midi2_flex)
add_midi2_cpp_test(test_midi2_conversion)
add_midi2_cpp_test(test_midi2_ci)
add_midi2_cpp_test(test_midi2_host)

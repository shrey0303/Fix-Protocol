cmake_minimum_required(VERSION 3.16)
project(fix-parser-cpp
        VERSION 1.0.0
        LANGUAGES CXX
        DESCRIPTION "Zero-allocation FIX 4.2/4.4 protocol parser")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ──────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive-)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -DNDEBUG")
endif()

# ── Core library ────────────────────────────────────────────────
add_library(fix_parser STATIC
    src/FixParser.cpp
    src/FixValidator.cpp
    src/FixEncoder.cpp
)
target_include_directories(fix_parser PUBLIC include)

# ── Dependencies via FetchContent ───────────────────────────────
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
)

# Prevent GoogleTest from overriding compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# Disable benchmark tests to speed up build
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
# Disable -Werror in benchmark (GCC 16 triggers -Wswitch in sysinfo.cc)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest googlebenchmark)

# ── Tests ───────────────────────────────────────────────────────
enable_testing()

add_executable(fix_tests
    test/parser_test.cpp
    test/checksum_test.cpp
    test/sequence_test.cpp
)
target_link_libraries(fix_tests PRIVATE fix_parser GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(fix_tests)

# ── Benchmarks ──────────────────────────────────────────────────
add_executable(fix_bench
    bench/parser_bench.cpp
)
target_link_libraries(fix_bench PRIVATE fix_parser benchmark::benchmark_main)

# ── Fuzzer (only with Clang and LibFuzzer support) ──────────────
option(ENABLE_FUZZER "Build fuzzer target (requires Clang with LibFuzzer)" OFF)
if(ENABLE_FUZZER)
    add_executable(fix_fuzz fuzz/fuzz_parser.cpp)
    target_link_libraries(fix_fuzz PRIVATE fix_parser)
    target_compile_options(fix_fuzz PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fix_fuzz PRIVATE -fsanitize=fuzzer,address)
endif()



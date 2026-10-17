cmake_minimum_required(VERSION 3.24)

project(spoti_c VERSION 0.1.0 LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "spoti-c requires the native Windows toolchain and SDK.")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer in Debug builds" ON)

add_library(spotic_build_options INTERFACE)
target_compile_features(spotic_build_options INTERFACE c_std_17)
if(ENABLE_ASAN)
    target_compile_definitions(spotic_build_options INTERFACE
        $<$<CONFIG:Debug>:SPOTIC_ASAN=1>)
endif()

if(MSVC)
    target_compile_options(spotic_build_options INTERFACE /W4 /WX)
    if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        # Enable MSVC's C11/C17 atomics; clang-cl supports them directly.
        target_compile_options(spotic_build_options INTERFACE /experimental:c11atomics)
    endif()
    if(ENABLE_ASAN)
        # CMake enables /RTC1 by default; MSVC ASan cannot be combined with it.
        string(REGEX REPLACE "/RTC[1su]+" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        target_compile_options(spotic_build_options INTERFACE
            $<$<CONFIG:Debug>:/fsanitize=address>)
        target_link_options(spotic_build_options INTERFACE
            $<$<CONFIG:Debug>:/INCREMENTAL:NO>)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(spotic_build_options INTERFACE -Wall -Wextra -Werror)
    if(ENABLE_ASAN)
        target_compile_options(spotic_build_options INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
        target_link_options(spotic_build_options INTERFACE
            $<$<CONFIG:Debug>:-fsanitize=address>)
    endif()
else()
    message(FATAL_ERROR "Use MSVC or a Clang/GNU-compatible C compiler.")
endif()

add_library(spotic_windows INTERFACE)
target_link_libraries(spotic_windows INTERFACE
    d3d11 dxgi d3dcompiler dwrite winhttp bcrypt ws2_32)

include(CTest)
add_library(spotic_core STATIC
    src/core/arena.c src/core/spsc_ring.c src/core/clock.c
    src/core/event_queue.c src/core/worker.c)
target_include_directories(spotic_core PUBLIC src)
target_link_libraries(spotic_core PUBLIC spotic_build_options)

if(BUILD_TESTING)
    add_executable(test_smoke tests/test_smoke.c)
    target_link_libraries(test_smoke PRIVATE spotic_build_options spotic_windows)
    add_test(NAME smoke COMMAND test_smoke)
    set_tests_properties(smoke PROPERTIES TIMEOUT 10)

    add_executable(test_arena tests/test_arena.c)
    target_link_libraries(test_arena PRIVATE spotic_core)
    add_test(NAME core_arena COMMAND test_arena)
    set_tests_properties(core_arena PROPERTIES TIMEOUT 10)

    add_executable(test_spsc_ring tests/test_spsc_ring.c)
    target_link_libraries(test_spsc_ring PRIVATE spotic_core)
    add_test(NAME core_spsc COMMAND test_spsc_ring)
    set_tests_properties(core_spsc PROPERTIES TIMEOUT 60)

    add_executable(test_event_queue tests/test_event_queue.c tests/test_worker.c)
    target_link_libraries(test_event_queue PRIVATE spotic_core)
    add_test(NAME core_event_queue COMMAND test_event_queue)
    set_tests_properties(core_event_queue PROPERTIES TIMEOUT 60)
    if(ENABLE_ASAN AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
        foreach(mode IN ITEMS reset rewind overrun)
            add_test(NAME core_arena_asan_${mode}
                COMMAND ${CMAKE_COMMAND}
                    -DTEST_PROGRAM=$<TARGET_FILE:test_arena> -DMODE=${mode}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_arena_asan.cmake)
            set_tests_properties(core_arena_asan_${mode} PROPERTIES TIMEOUT 15)
            if(CMAKE_CONFIGURATION_TYPES)
                set_tests_properties(core_arena_asan_${mode} PROPERTIES CONFIGURATIONS Debug)
            endif()
        endforeach()
    endif()
endif()

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
option(ENABLE_LIVE_OAUTH_TEST "Enable browser/account-dependent OAuth and AP compatibility test" OFF)

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
    src/core/event_queue.c src/core/worker.c src/core/metrics.c src/core/log.c)
target_include_directories(spotic_core PUBLIC src)
target_link_libraries(spotic_core PUBLIC spotic_build_options)
include(cmake/Proto.cmake)
add_library(spotic_shannon STATIC vendor/shannon.c src/protocol/shannon_adapter.c)
target_include_directories(spotic_shannon PUBLIC src vendor)
target_link_libraries(spotic_shannon PUBLIC spotic_build_options)
add_library(spotic_crypto STATIC src/protocol/crypto_cng.c src/protocol/crypto_constants.c
    src/protocol/crypto_dh.c src/protocol/crypto_hash.c src/protocol/crypto_rsa.c src/protocol/crypto_aes.c)
target_include_directories(spotic_crypto PUBLIC src)
target_link_libraries(spotic_crypto PUBLIC spotic_build_options bcrypt)

add_library(spotic_yyjson STATIC vendor/yyjson.c)
target_include_directories(spotic_yyjson PUBLIC vendor)
target_link_libraries(spotic_yyjson PUBLIC spotic_build_options)
add_library(spotic_http STATIC src/api/http_client.c src/api/http_pool.c
    src/api/http_request.c src/api/http_response.c src/api/http_retry.c)
target_include_directories(spotic_http PUBLIC src)
target_link_libraries(spotic_http PUBLIC spotic_core winhttp)
add_library(spotic_json_base STATIC src/api/json_base.c)
target_include_directories(spotic_json_base PUBLIC src)
target_link_libraries(spotic_json_base PUBLIC spotic_core spotic_yyjson)

add_library(spotic_credentials STATIC src/protocol/credential_store.c)
target_include_directories(spotic_credentials PUBLIC src)
target_link_libraries(spotic_credentials PUBLIC spotic_crypto crypt32 advapi32)
add_library(spotic_oauth STATIC src/api/oauth.c src/api/oauth_loopback.c src/api/oauth_token.c)
target_include_directories(spotic_oauth PUBLIC src)
target_link_libraries(spotic_oauth PUBLIC spotic_http spotic_json_base spotic_credentials shell32 ws2_32)

add_library(spotic_ap STATIC src/protocol/ap_packet.c src/protocol/ap_handshake.c
    src/protocol/ap_resolve.c src/protocol/ap_session.c src/protocol/ap_socket.c
    src/protocol/ap_io.c src/protocol/ap_route.c)
target_sources(spotic_ap PRIVATE src/protocol/ap_auth.c)
target_include_directories(spotic_ap PUBLIC src)
target_link_libraries(spotic_ap PUBLIC spotic_proto spotic_crypto spotic_shannon spotic_http spotic_json_base spotic_credentials ws2_32)

if(BUILD_TESTING)
    add_executable(test_json_base tests/test_json_base.c)
    target_include_directories(test_json_base PRIVATE tests)
    target_link_libraries(test_json_base PRIVATE spotic_json_base)
    add_test(NAME api_json_base COMMAND test_json_base)
    set_tests_properties(api_json_base PROPERTIES TIMEOUT 15)

    add_executable(test_http_client tests/test_http_client.c tests/test_http_lifetime.c tests/test_http_policy.c
        tests/support/http_server.c)
    target_include_directories(test_http_client PRIVATE tests)
    target_link_libraries(test_http_client PRIVATE spotic_http ws2_32)
    add_test(NAME api_http COMMAND test_http_client)
    set_tests_properties(api_http PROPERTIES TIMEOUT 60)
    set(CRYPTO_VECTORS "${CMAKE_CURRENT_BINARY_DIR}/generated/crypto_vectors.h")
    add_custom_command(OUTPUT "${CRYPTO_VECTORS}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/crypto_vectors.py"
            --header "${CRYPTO_VECTORS}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/crypto_vectors.py"
            "${PROJECT_SOURCE_DIR}/tests/fixtures/crypto-vectors.json"
        COMMENT "Preparing independent CNG crypto fixtures" VERBATIM)
    add_executable(test_crypto tests/test_crypto.c tests/test_crypto_dh.c
        tests/test_crypto_faults.c "${CRYPTO_VECTORS}")
    target_include_directories(test_crypto PRIVATE tests "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(test_crypto PRIVATE spotic_crypto)
    add_test(NAME protocol_crypto COMMAND test_crypto)
    set_tests_properties(protocol_crypto PROPERTIES TIMEOUT 30)
    set(SHANNON_VECTORS "${CMAKE_CURRENT_BINARY_DIR}/generated/shannon_vectors.h")
    add_custom_command(OUTPUT "${SHANNON_VECTORS}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/shannon_vectors.py"
            --header "${SHANNON_VECTORS}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/shannon_vectors.py"
            "${PROJECT_SOURCE_DIR}/tests/fixtures/shannon-vectors.json"
            "${PROJECT_SOURCE_DIR}/tools/shannon_reference/Cargo.lock"
            "${PROJECT_SOURCE_DIR}/tools/shannon_reference/src/main.rs"
            "${PROJECT_SOURCE_DIR}/vendor/manifest.json"
            "${PROJECT_SOURCE_DIR}/vendor/shannon.c"
            "${PROJECT_SOURCE_DIR}/vendor/shannon.h"
            "${PROJECT_SOURCE_DIR}/vendor/licenses/shannon/LICENSE"
        COMMENT "Preparing pinned Shannon known-answer fixtures" VERBATIM)
    add_executable(test_shannon tests/test_shannon.c tests/test_shannon_adapter.c "${SHANNON_VECTORS}")
    target_link_libraries(test_shannon PRIVATE spotic_shannon)
    target_include_directories(test_shannon PRIVATE tests "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME protocol_shannon COMMAND test_shannon)
    set_tests_properties(protocol_shannon PROPERTIES TIMEOUT 30)
    add_executable(test_ap_packet tests/test_ap_packet.c "${SHANNON_VECTORS}")
    target_link_libraries(test_ap_packet PRIVATE spotic_ap)
    target_include_directories(test_ap_packet PRIVATE tests "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME protocol_ap_packet COMMAND test_ap_packet)
    set_tests_properties(protocol_ap_packet PROPERTIES TIMEOUT 30)
    set(AP_VECTORS "${CMAKE_CURRENT_BINARY_DIR}/generated/ap_vectors.h")
    add_custom_command(OUTPUT "${AP_VECTORS}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/ap_vectors.py" --header "${AP_VECTORS}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/ap_vectors.py" "${PROJECT_SOURCE_DIR}/tests/fixtures/crypto-vectors.json"
        COMMENT "Preparing independent AP transcript fixtures" VERBATIM)
    add_executable(test_oauth tests/test_oauth.c tests/test_oauth_http.c tests/support/http_server.c)
    target_include_directories(test_oauth PRIVATE tests)
    target_link_libraries(test_oauth PRIVATE spotic_oauth)
    add_test(NAME api_oauth COMMAND test_oauth)
    set_tests_properties(api_oauth PROPERTIES TIMEOUT 60)
    add_executable(test_credential_store tests/test_credential_store.c tests/test_ap_auth.c
        tests/test_ap_session.c tests/test_ap_keepalive.c "${AP_VECTORS}")
    target_include_directories(test_credential_store PRIVATE tests "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(test_credential_store PRIVATE spotic_ap)
    add_test(NAME protocol_credentials COMMAND test_credential_store)
    set_tests_properties(protocol_credentials PROPERTIES TIMEOUT 30)
    if(ENABLE_LIVE_OAUTH_TEST)
        add_executable(test_oauth_live tests/test_oauth_live.c)
        target_include_directories(test_oauth_live PRIVATE tests)
        target_link_libraries(test_oauth_live PRIVATE spotic_oauth spotic_ap)
        add_test(NAME api_oauth_live COMMAND test_oauth_live)
        set_tests_properties(api_oauth_live PROPERTIES TIMEOUT 300 LABELS "live;account" SKIP_RETURN_CODE 77)
    endif()
    add_executable(test_ap_handshake tests/test_ap_handshake.c tests/test_ap_session.c
        tests/test_ap_keepalive.c tests/test_ap_socket.c tests/test_ap_resolve.c tests/test_ap_http.c
        tests/support/ap_server.c tests/support/http_server.c "${AP_VECTORS}")
    target_link_libraries(test_ap_handshake PRIVATE spotic_ap)
    target_include_directories(test_ap_handshake PRIVATE tests "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME protocol_ap_handshake COMMAND test_ap_handshake)
    set_tests_properties(protocol_ap_handshake PROPERTIES TIMEOUT 60)
    add_executable(test_proto_codec tests/test_proto_codec.c tests/test_proto_messages.c)
    target_link_libraries(test_proto_codec PRIVATE spotic_proto)
    target_include_directories(test_proto_codec PRIVATE tests)
    add_test(NAME protocol_proto COMMAND test_proto_codec)
    set_tests_properties(protocol_proto PROPERTIES TIMEOUT 30)
    add_test(NAME protocol_proto_codegen COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/check_proto.py" --build "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(protocol_proto_codegen PROPERTIES TIMEOUT 60)
    add_library(spotic_test_support STATIC
        tests/support/fixture_clock.c tests/support/fixture_http.c tests/support/fixture_http_client.c)
    target_link_libraries(spotic_test_support PUBLIC spotic_core ws2_32)
    target_include_directories(spotic_test_support PUBLIC tests)

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

    add_executable(test_log tests/test_log.c)
    target_link_libraries(test_log PRIVATE spotic_test_support)
    add_test(NAME core_log COMMAND test_log)
    set_tests_properties(core_log PROPERTIES TIMEOUT 30)

    add_executable(test_metrics tests/test_metrics.c)
    target_link_libraries(test_metrics PRIVATE spotic_test_support)
    add_test(NAME core_metrics COMMAND test_metrics)
    set_tests_properties(core_metrics PROPERTIES TIMEOUT 30)

    add_executable(test_fixtures tests/test_fixtures.c)
    target_link_libraries(test_fixtures PRIVATE spotic_test_support)
    add_test(NAME fixture_http COMMAND test_fixtures)
    set_tests_properties(fixture_http PROPERTIES TIMEOUT 30)
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

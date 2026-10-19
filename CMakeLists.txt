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
option(ENABLE_LIVE_MEDIA_TEST "Enable browser/account-dependent playable media resolution smoke" OFF)
option(ENABLE_LIVE_AUDIO_TEST "Enable physical WASAPI/default-device-change test" OFF)
option(ENABLE_LIVE_HEADLESS_TEST "Enable account/WASAPI headless playback acceptance" OFF)
option(ENABLE_LIVE_CDN_TEST "Enable public-network native CDN HTTPS compatibility test" OFF)
option(ENABLE_LIVE_CONNECT_TEST "Enable account/WASAPI/two-device Connect acceptance" OFF)
option(ENABLE_RENDERER_GPU_TESTS "Enable optional WARP DX11 pixel/device tests" OFF)

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

add_library(spotic_api_contract STATIC src/api/api_contract.c)
target_include_directories(spotic_api_contract PUBLIC src)
target_link_libraries(spotic_api_contract PUBLIC spotic_build_options)

add_library(spotic_json_view STATIC src/api/json_view.c src/api/json_page_pool.c)
target_include_directories(spotic_json_view PUBLIC src)
target_link_libraries(spotic_json_view PUBLIC spotic_json_base)
add_library(spotic_collections STATIC src/api/collection_api.c
    src/api/home_api.c src/api/library_api.c src/api/playlist_api.c)
target_include_directories(spotic_collections PUBLIC src)
target_link_libraries(spotic_collections PUBLIC spotic_json_view spotic_api_contract spotic_media)

add_library(spotic_catalog STATIC src/api/catalog_api.c src/api/catalog_request.c
    src/api/catalog_model.c src/api/search_api.c src/api/album_api.c src/api/artist_api.c)
target_include_directories(spotic_catalog PUBLIC src)
target_link_libraries(spotic_catalog PUBLIC spotic_json_view spotic_api_contract spotic_media)

add_library(spotic_lyrics STATIC src/api/lyrics.c src/api/lyrics_request.c
    src/api/lyrics_parse.c src/api/lyrics_http.c src/api/lyrics_timeline.c)
target_include_directories(spotic_lyrics PUBLIC src)
target_link_libraries(spotic_lyrics PUBLIC spotic_json_view spotic_api_contract spotic_media spotic_player)

add_library(spotic_credentials STATIC src/protocol/credential_store.c)
target_include_directories(spotic_credentials PUBLIC src)
target_link_libraries(spotic_credentials PUBLIC spotic_crypto crypt32 advapi32)
add_library(spotic_oauth STATIC src/api/oauth.c src/api/oauth_loopback.c src/api/oauth_token.c)
target_include_directories(spotic_oauth PUBLIC src)
target_link_libraries(spotic_oauth PUBLIC spotic_http spotic_json_base spotic_credentials shell32 ws2_32)

add_library(spotic_tokens STATIC src/api/token_manager.c src/api/token_scope.c
    src/api/token_hashcash.c src/api/clienttoken.c src/api/login5.c)
target_include_directories(spotic_tokens PUBLIC src)
target_link_libraries(spotic_tokens PUBLIC spotic_http spotic_proto spotic_crypto spotic_credentials)

add_library(spotic_ap_packet STATIC src/protocol/ap_packet.c)
target_include_directories(spotic_ap_packet PUBLIC src)
target_link_libraries(spotic_ap_packet PUBLIC spotic_shannon)

add_library(spotic_mercury STATIC src/protocol/mercury.c src/protocol/mercury_parts.c
    src/protocol/mercury_subscriptions.c)
target_include_directories(spotic_mercury PUBLIC src)
target_link_libraries(spotic_mercury PUBLIC spotic_proto spotic_ap_packet)

add_library(spotic_audio_key STATIC src/protocol/audio_key.c)
target_include_directories(spotic_audio_key PUBLIC src)
target_link_libraries(spotic_audio_key PUBLIC spotic_core spotic_ap_packet)

add_library(spotic_ap STATIC src/protocol/ap_handshake.c
    src/protocol/ap_resolve.c src/protocol/ap_session.c src/protocol/ap_socket.c
    src/protocol/ap_io.c src/protocol/ap_route.c)
target_sources(spotic_ap PRIVATE src/protocol/ap_auth.c)
target_include_directories(spotic_ap PUBLIC src)
target_link_libraries(spotic_ap PUBLIC spotic_audio_key spotic_mercury spotic_proto spotic_crypto spotic_shannon spotic_http spotic_json_base spotic_credentials ws2_32)

add_library(spotic_media STATIC src/protocol/media_source.c src/protocol/media_http.c src/protocol/media_ids.c
    src/api/metadata.c src/api/metadata_wire.c src/api/metadata_policy.c src/api/metadata_track.c src/api/storage_resolve.c)
target_include_directories(spotic_media PUBLIC src)
target_link_libraries(spotic_media PUBLIC spotic_audio_key spotic_tokens spotic_proto winhttp)

add_library(spotic_range STATIC src/audio/range_fetcher.c src/audio/range_leases.c
    src/audio/range_headers.c src/audio/range_http.c src/audio/range_transport.c src/audio/range_schedule.c src/audio/range_poll.c
    src/audio/audio_cache.c src/audio/cache_format.c src/audio/cache_disk.c src/audio/cache_store.c)
target_include_directories(spotic_range PUBLIC src vendor)
target_link_libraries(spotic_range PUBLIC spotic_http spotic_core)
add_library(spotic_cdn STATIC src/api/cdn_client.c src/api/cdn_url.c src/api/cdn_headers.c
    src/api/cdn_socket.c src/api/cdn_tls.c src/api/cdn_exchange.c)
target_include_directories(spotic_cdn PUBLIC src)
target_link_libraries(spotic_cdn PUBLIC spotic_core ws2_32 secur32)

add_library(spotic_decrypt STATIC src/audio/decrypt_stream.c src/audio/spotify_header.c)
target_include_directories(spotic_decrypt PUBLIC src)
target_link_libraries(spotic_decrypt PUBLIC spotic_range spotic_media spotic_crypto)

add_library(spotic_vorbis STATIC src/audio/vorbis_codec.c src/audio/vorbis_decoder.c
    src/audio/vorbis_input.c src/audio/vorbis_seek.c src/audio/ogg_index.c src/audio/audio_convert.c)
target_include_directories(spotic_vorbis PUBLIC src PRIVATE vendor)
target_link_libraries(spotic_vorbis PUBLIC spotic_core)

add_library(spotic_miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(spotic_miniaudio PRIVATE src/audio vendor)
target_link_libraries(spotic_miniaudio PUBLIC spotic_build_options)
add_library(spotic_audio_sink STATIC src/audio/audio_sink.c src/audio/sink_callback.c src/audio/sink_timeline.c
    src/audio/sink_wasapi.c)
target_include_directories(spotic_audio_sink PUBLIC src PRIVATE vendor)
target_link_libraries(spotic_audio_sink PUBLIC spotic_core PRIVATE spotic_miniaudio ole32 user32 synchronization)

add_library(spotic_player STATIC src/audio/player.c src/audio/player_commands.c src/audio/player_queue.c
    src/audio/player_prefetch.c src/audio/player_timeline.c)
target_include_directories(spotic_player PUBLIC src)
target_link_libraries(spotic_player PUBLIC spotic_vorbis spotic_audio_sink)

add_library(spotic_headless STATIC src/headless/runtime.c src/headless/network.c
    src/headless/sources.c src/headless/decode.c src/headless/commands.c
    src/headless/diagnostics.c src/headless/fixture_media.c src/headless/live.c)
target_include_directories(spotic_headless PUBLIC src)

add_library(spotic_tinfl STATIC vendor/miniz/miniz_tinfl.c)
target_include_directories(spotic_tinfl PUBLIC vendor/miniz)
target_compile_definitions(spotic_tinfl PUBLIC MINIZ_NO_MALLOC MINIZ_NO_STDIO MINIZ_NO_TIME
    MINIZ_NO_ARCHIVE_APIS MINIZ_NO_DEFLATE_APIS MINIZ_NO_ZLIB_APIS)
target_link_libraries(spotic_tinfl PUBLIC spotic_build_options)
add_library(spotic_connect STATIC src/protocol/dealer_payload.c src/protocol/connect_state.c
    src/protocol/connect_commands.c src/protocol/connect_transfer.c src/protocol/connect_wire.c
    src/protocol/connect_http.c src/protocol/dealer.c src/protocol/dealer_pump.c src/protocol/dealer_winhttp.c)
target_include_directories(spotic_connect PUBLIC src)
target_link_libraries(spotic_connect PUBLIC spotic_player spotic_tokens spotic_proto spotic_yyjson spotic_tinfl)
target_link_libraries(spotic_headless PUBLIC spotic_player spotic_decrypt spotic_ap spotic_json_base spotic_cdn psapi)
if(MSVC)
    # Fixture/fake output does not need the COM device or credential DLLs.
    # Keep the same binary's opt-in WASAPI/account paths available on demand.
    target_link_options(spotic_headless INTERFACE
        /DELAYLOAD:ole32.dll /DELAYLOAD:USER32.dll /DELAYLOAD:CRYPT32.dll
        /DELAYLOAD:WINHTTP.dll /DELAYLOAD:Secur32.dll)
    target_link_libraries(spotic_headless INTERFACE delayimp)
endif()
add_library(spotic_headless_fixture STATIC src/headless/fixture_server.c)
target_link_libraries(spotic_headless_fixture PUBLIC spotic_build_options ws2_32)
target_include_directories(spotic_headless_fixture PUBLIC src)
add_executable(spotic-headless src/headless/main.c)
target_link_libraries(spotic-headless PRIVATE spotic_headless_fixture spotic_headless)

add_library(spotic_ui STATIC src/ui/ui_scheduler.c src/ui/window.c)
target_include_directories(spotic_ui PUBLIC src)
target_compile_definitions(spotic_ui PUBLIC UNICODE _UNICODE)
target_link_libraries(spotic_ui PUBLIC spotic_core user32 gdi32 dwmapi)
include(cmake/UiShaders.cmake)
add_library(spotic_ui_surface STATIC src/ui/ui_surface.c)
target_link_libraries(spotic_ui_surface PUBLIC spotic_ui spotic_renderer)
add_executable(spotic WIN32 src/ui/main.c)
target_link_libraries(spotic PRIVATE spotic_ui_surface)

if(BUILD_TESTING)
    add_executable(test_renderer tests/test_renderer.c)
    target_include_directories(test_renderer PRIVATE tests)
    target_link_libraries(test_renderer PRIVATE spotic_renderer)
    add_test(NAME ui_renderer COMMAND test_renderer)
    set_tests_properties(ui_renderer PROPERTIES TIMEOUT 30)
    if(ENABLE_RENDERER_GPU_TESTS)
        add_executable(test_renderer_gpu tests/test_renderer_gpu.c)
        target_include_directories(test_renderer_gpu PRIVATE tests)
        target_link_libraries(test_renderer_gpu PRIVATE spotic_renderer)
        add_test(NAME ui_renderer_warp COMMAND test_renderer_gpu)
        set_tests_properties(ui_renderer_warp PROPERTIES TIMEOUT 60 LABELS "gpu;optional" SKIP_RETURN_CODE 77)
        add_executable(test_renderer_window tests/test_renderer_window.c)
        target_include_directories(test_renderer_window PRIVATE tests)
        target_link_libraries(test_renderer_window PRIVATE spotic_ui_surface)
        add_test(NAME ui_renderer_window COMMAND test_renderer_window)
        set_tests_properties(ui_renderer_window PROPERTIES TIMEOUT 60 LABELS "gpu;optional" SKIP_RETURN_CODE 77)
        if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Release")
            add_test(NAME bench_ui_dx11 COMMAND test_renderer_window --bench)
            set_tests_properties(bench_ui_dx11 PROPERTIES TIMEOUT 60 LABELS "gpu;performance" SKIP_RETURN_CODE 77)
        endif()
    endif()
    add_executable(test_ui_scheduler tests/test_ui_scheduler.c)
    target_include_directories(test_ui_scheduler PRIVATE tests)
    target_link_libraries(test_ui_scheduler PRIVATE spotic_ui)
    add_test(NAME ui_scheduler COMMAND test_ui_scheduler)
    set_tests_properties(ui_scheduler PROPERTIES TIMEOUT 30)
    add_executable(test_ui_window tests/test_ui_window.c)
    target_include_directories(test_ui_window PRIVATE tests)
    target_link_libraries(test_ui_window PRIVATE spotic_ui oleacc ole32 oleaut32 uuid)
    add_test(NAME ui_window COMMAND test_ui_window)
    set_tests_properties(ui_window PROPERTIES TIMEOUT 30)
    if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Release")
        add_test(NAME bench_ui_idle COMMAND test_ui_window --bench)
        set_tests_properties(bench_ui_idle PROPERTIES TIMEOUT 30 LABELS "performance")
    endif()
    add_executable(test_lyrics tests/test_lyrics.c tests/test_lyrics_errors.c
        tests/test_lyrics_timeline.c tests/test_lyrics_http.c tests/test_lyrics_lifetime.c tests/player_test_support.c
        tests/support/http_server.c)
    target_include_directories(test_lyrics PRIVATE tests)
    target_compile_definitions(test_lyrics PRIVATE
        API_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/api"
        VORBIS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vorbis")
    target_link_libraries(test_lyrics PRIVATE spotic_lyrics spotic_collections spotic_catalog ws2_32)
    add_test(NAME api_lyrics COMMAND test_lyrics)
    set_tests_properties(api_lyrics PROPERTIES TIMEOUT 60)
    add_executable(test_catalog_api tests/test_catalog_api.c tests/test_catalog_errors.c
        tests/test_catalog_pool.c)
    target_include_directories(test_catalog_api PRIVATE tests)
    target_compile_definitions(test_catalog_api PRIVATE API_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/api")
    target_link_libraries(test_catalog_api PRIVATE spotic_catalog spotic_collections)
    add_test(NAME api_catalog COMMAND test_catalog_api)
    set_tests_properties(api_catalog PROPERTIES TIMEOUT 30)
    add_executable(test_collection_api tests/test_collection_api.c tests/test_collection_errors.c
        tests/test_collection_races.c)
    target_include_directories(test_collection_api PRIVATE tests)
    target_compile_definitions(test_collection_api PRIVATE
        API_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/api")
    target_link_libraries(test_collection_api PRIVATE spotic_collections)
    add_test(NAME api_collections COMMAND test_collection_api)
    set_tests_properties(api_collections PROPERTIES TIMEOUT 30)
    add_executable(test_api_contract tests/test_api_contract.c tests/test_api_contract_manifest.c)
    target_include_directories(test_api_contract PRIVATE tests)
    target_compile_definitions(test_api_contract PRIVATE
        API_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/api")
    target_link_libraries(test_api_contract PRIVATE spotic_api_contract spotic_yyjson spotic_crypto)
    add_test(NAME api_contract COMMAND test_api_contract)
    set_tests_properties(api_contract PROPERTIES TIMEOUT 30)
    add_executable(test_cdn tests/test_cdn.c tests/cdn_tls_peer.c)
    target_include_directories(test_cdn PRIVATE tests)
    target_compile_definitions(test_cdn PRIVATE HEADLESS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio")
    target_compile_definitions(test_cdn PRIVATE CDN_PYTHON="${Python3_EXECUTABLE}"
        CDN_SERVER_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/tools/cdn_tls_peer.py")
    target_link_libraries(test_cdn PRIVATE spotic_cdn spotic_headless_fixture)
    add_test(NAME audio_cdn_transport COMMAND test_cdn)
    set_tests_properties(audio_cdn_transport PROPERTIES TIMEOUT 90)
    if(ENABLE_LIVE_CDN_TEST)
        add_executable(test_cdn_live tests/test_cdn_live.c)
        target_include_directories(test_cdn_live PRIVATE tests)
        target_link_libraries(test_cdn_live PRIVATE spotic_cdn)
        add_test(NAME audio_cdn_live COMMAND test_cdn_live)
        set_tests_properties(audio_cdn_live PROPERTIES TIMEOUT 30 LABELS "live;network")
    endif()
    add_executable(test_headless tests/test_headless.c tests/headless_support.c)
    target_include_directories(test_headless PRIVATE tests)
    target_compile_definitions(test_headless PRIVATE HEADLESS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio"
        HEADLESS_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(test_headless PRIVATE spotic_headless_fixture spotic_headless)
    add_test(NAME audio_headless_smoke COMMAND test_headless)
    set_tests_properties(audio_headless_smoke PROPERTIES TIMEOUT 120)
    if(ENABLE_LIVE_HEADLESS_TEST)
        add_executable(test_headless_live tests/test_headless_live.c)
        target_include_directories(test_headless_live PRIVATE tests)
        target_link_libraries(test_headless_live PRIVATE spotic_headless)
        add_test(NAME audio_headless_live COMMAND test_headless_live)
        set_tests_properties(audio_headless_live PROPERTIES TIMEOUT 180 LABELS "live;account;hardware" SKIP_RETURN_CODE 77)
    endif()
    if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Release")
        add_executable(bench_headless tests/bench_headless.c tests/headless_support.c)
        target_include_directories(bench_headless PRIVATE tests)
        target_compile_definitions(bench_headless PRIVATE HEADLESS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio"
            HEADLESS_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(bench_headless PRIVATE spotic_headless_fixture spotic_headless)
        add_test(NAME bench_headless COMMAND bench_headless)
        set_tests_properties(bench_headless PROPERTIES TIMEOUT 660 LABELS "performance")
    endif()
    foreach(player_test IN ITEMS player gapless)
        add_executable(test_${player_test} tests/test_${player_test}.c tests/player_test_support.c)
        target_include_directories(test_${player_test} PRIVATE tests)
        target_compile_definitions(test_${player_test} PRIVATE VORBIS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vorbis")
        target_link_libraries(test_${player_test} PRIVATE spotic_player)
        add_test(NAME audio_${player_test} COMMAND test_${player_test})
        set_tests_properties(audio_${player_test} PROPERTIES TIMEOUT 90)
    endforeach()
    foreach(connect_test IN ITEMS dealer connect)
        add_executable(test_${connect_test} tests/test_${connect_test}.c tests/player_test_support.c)
        target_include_directories(test_${connect_test} PRIVATE tests)
        target_compile_definitions(test_${connect_test} PRIVATE
            VORBIS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vorbis"
            CONNECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/connect")
        target_link_libraries(test_${connect_test} PRIVATE spotic_connect)
        add_test(NAME protocol_${connect_test} COMMAND test_${connect_test})
        set_tests_properties(protocol_${connect_test} PROPERTIES TIMEOUT 90)
    endforeach()
    target_sources(test_connect PRIVATE tests/test_connect_http.c tests/support/http_server.c)
    target_link_libraries(test_connect PRIVATE ws2_32)
    target_sources(test_dealer PRIVATE tests/test_dealer_winhttp.c)
    target_link_libraries(test_dealer PRIVATE ws2_32)
    if(ENABLE_LIVE_CONNECT_TEST)
        add_executable(test_connect_live tests/test_connect_live.c)
        target_include_directories(test_connect_live PRIVATE tests)
        target_link_libraries(test_connect_live PRIVATE spotic_connect spotic_headless)
        add_test(NAME protocol_connect_live COMMAND test_connect_live)
        set_tests_properties(protocol_connect_live PROPERTIES TIMEOUT 360 LABELS "live;account;hardware" SKIP_RETURN_CODE 77)
    endif()
    add_executable(test_audio_sink tests/test_audio_sink.c tests/test_sink_races.c)
    target_include_directories(test_audio_sink PRIVATE tests)
    target_link_libraries(test_audio_sink PRIVATE spotic_audio_sink)
    add_test(NAME audio_sink COMMAND test_audio_sink)
    set_tests_properties(audio_sink PROPERTIES TIMEOUT 90)
    if(ENABLE_LIVE_AUDIO_TEST)
        add_executable(test_audio_sink_live tests/test_audio_sink_live.c)
        target_include_directories(test_audio_sink_live PRIVATE tests)
        target_link_libraries(test_audio_sink_live PRIVATE spotic_audio_sink)
        add_test(NAME audio_sink_live COMMAND test_audio_sink_live)
        set_tests_properties(audio_sink_live PROPERTIES TIMEOUT 120 LABELS "live;hardware" SKIP_RETURN_CODE 77)
    endif()
    add_executable(test_vorbis_stream tests/test_vorbis_stream.c tests/test_vorbis_errors.c
        tests/test_audio_convert.c tests/test_vorbis_limits.c tests/vorbis_test_support.c)
    target_include_directories(test_vorbis_stream PRIVATE tests vendor)
    target_compile_definitions(test_vorbis_stream PRIVATE VORBIS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vorbis")
    target_link_libraries(test_vorbis_stream PRIVATE spotic_vorbis)
    add_test(NAME audio_vorbis_stream COMMAND test_vorbis_stream)
    set_tests_properties(audio_vorbis_stream PROPERTIES TIMEOUT 90)
    add_executable(test_audio_seek tests/test_audio_seek.c tests/test_ogg_index.c tests/vorbis_test_support.c)
    target_include_directories(test_audio_seek PRIVATE tests vendor)
    target_compile_definitions(test_audio_seek PRIVATE VORBIS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vorbis")
    target_link_libraries(test_audio_seek PRIVATE spotic_vorbis)
    add_test(NAME audio_seek COMMAND test_audio_seek)
    set_tests_properties(audio_seek PROPERTIES TIMEOUT 90)
    add_executable(test_decrypt_stream tests/test_decrypt_stream.c tests/test_decrypt_errors.c
        tests/test_decrypt_lifecycle.c)
    target_include_directories(test_decrypt_stream PRIVATE tests)
    target_compile_definitions(test_decrypt_stream PRIVATE
        DECRYPT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/decrypt")
    target_link_libraries(test_decrypt_stream PRIVATE spotic_decrypt)
    add_test(NAME audio_ctr_stream COMMAND test_decrypt_stream)
    set_tests_properties(audio_ctr_stream PROPERTIES TIMEOUT 60)
    add_executable(test_spotify_header tests/test_spotify_header.c)
    target_include_directories(test_spotify_header PRIVATE tests)
    target_link_libraries(test_spotify_header PRIVATE spotic_decrypt)
    add_test(NAME audio_spotify_header COMMAND test_spotify_header)
    set_tests_properties(audio_spotify_header PROPERTIES TIMEOUT 15)
    add_executable(test_range_cache tests/test_range_cache.c tests/test_cache_disk.c
        tests/test_range_errors.c tests/test_range_races.c tests/range_test_support.c tests/support/http_server.c)
    target_include_directories(test_range_cache PRIVATE tests)
    target_link_libraries(test_range_cache PRIVATE spotic_range ws2_32)
    add_test(NAME audio_range_cache COMMAND test_range_cache)
    set_tests_properties(audio_range_cache PROPERTIES TIMEOUT 90)
    add_executable(test_media_source tests/test_media_source.c tests/test_metadata.c
        tests/test_storage_resolve.c tests/support/http_server.c)
    target_include_directories(test_media_source PRIVATE tests)
    target_link_libraries(test_media_source PRIVATE spotic_media ws2_32)
    add_test(NAME protocol_media_source COMMAND test_media_source)
    set_tests_properties(protocol_media_source PROPERTIES TIMEOUT 60)
    if(ENABLE_LIVE_MEDIA_TEST)
        add_executable(test_media_live tests/test_media_live.c)
        target_include_directories(test_media_live PRIVATE tests)
        target_link_libraries(test_media_live PRIVATE spotic_media spotic_ap spotic_oauth)
        add_test(NAME protocol_media_live COMMAND test_media_live)
        set_tests_properties(protocol_media_live PROPERTIES TIMEOUT 360 LABELS "live;account" SKIP_RETURN_CODE 77)
    endif()
    add_executable(test_audio_key tests/test_audio_key.c)
    target_include_directories(test_audio_key PRIVATE tests)
    target_link_libraries(test_audio_key PRIVATE spotic_audio_key)
    add_test(NAME protocol_audio_key COMMAND test_audio_key)
    set_tests_properties(protocol_audio_key PROPERTIES TIMEOUT 30)
    add_executable(test_mercury tests/test_mercury.c tests/test_mercury_limits.c
        tests/test_mercury_subscriptions.c)
    target_include_directories(test_mercury PRIVATE tests)
    target_link_libraries(test_mercury PRIVATE spotic_ap)
    add_test(NAME protocol_mercury COMMAND test_mercury)
    set_tests_properties(protocol_mercury PROPERTIES TIMEOUT 30)
    set(AUTH_VECTORS "${CMAKE_CURRENT_BINARY_DIR}/generated/auth_vectors.h")
    add_custom_command(OUTPUT "${AUTH_VECTORS}"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/auth_fixtures.py" --header "${AUTH_VECTORS}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/auth_fixtures.py" "${PROJECT_SOURCE_DIR}/tests/fixtures/auth/vectors.json"
        COMMENT "Preparing independent sanitized authentication fixtures" VERBATIM)
    add_executable(test_token_manager tests/test_token_manager.c tests/test_token_challenges.c
        tests/test_token_errors.c tests/support/http_server.c "${AUTH_VECTORS}")
    target_include_directories(test_token_manager PRIVATE tests "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(test_token_manager PRIVATE spotic_tokens ws2_32)
    add_test(NAME api_tokens COMMAND test_token_manager)
    set_tests_properties(api_tokens PROPERTIES TIMEOUT 60)
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

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(PROTO_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(READ "${PROJECT_SOURCE_DIR}/proto/manifest.json" PROTO_MANIFEST)
string(JSON PROTO_COUNT LENGTH "${PROTO_MANIFEST}" files)
math(EXPR PROTO_LAST "${PROTO_COUNT} - 1")
set(PROTO_SOURCES)
set(PROTO_INPUTS)
foreach(INDEX RANGE ${PROTO_LAST})
    string(JSON NAME GET "${PROTO_MANIFEST}" files ${INDEX} path)
    string(REGEX REPLACE "\\.proto$" ".pb.c" SOURCE "${NAME}")
    string(REGEX REPLACE "\\.proto$" ".pb.h" HEADER "${NAME}")
    string(REGEX REPLACE "\\.proto$" ".options" OPTIONS "${NAME}")
    list(APPEND PROTO_SOURCES "${PROTO_OUTPUT}/${SOURCE}")
    list(APPEND PROTO_HEADERS "${PROTO_OUTPUT}/${HEADER}")
    list(APPEND PROTO_INPUTS "${PROJECT_SOURCE_DIR}/proto/${NAME}"
         "${PROJECT_SOURCE_DIR}/proto/${OPTIONS}")
endforeach()
file(GLOB_RECURSE NANOPB_GENERATOR CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/vendor/nanopb/generator/*.py"
    "${PROJECT_SOURCE_DIR}/vendor/nanopb/generator/*.proto")
add_custom_command(OUTPUT ${PROTO_SOURCES} ${PROTO_HEADERS}
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_proto.py"
        --output "${PROTO_OUTPUT}"
    DEPENDS ${PROTO_INPUTS} ${NANOPB_GENERATOR}
        "${PROJECT_SOURCE_DIR}/tools/generate_proto.py"
        "${PROJECT_SOURCE_DIR}/tools/proto-requirements.txt"
        "${PROJECT_SOURCE_DIR}/proto/manifest.json"
        "${PROJECT_SOURCE_DIR}/vendor/manifest.json"
    COMMENT "Generating the pinned, bounded Spotify nanopb closure" VERBATIM)
add_library(spotic_proto STATIC ${PROTO_SOURCES}
    "${PROJECT_SOURCE_DIR}/vendor/nanopb/pb_common.c"
    "${PROJECT_SOURCE_DIR}/vendor/nanopb/pb_encode.c"
    "${PROJECT_SOURCE_DIR}/vendor/nanopb/pb_decode.c"
    "${PROJECT_SOURCE_DIR}/src/protocol/proto_codec.c"
    "${PROJECT_SOURCE_DIR}/src/protocol/proto_track.c")
target_include_directories(spotic_proto PUBLIC "${PROTO_OUTPUT}"
    "${PROJECT_SOURCE_DIR}/vendor/nanopb")
target_compile_definitions(spotic_proto PUBLIC PB_VALIDATE_UTF8=1)
target_link_libraries(spotic_proto PUBLIC spotic_core)

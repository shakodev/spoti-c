if(NOT DEFINED TEST_PROGRAM OR NOT DEFINED MODE)
    message(FATAL_ERROR "TEST_PROGRAM and MODE are required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "ASAN_OPTIONS=allow_user_poisoning=1:halt_on_error=1:abort_on_error=0:exitcode=86"
        "${TEST_PROGRAM}" "${MODE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 10)

if(NOT "${result}" STREQUAL "86" OR
   NOT "${output}${error}" MATCHES "AddressSanitizer: use-after-poison")
    message(FATAL_ERROR "Expected ASan use-after-poison for ${MODE}; got ${result}\n${output}${error}")
endif()
message(STATUS "ASan caught the intentional ${MODE} fixture misuse")

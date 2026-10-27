if(NOT DEFINED RUNNER)
  message(FATAL_ERROR "RUNNER is required")
endif()

execute_process(COMMAND "${RUNNER}" --filter=missing_case
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${code}" STREQUAL "1" OR NOT output MATCHES "TESTS 0/0" OR
   NOT errors MATCHES "no tests selected")
  message(FATAL_ERROR "Empty selection must fail: code=${code} stdout=${output} stderr=${errors}")
endif()
message(STATUS "harness empty selection PASS")

execute_process(COMMAND "${RUNNER}" duplicate
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${code}" STREQUAL "1" OR NOT errors MATCHES "duplicate test")
  message(FATAL_ERROR "Duplicate registration must fail: code=${code} stdout=${output} stderr=${errors}")
endif()
message(STATUS "harness duplicate registration PASS")

execute_process(COMMAND "${RUNNER}" overflow --filter=probe
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${code}" STREQUAL "1" OR NOT errors MATCHES "registration capacity exceeded")
  message(FATAL_ERROR "Overflow must fail even with a valid filter: code=${code} stdout=${output} stderr=${errors}")
endif()
message(STATUS "harness registration overflow PASS")

execute_process(COMMAND "${RUNNER}"
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${code}" STREQUAL "0" OR NOT output MATCHES "TESTS 1/1" OR NOT "${errors}" STREQUAL "")
  message(FATAL_ERROR "Normal suite must pass: code=${code} stdout=${output} stderr=${errors}")
endif()

execute_process(COMMAND "${RUNNER}" failure
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${code}" STREQUAL "1" OR NOT output MATCHES "FAIL probe" OR NOT output MATCHES "TESTS 0/1")
  message(FATAL_ERROR "Assertion must fail: code=${code} stdout=${output} stderr=${errors}")
endif()

execute_process(COMMAND "${RUNNER}" --filter=
  RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT "${code}" STREQUAL "1" OR NOT errors MATCHES "empty filter")
  message(FATAL_ERROR "Explicit empty filter must fail: code=${code} stdout=${output} stderr=${errors}")
endif()
message(STATUS "harness positive, assertion, and explicit empty filter PASS")

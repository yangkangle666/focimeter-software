foreach(required_variable IN ITEMS
        M2_EXECUTABLE M2_INPUT M2_OUTPUT M2_PROJECT_ROOT
        M2_EXPECTED_TASK_ID M2_EXPECTED_CALIBRATION_ERROR_CODE
        M2_EXPECTED_MEASUREMENT_ERROR_CODE)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${M2_EXECUTABLE}"
        --input "${M2_INPUT}"
        --output "${M2_OUTPUT}"
        --project-root "${M2_PROJECT_ROOT}"
        --experimental-multispot
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT result EQUAL 3)
    message(FATAL_ERROR "Expected experimental M2 exit code 3, got ${result}.\n${stdout}\n${stderr}")
endif()

set(experimental_directory "${M2_OUTPUT}/experimental_multispot")
foreach(output_name IN ITEMS spots_calib_multispot.json spots_meas_multispot.json)
    if(output_name STREQUAL "spots_calib_multispot.json")
        set(expected_error_code "${M2_EXPECTED_CALIBRATION_ERROR_CODE}")
    else()
        set(expected_error_code "${M2_EXPECTED_MEASUREMENT_ERROR_CODE}")
    endif()
    set(output_path "${experimental_directory}/${output_name}")
    if(NOT EXISTS "${output_path}")
        message(FATAL_ERROR "Expected experimental failure output does not exist: ${output_path}")
    endif()
    file(READ "${output_path}" document)
    string(JSON status ERROR_VARIABLE json_error GET "${document}" status)
    string(JSON schema_version ERROR_VARIABLE schema_error GET "${document}" schema_version)
    string(JSON task_id ERROR_VARIABLE task_error GET "${document}" task_id)
    string(JSON experimental ERROR_VARIABLE experimental_error GET "${document}" experimental)
    string(JSON contract_status ERROR_VARIABLE contract_error GET "${document}" contract_status)
    string(JSON validation_scope ERROR_VARIABLE scope_error GET "${document}" validation_scope)
    string(JSON physical_identity ERROR_VARIABLE identity_error GET "${document}" matching physical_identity_guaranteed)
    string(JSON error_code ERROR_VARIABLE code_error GET "${document}" error code)
    string(JSON error_message ERROR_VARIABLE message_error GET "${document}" error message)
    if(json_error OR NOT status STREQUAL "error")
        message(FATAL_ERROR "${output_name} must contain status=error")
    endif()
    if(schema_error OR NOT schema_version STREQUAL "m2.multispot.experimental.1")
        message(FATAL_ERROR "${output_name} must use the isolated experimental schema")
    endif()
    if(task_error OR NOT task_id STREQUAL M2_EXPECTED_TASK_ID)
        message(FATAL_ERROR "${output_name} must retain task_id=${M2_EXPECTED_TASK_ID}")
    endif()
    if(experimental_error OR NOT experimental)
        message(FATAL_ERROR "${output_name} must be explicitly experimental")
    endif()
    if(contract_error OR NOT contract_status STREQUAL "proposed")
        message(FATAL_ERROR "${output_name} must retain contract_status=proposed")
    endif()
if(scope_error OR NOT validation_scope STREQUAL "software_only")
    message(FATAL_ERROR "${output_name} must retain validation_scope=software_only")
endif()
    if(identity_error OR physical_identity)
        message(FATAL_ERROR "${output_name} must not guarantee physical identity")
    endif()
    if(code_error OR NOT error_code STREQUAL expected_error_code)
        message(FATAL_ERROR "${output_name} must contain error.code=${expected_error_code}")
    endif()
    if(message_error OR error_message STREQUAL "")
        message(FATAL_ERROR "${output_name} must contain a non-empty error.message")
    endif()
endforeach()

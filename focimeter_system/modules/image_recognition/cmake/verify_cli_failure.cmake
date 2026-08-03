foreach(required_variable IN ITEMS M2_EXECUTABLE M2_INPUT M2_OUTPUT M2_PROJECT_ROOT)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${M2_EXECUTABLE}"
        --input "${M2_INPUT}"
        --output "${M2_OUTPUT}"
        --project-root "${M2_PROJECT_ROOT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT result EQUAL 3)
    message(FATAL_ERROR "Expected M2 exit code 3, got ${result}.\n${stdout}\n${stderr}")
endif()

foreach(output_name IN ITEMS spots_calib.json spots_meas.json)
    set(output_path "${M2_OUTPUT}/${output_name}")
    if(NOT EXISTS "${output_path}")
        message(FATAL_ERROR "Expected failure output does not exist: ${output_path}")
    endif()
    file(READ "${output_path}" document)
    string(JSON status ERROR_VARIABLE json_error GET "${document}" status)
    if(json_error OR NOT status STREQUAL "error")
        message(FATAL_ERROR "${output_name} must contain status=error")
    endif()
endforeach()

file(READ "${M2_OUTPUT}/spots_meas.json" measurement_document)
string(JSON measurement_code ERROR_VARIABLE json_error GET "${measurement_document}" error code)
if(json_error OR NOT measurement_code STREQUAL "SPOT_COUNT_MISMATCH")
    message(FATAL_ERROR "Measurement failure must be SPOT_COUNT_MISMATCH")
endif()

file(READ "${M2_OUTPUT}/spots_calib.json" calibration_document)
string(JSON calibration_code ERROR_VARIABLE json_error GET "${calibration_document}" error code)
if(json_error OR NOT calibration_code STREQUAL "COORDINATE_SYSTEM_INVALID")
    message(FATAL_ERROR "Calibration failure must be COORDINATE_SYSTEM_INVALID")
endif()

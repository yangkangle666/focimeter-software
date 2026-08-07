foreach(required_variable IN ITEMS
        M2_EXECUTABLE M2_INPUT M2_OUTPUT M2_PROJECT_ROOT
        M2_EXPECTED_MEASUREMENT_WARNING M2_EXPECTED_OUTPUT_ROOT
        M2_EXPECTED_TASK_ID)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${M2_OUTPUT}")
set(normalized_input_path "${M2_OUTPUT}-input.json")
file(REMOVE "${normalized_input_path}")
file(READ "${M2_INPUT}" input_document)
string(
    JSON normalized_input
    ERROR_VARIABLE input_error
    SET "${input_document}" task_id "\"${M2_EXPECTED_TASK_ID}\"")
if(input_error)
    message(FATAL_ERROR "Could not normalize real JPEG task_id: ${input_error}")
endif()
file(WRITE "${normalized_input_path}" "${normalized_input}\n")
execute_process(
    COMMAND "${M2_EXECUTABLE}"
        --input "${normalized_input_path}"
        --output "${M2_OUTPUT}"
        --project-root "${M2_PROJECT_ROOT}"
        --experimental-multispot
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Expected real JPEG M2 success, got ${result}.\n${stdout}\n${stderr}")
endif()

set(experimental_directory "${M2_OUTPUT}/experimental_multispot")
foreach(output_name IN ITEMS spots_calib_multispot.json spots_meas_multispot.json)
    set(output_path "${experimental_directory}/${output_name}")
    if(NOT EXISTS "${output_path}")
        message(FATAL_ERROR "Expected real JPEG output does not exist: ${output_path}")
    endif()
    file(READ "${output_path}" document)
    string(JSON status ERROR_VARIABLE status_error GET "${document}" status)
    string(JSON schema_version ERROR_VARIABLE schema_error GET "${document}" schema_version)
    string(JSON data_source ERROR_VARIABLE source_error GET "${document}" data_source)
    string(JSON validation_status ERROR_VARIABLE validation_error GET "${document}" validation_status)
    string(JSON validation_scope ERROR_VARIABLE scope_error GET "${document}" validation_scope)
    string(JSON metrology_validated ERROR_VARIABLE metrology_error GET "${document}" metrology_validated)
    string(JSON matching_status ERROR_VARIABLE matching_error GET "${document}" matching status)
    string(JSON id_scope ERROR_VARIABLE id_scope_error GET "${document}" matching id_scope)
    string(JSON physical_identity ERROR_VARIABLE identity_error GET "${document}" matching physical_identity_guaranteed)
    string(JSON is_usable ERROR_VARIABLE usable_error GET "${document}" quality is_usable)
    string(JSON spot_count ERROR_VARIABLE spots_error LENGTH "${document}" spots)
    string(JSON detected_count ERROR_VARIABLE detected_error GET "${document}" quality detected_count)

    if(status_error OR NOT status STREQUAL "ok")
        message(FATAL_ERROR "${output_name} must contain status=ok")
    endif()
    if(schema_error OR NOT schema_version STREQUAL "m2.multispot.experimental.1")
        message(FATAL_ERROR "${output_name} must use the experimental schema")
    endif()
    if(source_error OR NOT data_source STREQUAL "real")
        message(FATAL_ERROR "${output_name} must retain data_source=real")
    endif()
    if(validation_error OR NOT validation_status STREQUAL "software_verified" OR
       scope_error OR NOT validation_scope STREQUAL "software_only" OR
       metrology_error OR metrology_validated)
        message(FATAL_ERROR "${output_name} must remain software-only and not metrology validated")
    endif()
    if(matching_error OR NOT matching_status STREQUAL "not_performed" OR
       id_scope_error OR NOT id_scope STREQUAL "image_local" OR
       identity_error OR physical_identity)
        message(FATAL_ERROR "${output_name} must not claim cross-image physical identity")
    endif()
    if(usable_error OR NOT is_usable)
        message(FATAL_ERROR "${output_name} must remain usable for software integration")
    endif()
    if(spots_error OR detected_error OR NOT spot_count EQUAL detected_count)
        message(FATAL_ERROR "${output_name} spots length must equal quality.detected_count")
    endif()
    string(FIND "${document}" "\"spot_id\"" spot_id_position)
    if(NOT spot_id_position EQUAL -1)
        message(FATAL_ERROR "${output_name} must not fabricate spot_id")
    endif()

    if(output_name STREQUAL "spots_calib_multispot.json")
        set(expected_warning "EDGE_CLIPPED_CANDIDATE_REJECTED")
    else()
        set(expected_warning "${M2_EXPECTED_MEASUREMENT_WARNING}")
    endif()

    string(JSON warning_count ERROR_VARIABLE warnings_error LENGTH "${document}" quality warnings)
    if(warnings_error)
        message(FATAL_ERROR "${output_name} must contain a valid quality.warnings array")
    endif()
    set(has_expected_warning FALSE)
    set(has_merged_warning FALSE)
    if(warning_count GREATER 0)
        math(EXPR last_warning_index "${warning_count} - 1")
        foreach(warning_index RANGE 0 ${last_warning_index})
            string(JSON warning GET "${document}" quality warnings ${warning_index})
            if(warning STREQUAL expected_warning)
                set(has_expected_warning TRUE)
            endif()
            if(warning STREQUAL "POSSIBLE_MERGED_COMPONENT")
                set(has_merged_warning TRUE)
            endif()
        endforeach()
    endif()
    if(output_name STREQUAL "spots_meas_multispot.json" AND has_merged_warning)
        message(FATAL_ERROR "${output_name} must not fail or warn solely because a large component is near-circular")
    endif()
    if(NOT has_expected_warning)
        message(FATAL_ERROR "${output_name} must report ${expected_warning}")
    endif()

    set(expected_path "${M2_EXPECTED_OUTPUT_ROOT}/${output_name}")
    if(NOT EXISTS "${expected_path}")
        message(FATAL_ERROR "Expected checked-in real JPEG output does not exist: ${expected_path}")
    endif()
    file(READ "${expected_path}" expected_document)
    string(
        JSON canonical_output
        ERROR_VARIABLE output_json_error
        SET "${document}" task_id "\"${M2_EXPECTED_TASK_ID}\"")
    string(
        JSON canonical_expected
        ERROR_VARIABLE expected_json_error
        SET "${expected_document}" task_id "\"${M2_EXPECTED_TASK_ID}\"")
    if(output_json_error OR expected_json_error)
        message(FATAL_ERROR
            "Could not canonicalize ${output_name}: "
            "output=${output_json_error}; expected=${expected_json_error}")
    endif()
    set(output_canonical_path "${M2_OUTPUT}/${output_name}.canonical")
    set(expected_canonical_path "${M2_OUTPUT}/${output_name}.expected.canonical")
    file(WRITE "${output_canonical_path}" "${canonical_output}\n")
    file(WRITE "${expected_canonical_path}" "${canonical_expected}\n")
    file(SHA256 "${output_canonical_path}" output_sha256)
    file(SHA256 "${expected_canonical_path}" expected_sha256)
    file(REMOVE "${output_canonical_path}" "${expected_canonical_path}")
    if(NOT output_sha256 STREQUAL expected_sha256)
        message(FATAL_ERROR
            "${output_name} changed relative to the M3 integration fixture. "
            "Generated canonical SHA-256=${output_sha256}; "
            "expected canonical SHA-256=${expected_sha256}")
    endif()
    message(STATUS "${output_name} canonical SHA-256 matches ${output_sha256}")
endforeach()

file(REMOVE "${normalized_input_path}")

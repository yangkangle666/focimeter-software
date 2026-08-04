foreach(required_variable IN ITEMS
        M2_EXECUTABLE M2_INPUT M2_OUTPUT M2_PROJECT_ROOT
        M2_EXPECTED_MEASUREMENT_WARNING M2_EXPECTED_SPOT_COUNT)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${M2_OUTPUT}")
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
    if(spots_error OR NOT spot_count EQUAL M2_EXPECTED_SPOT_COUNT)
        message(FATAL_ERROR
            "${output_name} software regression count changed: expected ${M2_EXPECTED_SPOT_COUNT}, got ${spot_count}")
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
endforeach()

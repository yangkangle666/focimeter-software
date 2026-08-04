foreach(required_variable IN ITEMS M2_GENERATOR M2_OUTPUT_ROOT M2_REFERENCE_ROOT)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(first "${M2_OUTPUT_ROOT}/first")
set(second "${M2_OUTPUT_ROOT}/second")
file(REMOVE_RECURSE "${first}" "${second}")

foreach(destination IN ITEMS "${first}" "${second}")
    execute_process(
        COMMAND "${M2_GENERATOR}" --output "${destination}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Multispot generator failed for ${destination}.\n${stdout}\n${stderr}")
    endif()
endforeach()

file(GLOB_RECURSE first_files RELATIVE "${first}" "${first}/*")
file(GLOB_RECURSE second_files RELATIVE "${second}" "${second}/*")
list(SORT first_files)
list(SORT second_files)
if(NOT first_files STREQUAL second_files)
    message(FATAL_ERROR "Repeated generation produced different file lists")
endif()

foreach(relative_path IN LISTS first_files)
    if(NOT IS_DIRECTORY "${first}/${relative_path}")
        file(SHA256 "${first}/${relative_path}" first_hash)
        file(SHA256 "${second}/${relative_path}" second_hash)
        if(NOT first_hash STREQUAL second_hash)
            message(FATAL_ERROR "Repeated generation changed ${relative_path}")
        endif()
    endif()
endforeach()

file(GLOB_RECURSE reference_files RELATIVE "${M2_REFERENCE_ROOT}" "${M2_REFERENCE_ROOT}/*")
list(FILTER reference_files EXCLUDE REGEX "^README\\.md$")
list(SORT reference_files)
if(NOT first_files STREQUAL reference_files)
    message(FATAL_ERROR "Generated file list differs from the committed synthetic dataset")
endif()
foreach(relative_path IN LISTS first_files)
    if(NOT IS_DIRECTORY "${first}/${relative_path}")
        file(SHA256 "${first}/${relative_path}" generated_hash)
        file(SHA256 "${M2_REFERENCE_ROOT}/${relative_path}" reference_hash)
        if(NOT generated_hash STREQUAL reference_hash)
            message(FATAL_ERROR "Committed synthetic file is stale: ${relative_path}")
        endif()
    endif()
endforeach()

file(REMOVE_RECURSE "${first}" "${second}")

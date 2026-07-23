cmake_minimum_required(VERSION 3.21)

foreach(required IN ITEMS M2_EXECUTABLE M2_DESTINATION M2_SEARCH_DIRECTORY M2_LOCK_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required runtime-deployment variable: ${required}")
    endif()
endforeach()

file(
    LOCK "${M2_LOCK_FILE}"
    GUARD PROCESS
    TIMEOUT 60
    RESULT_VARIABLE lock_result
)
if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Could not acquire the M2 runtime-deployment lock: ${lock_result}")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${M2_EXECUTABLE}"
    DIRECTORIES "${M2_SEARCH_DIRECTORY}"
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
    CONFLICTING_DEPENDENCIES_PREFIX m2_runtime_conflicts
    PRE_EXCLUDE_REGEXES
        "api-ms-win-.*"
        "ext-ms-win-.*"
    POST_EXCLUDE_REGEXES
        ".*[\\/]Windows[\\/]System32[\\/].*"
        ".*[\\/]Windows[\\/]SysWOW64[\\/].*"
)

set(third_party_dependency_pattern
    "^(opencv_|abseil|jpeg|liblzma|libpng|libprotobuf|libsharpyuv|libusb|libwebp|pkgconf|tiff|turbojpeg|zlib).*[.]dll$")
foreach(dependency IN LISTS unresolved_dependencies)
    string(TOLOWER "${dependency}" dependency_lower)
    if(dependency_lower MATCHES "${third_party_dependency_pattern}")
        message(FATAL_ERROR "Required third-party runtime dependency was not resolved: ${dependency}")
    endif()
endforeach()

# Windows dependency scanning can report optional OS delay-load libraries and the
# MSVC debug runtime as unresolved. They are supplied by Windows/Visual Studio and
# must not be copied into the M2 deliverable.
if(unresolved_dependencies)
    list(LENGTH unresolved_dependencies unresolved_count)
    message(STATUS "Ignoring ${unresolved_count} Windows/toolchain-provided runtime dependencies")
endif()

file(TO_CMAKE_PATH "${M2_SEARCH_DIRECTORY}" search_directory_normalized)
string(TOLOWER "${search_directory_normalized}/" search_directory_prefix)
foreach(dependency IN LISTS resolved_dependencies)
    file(TO_CMAKE_PATH "${dependency}" dependency_normalized)
    string(TOLOWER "${dependency_normalized}" dependency_lower)
    string(FIND "${dependency_lower}" "${search_directory_prefix}" prefix_position)
    if(NOT prefix_position EQUAL 0)
        continue()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${dependency}" "${M2_DESTINATION}"
        RESULT_VARIABLE copy_result
    )
    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to deploy runtime dependency: ${dependency}")
    endif()
endforeach()

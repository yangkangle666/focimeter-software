cmake_minimum_required(VERSION 3.21)

project(focimeter_m2 LANGUAGES CXX)

option(M2_BUILD_TESTS "Build M2 automated tests" ON)
option(M2_BUILD_CLI "Build the M2 command-line program" ON)
option(M2_BUILD_TOOLS "Build M2 synthetic-data tools" ON)
option(BUILD_TESTING "Enable CTest test registration" ON)

set(M2_OPENCV_COMPONENTS core imgproc imgcodecs)
if(M2_BUILD_CLI)
    list(APPEND M2_OPENCV_COMPONENTS highgui)
endif()
find_package(OpenCV REQUIRED COMPONENTS ${M2_OPENCV_COMPONENTS})
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

function(m2_deploy_windows_runtime target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DM2_EXECUTABLE=$<TARGET_FILE:${target_name}>"
                "-DM2_DESTINATION=$<TARGET_FILE_DIR:${target_name}>"
                "-DM2_SEARCH_DIRECTORY=$<IF:$<CONFIG:Debug>,${OpenCV_INSTALL_PATH}/debug/bin,${OpenCV_INSTALL_PATH}/bin>"
                "-DM2_LOCK_FILE=${CMAKE_BINARY_DIR}/m2-runtime-deploy.lock"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_runtime.cmake"
            VERBATIM
        )
    endif()
endfunction()

add_library(focimeter_m2_core STATIC
    src/image_processor.cpp
    src/json_io.cpp
    src/multispot_detector.cpp
    src/module.cpp
    src/output_lock.cpp
    src/spot_matcher.cpp
)

target_compile_features(focimeter_m2_core PUBLIC cxx_std_17)
target_include_directories(focimeter_m2_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(focimeter_m2_core
    PUBLIC
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(focimeter_m2_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(focimeter_m2_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(M2_BUILD_CLI)
    add_executable(focimeter_m2 src/focimeter.cpp)
    target_link_libraries(focimeter_m2 PRIVATE focimeter_m2_core opencv_highgui)
    m2_deploy_windows_runtime(focimeter_m2)
    if(MSVC)
        target_compile_options(focimeter_m2 PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

if(M2_BUILD_TOOLS)
    add_executable(m2_generate_synthetic tools/generate_synthetic.cpp)
    target_link_libraries(m2_generate_synthetic PRIVATE opencv_core opencv_imgproc opencv_imgcodecs)
    target_compile_features(m2_generate_synthetic PRIVATE cxx_std_17)
    m2_deploy_windows_runtime(m2_generate_synthetic)
    if(MSVC)
        target_compile_options(m2_generate_synthetic PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(m2_generate_multispot_synthetic tools/generate_multispot_synthetic.cpp)
    target_link_libraries(m2_generate_multispot_synthetic
        PRIVATE opencv_core opencv_imgproc opencv_imgcodecs nlohmann_json::nlohmann_json)
    target_compile_features(m2_generate_multispot_synthetic PRIVATE cxx_std_17)
    m2_deploy_windows_runtime(m2_generate_multispot_synthetic)
    if(MSVC)
        target_compile_options(m2_generate_multispot_synthetic PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

if(M2_BUILD_TESTS AND BUILD_TESTING)
    enable_testing()
    get_filename_component(M2_SYSTEM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(M2_SYNTHETIC_ROOT "${M2_SYSTEM_ROOT}/data/mock/m2_image_recognition/synthetic")
    set(M2_MULTISPOT_SYNTHETIC_ROOT
        "${M2_SYSTEM_ROOT}/data/mock/m2_image_recognition/synthetic_multispot")
    set(M2_REAL_LENS_ROOT
        "${M2_SYSTEM_ROOT}/data/real/multispot_lens_pairs/real_lens_pair_set_001")
    set(M2_TEST_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/Testing/m2-$<CONFIG>")

    add_executable(m2_tests
        tests/test_main.cpp
    )
    target_link_libraries(m2_tests PRIVATE focimeter_m2_core nlohmann_json::nlohmann_json)
    target_compile_features(m2_tests PRIVATE cxx_std_17)
    m2_deploy_windows_runtime(m2_tests)
    if(MSVC)
        target_compile_options(m2_tests PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(m2_multispot_tests tests/test_multispot.cpp)
    target_link_libraries(m2_multispot_tests
        PRIVATE focimeter_m2_core nlohmann_json::nlohmann_json)
    target_compile_features(m2_multispot_tests PRIVATE cxx_std_17)
    m2_deploy_windows_runtime(m2_multispot_tests)
    if(MSVC)
        target_compile_options(m2_multispot_tests PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(m2_jpeg_tests tests/test_jpeg.cpp)
    target_link_libraries(m2_jpeg_tests
        PRIVATE focimeter_m2_core nlohmann_json::nlohmann_json)
    target_compile_features(m2_jpeg_tests PRIVATE cxx_std_17)
    m2_deploy_windows_runtime(m2_jpeg_tests)
    if(MSVC)
        target_compile_options(m2_jpeg_tests PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_test(
        NAME m2_unit_and_integration
        COMMAND m2_tests
            --temp-dir "${M2_TEST_OUTPUT_ROOT}/unit"
            --synthetic-root "${M2_SYNTHETIC_ROOT}"
    )
    set(M2_REGISTERED_TESTS m2_unit_and_integration)
    add_test(
        NAME m2_multispot_unit_and_integration
        COMMAND m2_multispot_tests
            --temp-dir "${M2_TEST_OUTPUT_ROOT}/multispot-unit"
            --synthetic-root "${M2_MULTISPOT_SYNTHETIC_ROOT}"
            --system-root "${M2_SYSTEM_ROOT}"
    )
    list(APPEND M2_REGISTERED_TESTS m2_multispot_unit_and_integration)
    add_test(
        NAME m2_jpeg_detection_and_determinism
        COMMAND m2_jpeg_tests
            --temp-dir "${M2_TEST_OUTPUT_ROOT}/jpeg"
            --synthetic-root "${M2_MULTISPOT_SYNTHETIC_ROOT}"
            --real-root "${M2_REAL_LENS_ROOT}"
    )
    list(APPEND M2_REGISTERED_TESTS m2_jpeg_detection_and_determinism)
    if(M2_BUILD_CLI)
        add_test(NAME m2_cli_help COMMAND focimeter_m2 --help)
        list(APPEND M2_REGISTERED_TESTS m2_cli_help)

        foreach(case_name IN ITEMS transform noise affine uneven)
            add_test(
                NAME m2_cli_synthetic_${case_name}
                COMMAND focimeter_m2
                    --input "${M2_SYNTHETIC_ROOT}/input_package_${case_name}.json"
                    --output "${M2_TEST_OUTPUT_ROOT}/five-spot-${case_name}"
                    --project-root "${M2_SYSTEM_ROOT}"
            )
            list(APPEND M2_REGISTERED_TESTS m2_cli_synthetic_${case_name})
        endforeach()
        add_test(
            NAME m2_cli_synthetic_missing
            COMMAND ${CMAKE_COMMAND}
                "-DM2_EXECUTABLE=$<TARGET_FILE:focimeter_m2>"
                "-DM2_INPUT=${M2_SYNTHETIC_ROOT}/input_package_missing.json"
                "-DM2_OUTPUT=${M2_TEST_OUTPUT_ROOT}/five-spot-missing"
                "-DM2_PROJECT_ROOT=${M2_SYSTEM_ROOT}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_cli_failure.cmake"
        )
        list(APPEND M2_REGISTERED_TESTS m2_cli_synthetic_missing)

        add_test(
            NAME m2_cli_multispot_25
            COMMAND focimeter_m2
                --input "${M2_MULTISPOT_SYNTHETIC_ROOT}/packages/input_package_25_clean_shift.json"
                --output "${M2_TEST_OUTPUT_ROOT}/multispot-25"
                --project-root "${M2_SYSTEM_ROOT}"
                --experimental-multispot
        )
        list(APPEND M2_REGISTERED_TESTS m2_cli_multispot_25)
        add_test(
            NAME m2_cli_multispot_16bit
            COMMAND focimeter_m2
                --input "${M2_MULTISPOT_SYNTHETIC_ROOT}/packages/input_package_25_12bit_shift.json"
                --output "${M2_TEST_OUTPUT_ROOT}/multispot-16bit"
                --project-root "${M2_SYSTEM_ROOT}"
                --experimental-multispot
                --experimental-16bit-white-level 4095
        )
        list(APPEND M2_REGISTERED_TESTS m2_cli_multispot_16bit)
        add_test(
            NAME m2_cli_multispot_missing
            COMMAND ${CMAKE_COMMAND}
                "-DM2_EXECUTABLE=$<TARGET_FILE:focimeter_m2>"
                "-DM2_INPUT=${M2_MULTISPOT_SYNTHETIC_ROOT}/packages/input_package_missing_11.json"
                "-DM2_OUTPUT=${M2_TEST_OUTPUT_ROOT}/multispot-missing"
                "-DM2_PROJECT_ROOT=${M2_SYSTEM_ROOT}"
                "-DM2_EXPECTED_TASK_ID=m2_multispot_missing_11"
                "-DM2_EXPECTED_CALIBRATION_ERROR_CODE=COORDINATE_SYSTEM_INVALID"
                "-DM2_EXPECTED_MEASUREMENT_ERROR_CODE=SPOT_COUNT_MISMATCH"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_multispot_cli_failure.cmake"
        )
        list(APPEND M2_REGISTERED_TESTS m2_cli_multispot_missing)

        foreach(lens_id IN ITEMS 001 002)
            if(lens_id STREQUAL "001")
                set(expected_measurement_warning "SUBPITCH_FRAGMENT_REJECTED_UNVERIFIED")
            else()
                set(expected_measurement_warning "EDGE_CLIPPED_CANDIDATE_REJECTED")
            endif()
            add_test(
                NAME m2_cli_real_jpeg_lens_${lens_id}
                COMMAND ${CMAKE_COMMAND}
                    "-DM2_EXECUTABLE=$<TARGET_FILE:focimeter_m2>"
                    "-DM2_INPUT=${M2_REAL_LENS_ROOT}/packages/input_package_lens_${lens_id}.json"
                    "-DM2_OUTPUT=${M2_TEST_OUTPUT_ROOT}/real-jpeg-lens-${lens_id}"
                    "-DM2_PROJECT_ROOT=${M2_SYSTEM_ROOT}"
                    "-DM2_EXPECTED_MEASUREMENT_WARNING=${expected_measurement_warning}"
                    "-DM2_EXPECTED_SPOT_COUNT=27"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_real_multispot_cli_success.cmake"
            )
            list(APPEND M2_REGISTERED_TESTS m2_cli_real_jpeg_lens_${lens_id})
        endforeach()
    endif()
    if(M2_BUILD_TOOLS)
        add_test(
            NAME m2_generate_synthetic_smoke
            COMMAND m2_generate_synthetic
                --output "${M2_TEST_OUTPUT_ROOT}/five-spot-generated"
        )
        list(APPEND M2_REGISTERED_TESTS m2_generate_synthetic_smoke)

        add_test(
            NAME m2_generate_multispot_reproducible
            COMMAND ${CMAKE_COMMAND}
                "-DM2_GENERATOR=$<TARGET_FILE:m2_generate_multispot_synthetic>"
                "-DM2_OUTPUT_ROOT=${M2_TEST_OUTPUT_ROOT}/multispot-generated"
                "-DM2_REFERENCE_ROOT=${M2_MULTISPOT_SYNTHETIC_ROOT}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_multispot_generator_reproducible.cmake"
        )
        list(APPEND M2_REGISTERED_TESTS m2_generate_multispot_reproducible)
    endif()
    set_tests_properties(${M2_REGISTERED_TESTS} PROPERTIES TIMEOUT 120)
    set_tests_properties(m2_unit_and_integration PROPERTIES TIMEOUT 90)
    set_tests_properties(m2_multispot_unit_and_integration PROPERTIES TIMEOUT 180)
    set_tests_properties(m2_jpeg_detection_and_determinism PROPERTIES TIMEOUT 180)
endif()

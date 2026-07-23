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
endif()

if(M2_BUILD_TESTS AND BUILD_TESTING)
    enable_testing()
    get_filename_component(M2_SYSTEM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(M2_SYNTHETIC_ROOT "${M2_SYSTEM_ROOT}/data/mock/m2_image_recognition/synthetic")

    add_executable(m2_tests
        tests/test_main.cpp
    )
    target_link_libraries(m2_tests PRIVATE focimeter_m2_core nlohmann_json::nlohmann_json)
    target_compile_features(m2_tests PRIVATE cxx_std_17)
    m2_deploy_windows_runtime(m2_tests)
    if(MSVC)
        target_compile_options(m2_tests PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_test(
        NAME m2_unit_and_integration
        COMMAND m2_tests
            --temp-dir $ENV{TEMP}/focimeter-m2-tests
            --synthetic-root "${M2_SYNTHETIC_ROOT}"
    )
    set(M2_REGISTERED_TESTS m2_unit_and_integration)
    if(M2_BUILD_CLI)
        add_test(NAME m2_cli_help COMMAND focimeter_m2 --help)
        list(APPEND M2_REGISTERED_TESTS m2_cli_help)

        foreach(case_name IN ITEMS transform noise affine)
            add_test(
                NAME m2_cli_synthetic_${case_name}
                COMMAND focimeter_m2
                    --input "${M2_SYNTHETIC_ROOT}/input_package_${case_name}.json"
                    --output "$ENV{TEMP}/focimeter-m2-cli-${case_name}"
                    --project-root "${M2_SYSTEM_ROOT}"
            )
            list(APPEND M2_REGISTERED_TESTS m2_cli_synthetic_${case_name})
        endforeach()
        add_test(
            NAME m2_cli_synthetic_missing
            COMMAND focimeter_m2
                --input "${M2_SYNTHETIC_ROOT}/input_package_missing.json"
                --output "$ENV{TEMP}/focimeter-m2-cli-missing"
                --project-root "${M2_SYSTEM_ROOT}"
        )
        set_tests_properties(m2_cli_synthetic_missing PROPERTIES WILL_FAIL TRUE)
        list(APPEND M2_REGISTERED_TESTS m2_cli_synthetic_missing)
    endif()
    if(M2_BUILD_TOOLS)
        add_test(
            NAME m2_generate_synthetic_smoke
            COMMAND m2_generate_synthetic
                --output "$ENV{TEMP}/focimeter-m2-generated"
        )
        list(APPEND M2_REGISTERED_TESTS m2_generate_synthetic_smoke)
    endif()
    set_tests_properties(${M2_REGISTERED_TESTS} PROPERTIES TIMEOUT 30)
endif()

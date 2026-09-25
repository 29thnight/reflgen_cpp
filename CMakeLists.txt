cmake_minimum_required(VERSION 3.25)

project(reflgen
    VERSION 0.1.0
    DESCRIPTION "Macro-free C++20 reflection and format-agnostic serialization"
    LANGUAGES CXX)

option(REFLGEN_BUILD_TESTS "Build the reflgen test suite" ${PROJECT_IS_TOP_LEVEL})
option(REFLGEN_BUILD_EXAMPLES "Build the reflgen examples" ${PROJECT_IS_TOP_LEVEL})
option(REFLGEN_BUILD_GENERATOR "Build the reflgen code generator (requires libclang)" ${PROJECT_IS_TOP_LEVEL})
set(REFLGEN_TEST_CXX_STANDARD 20 CACHE STRING "C++ standard used to build tests and examples (20, 23)")

# reflgen_generate() — [[reflgen::…]] attribute 에서 reflection 코드를 만든다.
include(cmake/reflgen.cmake)

# 헤더 전용 라이브러리. C++20 이 하한이다.
add_library(reflgen INTERFACE)
add_library(reflgen::reflgen ALIAS reflgen)
target_include_directories(reflgen INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(reflgen INTERFACE cxx_std_20)

# 헤더의 주석이 한글(UTF-8)이다. MSVC 는 BOM 없는 원본을 시스템 코드 페이지로 읽으므로
# 소비자에게까지 /utf-8 을 전파한다(헤더 자체에도 BOM 을 둔다 — README 참고).
target_compile_options(reflgen INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<AND:$<CXX_COMPILER_ID:Clang>,$<STREQUAL:${CMAKE_CXX_SIMULATE_ID},MSVC>>:/utf-8>)

function(reflgen_configure_target target)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD ${REFLGEN_TEST_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHsc /bigobj)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    # clang 21 까지의 기본 식 중첩 한도(256)를 새 clang 에서도 지킨다 — 큰 fold 식이 새 clang 에서만 통과하는 것을
    # 로컬에서 잡으려는 것이다(CI 의 clang 20 에서 깨진 적이 있다).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE
            "$<IF:$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>,/clang:-fbracket-depth=256,-fbracket-depth=256>")
    endif()
endfunction()

if(REFLGEN_BUILD_GENERATOR)
    add_subdirectory(generator)
endif()

if(REFLGEN_BUILD_TESTS)
    enable_testing()
    set(REFLGEN_TEST_SOURCES
        tests/harness.cpp
        tests/static_string_tests.cpp
        tests/annotation_readiness_tests.cpp
        tests/name_tests.cpp
        tests/enum_tests.cpp
        tests/schema_tests.cpp
        tests/runtime_tests.cpp
        tests/scalar_tests.cpp
        tests/string_tests.cpp
        tests/container_tests.cpp
        tests/custom_container_tests.cpp
        tests/utility_tests.cpp
        tests/object_tests.cpp
        tests/pointer_tests.cpp
        tests/cycle_tests.cpp
        tests/json_tests.cpp
        tests/binary_tests.cpp)
    # C++23 전용 표준 타입(std::expected 등)은 23 빌드에서만 시험한다.
    if(REFLGEN_TEST_CXX_STANDARD GREATER_EQUAL 23)
        list(APPEND REFLGEN_TEST_SOURCES tests/cpp23_tests.cpp)
    endif()
    if(MSVC)
        list(APPEND REFLGEN_TEST_SOURCES tests/msvc_crt.cpp)
    endif()
    add_executable(reflgen_tests ${REFLGEN_TEST_SOURCES})
    target_link_libraries(reflgen_tests PRIVATE reflgen::reflgen)
    reflgen_configure_target(reflgen_tests)
    add_test(NAME reflgen_tests COMMAND reflgen_tests)

    # 생성기 시험 — libclang 이 있어 생성기가 빌드될 때만.
    if(TARGET reflgen_cli)
        set(reflgen_harness_sources tests/harness.cpp)
        if(MSVC)
            list(APPEND reflgen_harness_sources tests/msvc_crt.cpp)
        endif()

        add_executable(reflgen_generator_tests generator/tests/attribute_scan_tests.cpp ${reflgen_harness_sources})
        target_include_directories(reflgen_generator_tests PRIVATE tests)
        target_link_libraries(reflgen_generator_tests PRIVATE reflgen_generator_core reflgen::reflgen)
        reflgen_configure_target(reflgen_generator_tests)
        add_test(NAME reflgen_generator_tests COMMAND reflgen_generator_tests)

        # end-to-end: attribute 만 단 header → 생성 → 컴파일 → 실행.
        add_executable(reflgen_generated_tests tests/generated/generated_tests.cpp ${reflgen_harness_sources})
        target_include_directories(reflgen_generated_tests PRIVATE tests tests/generated)
        target_link_libraries(reflgen_generated_tests PRIVATE reflgen::reflgen)
        reflgen_configure_target(reflgen_generated_tests)
        reflgen_generate(reflgen_generated_tests
            MODULE generated_tests
            HEADERS tests/generated/game_types.h
            ATTRIBUTE_SCOPES generated_tests)
        add_test(NAME reflgen_generated_tests COMMAND reflgen_generated_tests)

        # 잘못 쓴 header 에 대한 진단(코드·위치·실패 종료).
        add_test(NAME reflgen_generator_diagnostics
            COMMAND "${CMAKE_COMMAND}"
                "-DGENERATOR=$<TARGET_FILE:reflgen_cli>"
                "-DHEADER=${PROJECT_SOURCE_DIR}/tests/generated/bad_types.h"
                "-DINCLUDE=${PROJECT_SOURCE_DIR}/include"
                "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/reflgen/diagnostics"
                -P "${PROJECT_SOURCE_DIR}/tests/generated/check_diagnostics.cmake")

        # 편집기 자동완성용 attribute 카탈로그.
        add_test(NAME reflgen_generator_catalog
            COMMAND "${CMAKE_COMMAND}"
                "-DCATALOG=${CMAKE_CURRENT_BINARY_DIR}/reflgen/reflgen_generated_tests/$<CONFIG>/reflgen_generated_tests.attributes.tsv"
                -P "${PROJECT_SOURCE_DIR}/tests/generated/check_catalog.cmake")

        # 손으로 쓴 응답 파일 — UTF-8 BOM 과 따옴표 친 경로를 받아야 한다(메모장·PowerShell 이 BOM 을 붙인다).
        add_test(NAME reflgen_generator_response_file
            COMMAND reflgen_cli --output "${CMAKE_CURRENT_BINARY_DIR}/reflgen/response_file"
                    "@tests/generated/response_with_bom.rsp"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

        # 프로젝트의 header 전부를 후보로 받았을 때(--discover, MSBuild 가 쓴다).
        add_test(NAME reflgen_generator_discover
            COMMAND "${CMAKE_COMMAND}"
                "-DGENERATOR=$<TARGET_FILE:reflgen_cli>"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/reflgen/discover"
                -P "${PROJECT_SOURCE_DIR}/tests/generated/check_discover.cmake")

        # 한글·공백이 든 경로.
        add_test(NAME reflgen_generator_unicode_paths
            COMMAND "${CMAKE_COMMAND}"
                "-DGENERATOR=$<TARGET_FILE:reflgen_cli>"
                "-DHEADER=${PROJECT_SOURCE_DIR}/tests/generated/game_types.h"
                "-DINCLUDE=${PROJECT_SOURCE_DIR}/include"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/reflgen/unicode"
                -P "${PROJECT_SOURCE_DIR}/tests/generated/check_unicode_paths.cmake")

        # MSBuild 연동 — msbuild/reflgen.targets 를 가져온 .vcxproj 가 header 를 찾아 생성·주입·컴파일하고
        # 최신 상태를 지키는가(미리 컴파일된 header 를 쓰는 프로젝트).
        find_program(REFLGEN_MSBUILD_EXECUTABLE NAMES MSBuild msbuild)
        if(WIN32 AND REFLGEN_MSBUILD_EXECUTABLE)
            add_test(NAME reflgen_msbuild_integration
                COMMAND "${CMAKE_COMMAND}"
                    "-DMSBUILD=${REFLGEN_MSBUILD_EXECUTABLE}"
                    "-DGENERATOR=$<TARGET_FILE:reflgen_cli>"
                    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/msbuild_test"
                    -P "${PROJECT_SOURCE_DIR}/tests/msbuild/run_msbuild_test.cmake")
        endif()
    endif()
endif()

if(REFLGEN_BUILD_EXAMPLES)
    add_executable(reflgen_quick_start examples/quick_start.cpp)
    target_link_libraries(reflgen_quick_start PRIVATE reflgen::reflgen)
    reflgen_configure_target(reflgen_quick_start)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS reflgen EXPORT reflgen-targets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES cmake/reflgen.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reflgen)
# .vcxproj 용 — <prefix>/share/reflgen/msbuild/reflgen.targets 가 <prefix>/include 와 <prefix>/bin 을 찾는다.
install(FILES msbuild/reflgen.targets DESTINATION ${CMAKE_INSTALL_DATADIR}/reflgen/msbuild)
if(TARGET reflgen_cli)
    install(TARGETS reflgen_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(REFLGEN_LIBCLANG_DLL)
        install(FILES "${REFLGEN_LIBCLANG_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
install(EXPORT reflgen-targets
    NAMESPACE reflgen::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reflgen)
configure_package_config_file(cmake/reflgen-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/reflgen-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reflgen)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/reflgen-config-version.cmake
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/reflgen-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/reflgen-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/reflgen)

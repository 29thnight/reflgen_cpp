cmake_minimum_required(VERSION 3.25)

project(reflgen
    VERSION 0.1.0
    DESCRIPTION "Macro-free C++20 reflection and format-agnostic serialization"
    LANGUAGES CXX)

option(REFLGEN_BUILD_TESTS "Build the reflgen test suite" ${PROJECT_IS_TOP_LEVEL})
option(REFLGEN_BUILD_EXAMPLES "Build the reflgen examples" ${PROJECT_IS_TOP_LEVEL})
set(REFLGEN_TEST_CXX_STANDARD 20 CACHE STRING "C++ standard used to build tests and examples (20, 23)")

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
endfunction()

if(REFLGEN_BUILD_TESTS)
    enable_testing()
    set(REFLGEN_TEST_SOURCES
        tests/harness.cpp
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

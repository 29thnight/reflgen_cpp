# 판이 한 곳(include/reflgen/core/version.h)에서만 나오는지 — CMake project(VERSION), 생성기 --version,
# VSIX 매니페스트가 모두 같은 값을 말해야 한다.
#
#   cmake -DGENERATOR=<reflgen.exe> -DPROJECT_VERSION=<x.y.z> -DMANIFEST=<source.extension.vsixmanifest>
#         -DHEADER=<version.h> -P check_version.cmake
foreach(name GENERATOR PROJECT_VERSION MANIFEST HEADER)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "check_version: ${name} is not set")
    endif()
endforeach()

file(STRINGS "${HEADER}" header_string REGEX "version_string = \"[0-9]+\\.[0-9]+\\.[0-9]+\"")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" header_version "${header_string}")
file(STRINGS "${HEADER}" format_line REGEX "generated_code_format = [0-9]+")
string(REGEX MATCH "[0-9]+" header_format "${format_line}")
if(NOT header_version STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "version.h says '${header_version}' but CMake project(VERSION) is '${PROJECT_VERSION}'")
endif()

execute_process(COMMAND "${GENERATOR}" --version OUTPUT_VARIABLE generator_output RESULT_VARIABLE generator_result)
set(expected_output "reflgen ${header_version} (generated code format ${header_format})")
string(STRIP "${generator_output}" generator_output)
if(NOT generator_result EQUAL 0 OR NOT generator_output STREQUAL expected_output)
    message(FATAL_ERROR "reflgen --version printed '${generator_output}' (exit ${generator_result}); expected '${expected_output}'")
endif()

file(READ "${MANIFEST}" manifest)
string(REGEX MATCH "<Identity [^>]*Version=\"([0-9.]+)\"" identity "${manifest}")
if(NOT CMAKE_MATCH_1 STREQUAL header_version)
    message(FATAL_ERROR "the VSIX manifest version is '${CMAKE_MATCH_1}' but version.h says '${header_version}'")
endif()
message(STATUS "version ${header_version}, generated code format ${header_format}: consistent")

# 생성기 시험 — clang 오류를 반영 선언과 가른다. MSVC 로만 빌드하는 코드는 clang 으로 읽으면 반영과 무관한 곳에서
# 오류를 낸다. 그것 때문에 생성을 멈추면 그런 코드베이스에서는 생성기를 쓸 수 없다.
#   1) tolerated_errors.h — 반영 선언 밖 오류 21 개(clang 의 기본 한도 20 을 넘는다): 성공하고, 오류 뒤의 survivor 를
#      생성하고, 넘긴 오류 수를 알린다. clang 의 내장 header 를 못 찾으면 오류가 하나 더 생겨 수가 달라진다.
#   2) reflected_errors.h — 반영 선언 안의 오류, 밖의 오류(부모)로 무효가 된 반영 선언: 실패하고 그 자리를 가리키며,
#      넘겼을 오류도 모두 알린다(원인이 거기 있다).
#
#   cmake -DGENERATOR=<reflgen> -DSOURCE=<tests/generated> -DINCLUDE=<include dir> -DOUTPUT=<dir> -P check_tolerance.cmake

file(REMOVE_RECURSE "${OUTPUT}")

# MSBuild 연동은 $(IncludePath)(MSVC·Windows SDK include)를 -isystem 으로 준다 — 개발자 명령 프롬프트면 같은 조건으로
# 시험한다. clang 은 -isystem 을 내장 header 보다 먼저 찾으므로 MSVC 의 xmmintrin.h 가 이기면 안 된다.
set(msvc_include "")
if(DEFINED ENV{VCToolsInstallDir})
    set(msvc_include "-isystem" "$ENV{VCToolsInstallDir}include")
endif()

execute_process(
    COMMAND "${GENERATOR}" --module tolerated --output "${OUTPUT}/tolerated" "${SOURCE}/tolerated_errors.h" --
            "-I${INCLUDE}" ${msvc_include}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "errors outside the reflected declarations stopped the generator:\n${output}")
endif()
if(NOT output MATCHES "note RG0101: 21 clang errors outside the reflected declarations")
    message(FATAL_ERROR "the generator did not report exactly 21 ignored clang errors:\n${output}")
endif()
file(READ "${OUTPUT}/tolerated/tolerated_errors.reflgen.h" generated)
if(NOT generated MATCHES "survivor" OR NOT generated MATCHES "lanes")
    message(FATAL_ERROR "the declaration after the errors was not generated:\n${generated}")
endif()

execute_process(
    COMMAND "${GENERATOR}" --module reflected --output "${OUTPUT}/reflected" "${SOURCE}/reflected_errors.h" --
            "-I${INCLUDE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(result EQUAL 0)
    message(FATAL_ERROR "an error inside a reflected declaration did not stop the generator:\n${output}")
endif()
if(NOT output MATCHES "reflected_errors\\.h\\(11,9\\): error RG0100: clang: unknown type name 'undeclared_type'")
    message(FATAL_ERROR "the error inside the reflected declaration was not reported at its place:\n${output}")
endif()
# 밖의 오류로 무효가 된 반영 선언도 실패다 — 그리고 실패할 때는 넘긴 오류도 모두 알려 원인을 찾게 한다.
if(NOT output MATCHES "reflected_errors\\.h\\(20,33\\): error RG0102: .*derived")
    message(FATAL_ERROR "a reflected declaration made invalid by an error elsewhere was not reported:\n${output}")
endif()
if(NOT output MATCHES "reflected_errors\\.h\\(17,9\\): note RG0101: clang: unknown type name 'another_undeclared_type'")
    message(FATAL_ERROR "the clang error behind the invalid declaration was not listed:\n${output}")
endif()
message(STATUS "clang errors: ignored outside the reflected declarations, reported inside them")

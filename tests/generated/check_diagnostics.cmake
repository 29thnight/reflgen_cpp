# 생성기 진단 시험 — bad_types.h 를 생성기에 주고, 기대한 진단이 모두 원본 위치와 함께
# 나오는지, 오류가 있으면 실패 코드로 끝나는지 본다.
#
#   cmake -DGENERATOR=<reflgen> -DHEADER=<bad_types.h> -DINCLUDE=<include dir> -DOUTPUT=<dir> -P check_diagnostics.cmake

execute_process(
    COMMAND "${GENERATOR}" --module bad --output "${OUTPUT}" "${HEADER}" -- "-I${INCLUDE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)

if(result EQUAL 0)
    message(FATAL_ERROR "the generator succeeded on a header with errors:\n${output}")
endif()

# 줄 번호까지 확인한다 — VS Error List 가 원본으로 이동하는 근거다.
set(expected
    "bad_types\\.h\\(9,28\\): error RG0002: .*secret_.*friend struct reflgen::access"
    "bad_types\\.h\\(16,9\\): warning RG0004: bit-field 'bits'"
    "bad_types\\.h\\(17,10\\): warning RG0004: reference member 'reference'"
    "bad_types\\.h\\(20,31\\): error RG0006: 'fire' is overloaded"
    "bad_types\\.h\\(26,29\\): warning RG0005: class templates"
    "bad_types\\.h\\(34,29\\): warning RG0005: .*anonymous namespace"
    "bad_types\\.h\\(41,29\\): error RG0002: .*outer_secret"
    "bad_types\\.h\\(53,28\\): warning RG0005: unions are not supported"
    "bad_types\\.h\\(60,26\\): warning RG0004: constructors and destructors"
    "bad_types\\.h\\(63,31\\): warning RG0005: member function template 'accept'"
    "bad_types\\.h\\(65,47\\): warning RG0004: static data member 'limit'"
    "bad_types\\.h\\(1,1\\): warning RG0003: .*bad_types\\.reflgen\\.h")
foreach(pattern IN LISTS expected)
    if(NOT output MATCHES "${pattern}")
        message(FATAL_ERROR "missing diagnostic matching '${pattern}' in:\n${output}")
    endif()
endforeach()

# ignore 가 붙은 bit-field 는 진단하지 않는다.
if(output MATCHES "ignored_bits")
    message(FATAL_ERROR "an ignored member was diagnosed:\n${output}")
endif()
message(STATUS "diagnostics as expected:\n${output}")

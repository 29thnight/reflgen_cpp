# 비ASCII 경로 시험 — 한글과 공백이 든 디렉터리의 header 를 생성기에 주면 생성되고, 생성 파일이 그 경로를
# 그대로 가리키는가(#line·#include).
#
#   cmake -DGENERATOR=<reflgen> -DHEADER=<game_types.h> -DINCLUDE=<include dir> -DWORK=<dir> -P check_unicode_paths.cmake

set(directory "${WORK}/한글 경로")
file(REMOVE_RECURSE "${directory}")
file(MAKE_DIRECTORY "${directory}")
file(COPY "${HEADER}" DESTINATION "${directory}")
get_filename_component(header_name "${HEADER}" NAME)
get_filename_component(stem "${HEADER}" NAME_WLE)

execute_process(
    COMMAND "${GENERATOR}" --module unicode --output "${directory}/out" --attribute-scope generated_tests
            "${directory}/${header_name}" -- "-I${INCLUDE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the generator failed on a non-ASCII path:\n${output}")
endif()

set(generated "${directory}/out/${stem}.reflgen.h")
if(NOT EXISTS "${generated}")
    message(FATAL_ERROR "${generated} was not written")
endif()
file(READ "${generated}" text)
string(FIND "${text}" "한글 경로/${header_name}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the generated header does not refer to the original path:\n${text}")
endif()
message(STATUS "non-ASCII paths: ok")

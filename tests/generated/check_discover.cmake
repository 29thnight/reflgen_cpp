# --discover 시험 — 후보 header 가운데 [[reflgen::reflect]] 가 있는 것만 생성하고, 반영을 지운 header 의 옛
# 생성 파일은 지우되, 출력 디렉터리에 있는 다른 module 의 생성 파일은 건드리지 않는가.
#
#   cmake -DGENERATOR=<reflgen> -DWORK=<dir> -P check_discover.cmake

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/out")
file(WRITE "${WORK}/point.h" "#pragma once\nstruct [[reflgen::reflect]] point\n{\n    int x = 0;\n};\n")
file(WRITE "${WORK}/plain.h" "#pragma once\nstruct plain\n{\n    int x = 0;\n};\n")
# 같은 출력 디렉터리를 쓰는 다른 module 의 생성 파일.
file(WRITE "${WORK}/out/foreign.reflgen.h" "// another module\n")

function(run_generator)
    execute_process(
        COMMAND "${GENERATOR}" --module discover --output "${WORK}/out" --discover "${WORK}/point.h" "${WORK}/plain.h"
                "${WORK}/missing.h"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the generator failed:\n${output}")
    endif()
endfunction()

run_generator()
if(NOT EXISTS "${WORK}/out/point.reflgen.h")
    message(FATAL_ERROR "point.h declares reflection but was not generated")
endif()
if(EXISTS "${WORK}/out/plain.reflgen.h")
    message(FATAL_ERROR "plain.h does not declare reflection but was generated")
endif()
file(READ "${WORK}/out/reflgen_discover.h" injection)
if(NOT injection MATCHES "point\\.reflgen\\.h")
    message(FATAL_ERROR "the injection header does not include point.reflgen.h:\n${injection}")
endif()

# 반영을 지우면 옛 생성 파일이 사라지고 주입 header 에서도 빠진다.
file(WRITE "${WORK}/point.h" "#pragma once\nstruct point\n{\n    int x = 0;\n};\n")
run_generator()
if(EXISTS "${WORK}/out/point.reflgen.h")
    message(FATAL_ERROR "point.reflgen.h was left behind after point.h stopped declaring reflection")
endif()
file(READ "${WORK}/out/reflgen_discover.h" injection)
if(injection MATCHES "point\\.reflgen\\.h")
    message(FATAL_ERROR "the injection header still includes point.reflgen.h:\n${injection}")
endif()
if(NOT EXISTS "${WORK}/out/foreign.reflgen.h")
    message(FATAL_ERROR "a generated file of another module was deleted")
endif()
message(STATUS "discover: ok")

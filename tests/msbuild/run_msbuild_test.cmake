# MSBuild 연동 시험 — reflgen_msbuild_test.vcxproj 를 빌드해 실행하고, 바뀐 것 없이 다시 빌드하면
# 생성기가 돌지 않는지 본다.
#
#   cmake -DMSBUILD=<MSBuild.exe> -DGENERATOR=<reflgen.exe> -DWORK=<dir> -P run_msbuild_test.cmake

set(project "${CMAKE_CURRENT_LIST_DIR}/reflgen_msbuild_test.vcxproj")
# 경로는 '/' 로 넘긴다 — "…\bin\" 처럼 끝이 역슬래시면 명령줄에서 닫는 따옴표가 먹힌다.
set(build_command "${MSBUILD}" "${project}" /nologo /v:minimal /p:Configuration=Debug /p:Platform=x64
    "/p:ReflgenExecutable=${GENERATOR}" "/p:OutDir=${WORK}/bin/" "/p:IntDir=${WORK}/obj/")

# 앞선 시험이 남긴 결과가 있어도 첫 빌드는 반드시 생성하게 한다.
file(REMOVE "${WORK}/obj/reflgen/reflgen_msbuild_test.stamp")
execute_process(COMMAND ${build_command} RESULT_VARIABLE result OUTPUT_VARIABLE first_output ERROR_VARIABLE first_output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the first build failed:\n${first_output}")
endif()

execute_process(COMMAND "${WORK}/bin/reflgen_msbuild_test.exe" RESULT_VARIABLE result OUTPUT_VARIABLE output
                ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the built program failed:\n${output}")
endif()
message(STATUS "${output}")

# 등록 없이 찾았는가 — [[reflgen::reflect]] 가 있는 header 만 생성되고, 나머지(plain.h, pch.h)는 건너뛴다.
if(NOT EXISTS "${WORK}/obj/reflgen/game_types.reflgen.h")
    message(FATAL_ERROR "game_types.h was not discovered:\n${first_output}")
endif()
foreach(skipped IN ITEMS plain pch)
    if(EXISTS "${WORK}/obj/reflgen/${skipped}.reflgen.h")
        message(FATAL_ERROR "${skipped}.h does not declare reflection but was generated")
    endif()
endforeach()

# 다시 빌드하면 생성기가 돌지 않아야 한다. 생성할 때만 찍히는 reflgen 메시지로 가린다(MSBuild 자체의
# "target 을 건너뜀" 메시지는 지역화된다).
if(NOT first_output MATCHES "reflgen: generating reflection")
    message(FATAL_ERROR "the first build did not report generating:\n${first_output}")
endif()
execute_process(COMMAND ${build_command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the second build failed:\n${output}")
endif()
if(output MATCHES "reflgen: generating reflection")
    message(FATAL_ERROR "the generator ran again although nothing changed:\n${output}")
endif()
message(STATUS "msbuild integration: generated, compiled, ran, and stayed up to date")

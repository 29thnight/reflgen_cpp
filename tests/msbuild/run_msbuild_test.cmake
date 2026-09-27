# MSBuild 연동 시험 — reflgen_msbuild_test.vcxproj(와 그것이 참조하는 reflgen_msbuild_lib·reflgen_msbuild_peer)를
# 빌드해 실행하고, 바뀐 것 없이 다시 빌드하면 생성기가 돌지 않는지 본다. 두 라이브러리는 서로를 ReflgenReference 로
# 잇는다 — 병렬 빌드(/m)에서 교착 없이 끝나고, 생성이 프로젝트마다 한 번만 도는지도 본다. 같은 세 프로젝트를
# 솔루션(reflgen_msbuild.sln)으로도 빌드한다 — 솔루션 빌드는 프로젝트마다 전역 속성을 다르게 준다.
#
#   cmake -DMSBUILD=<MSBuild.exe> -DGENERATOR=<reflgen.exe> -DWORK=<dir> -P run_msbuild_test.cmake

# 한 번 빌드해 실행한다. 모듈마다 생성이 정확히 한 번 돌아야 한다 — ReflgenReference 가 전역 속성을 어긋나게 주면
# MSBuild 가 같은 프로젝트를 다른 구성으로 한 번 더 평가해 두 번(동시에) 생성한다. 빌드 출력은 output_variable 에 둔다.
function(build_and_run build_file work output_variable)
    # 경로는 '/' 로 넘긴다 — "…\bin\" 처럼 끝이 역슬래시면 명령줄에서 닫는 따옴표가 먹힌다.
    # IntDir 는 전역 속성으로 주지 않는다 — 참조하는 프로젝트까지 같은 IntDir 를 쓰게 된다. 세 프로젝트가 작업
    # 디렉터리(ReflgenTestWork) 아래에 자기 이름으로 나눠 둔다.
    execute_process(COMMAND "${MSBUILD}" "${build_file}" /nologo /v:minimal /m /p:Configuration=Debug /p:Platform=x64
                            "/p:ReflgenExecutable=${GENERATOR}" "/p:ReflgenTestWork=${work}/"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "building ${build_file} failed:\n${output}")
    endif()
    foreach(module IN ITEMS msbuild_test msbuild_lib msbuild_peer)
        string(REGEX MATCHALL "reflgen: generating reflection for [A-Za-z_]+ \\(${module}\\)" runs "${output}")
        list(LENGTH runs run_count)
        if(NOT run_count EQUAL 1)
            message(FATAL_ERROR "${module} was generated ${run_count} times in one build of ${build_file}:\n${output}")
        endif()
    endforeach()

    execute_process(COMMAND "${work}/bin/reflgen_msbuild_test.exe" RESULT_VARIABLE result OUTPUT_VARIABLE program
                    ERROR_VARIABLE program)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the program built from ${build_file} failed:\n${program}")
    endif()
    message(STATUS "${program}")
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

if(NOT MSBUILD OR NOT GENERATOR OR NOT WORK)
    message(FATAL_ERROR "pass -DMSBUILD=<MSBuild.exe> -DGENERATOR=<reflgen.exe> -DWORK=<dir>")
endif()

set(project "${CMAKE_CURRENT_LIST_DIR}/reflgen_msbuild_test.vcxproj")
set(app_output "${WORK}/obj/reflgen_msbuild_test/reflgen")
set(lib_output "${WORK}/obj/reflgen_msbuild_lib/reflgen")
set(peer_output "${WORK}/obj/reflgen_msbuild_peer/reflgen")

# 앞선 시험이 남긴 결과가 있어도 첫 빌드는 반드시 생성하게 한다.
file(REMOVE_RECURSE "${WORK}")
build_and_run("${project}" "${WORK}" first_output)

# 등록 없이 찾았는가 — [[reflgen::reflect]] 가 있는 header 만 생성되고, 나머지(plain.h, pch.h)는 건너뛴다.
if(NOT EXISTS "${app_output}/game_types.reflgen.h" OR NOT EXISTS "${lib_output}/lib_types.reflgen.h"
   OR NOT EXISTS "${peer_output}/peer_types.reflgen.h")
    message(FATAL_ERROR "game_types.h, lib_types.h or peer_types.h was not discovered:\n${first_output}")
endif()
foreach(skipped IN ITEMS plain pch)
    if(EXISTS "${app_output}/${skipped}.reflgen.h")
        message(FATAL_ERROR "${skipped}.h does not declare reflection but was generated")
    endif()
endforeach()

# 다시 빌드하면 생성기가 돌지 않아야 한다. 생성할 때만 찍히는 reflgen 메시지로 가린다(MSBuild 자체의
# "target 을 건너뜀" 메시지는 지역화된다).
execute_process(COMMAND "${MSBUILD}" "${project}" /nologo /v:minimal /m /p:Configuration=Debug /p:Platform=x64
                        "/p:ReflgenExecutable=${GENERATOR}" "/p:ReflgenTestWork=${WORK}/"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the second build failed:\n${output}")
endif()
if(output MATCHES "reflgen: generating reflection")
    message(FATAL_ERROR "the generator ran again although nothing changed:\n${output}")
endif()

# 솔루션 빌드 — 작업 디렉터리를 따로 둔다(처음부터 생성하게).
set(solution_work "${WORK}/solution")
build_and_run("${CMAKE_CURRENT_LIST_DIR}/reflgen_msbuild.sln" "${solution_work}" solution_output)

message(STATUS "msbuild integration: generated once per project, compiled, ran, and stayed up to date")

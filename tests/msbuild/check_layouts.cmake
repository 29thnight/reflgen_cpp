# reflgen.targets 가 배포 배치마다 생성기와 include 디렉터리를 스스로 찾는지 — 사용자가 경로를 적지 않아도 되게.
#
#   install  <prefix>\share\reflgen\msbuild\reflgen.targets + <prefix>\bin\reflgen.exe            (cmake --install)
#   vcpkg    <installed>\share\reflgen\msbuild\reflgen.targets + <installed>\tools\reflgen\reflgen.exe
#   nuget    <package>\build\native\reflgen.targets(얇은 shim) → 설치 배치를 그대로 담은 패키지
#
#   cmake -DMSBUILD=<MSBuild.exe> -DSOURCE=<저장소> -DWORK=<작업 폴더> -P check_layouts.cmake
foreach(name MSBUILD SOURCE WORK)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "check_layouts: ${name} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")

# 배치 하나를 흉내 낸다: targets·include 표지·생성기 자리(빈 파일이면 된다 — 존재만 본다).
function(make_layout root targets_dir executable)
    file(COPY "${SOURCE}/msbuild/reflgen.targets" DESTINATION "${root}/${targets_dir}")
    file(WRITE "${root}/include/reflgen/reflgen.h" "")
    file(WRITE "${root}/${executable}" "")
endfunction()

make_layout("${WORK}/install" "share/reflgen/msbuild" "bin/reflgen.exe")
make_layout("${WORK}/vcpkg" "share/reflgen/msbuild" "tools/reflgen/reflgen.exe")
make_layout("${WORK}/nuget" "share/reflgen/msbuild" "bin/reflgen.exe")
file(COPY "${SOURCE}/msbuild/nuget/reflgen.targets" DESTINATION "${WORK}/nuget/build/native")

# 평가만 하는 최소 프로젝트 — C++ 도구 집합 없이 속성을 읽는다.
function(check_layout name import expected_executable)
    set(project "${WORK}/${name}-probe/probe.proj")
    file(WRITE "${project}"
        "<Project>\n"
        "  <PropertyGroup><ProjectName>probe</ProjectName><IntDir>obj\\</IntDir></PropertyGroup>\n"
        "  <Import Project=\"${import}\" />\n"
        "</Project>\n")
    execute_process(
        COMMAND "${MSBUILD}" "${project}" -nologo -getProperty:ReflgenExecutable -getProperty:ReflgenIncludeDirectory
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${name}: msbuild failed (${result}):\n${output}\n${error}")
    endif()
    # JSON 이라 경로의 역슬래시가 두 겹이다. 정규식이 아니라 문자열로 비교한다(경로에 특수 문자가 올 수 있다).
    string(REPLACE "\\\\" "/" output "${output}")
    string(REPLACE "\\" "/" output "${output}")
    string(TOLOWER "${output}" output_lower)
    string(TOLOWER "\"reflgenexecutable\": \"${WORK}/${name}/${expected_executable}\"" executable_entry)
    string(TOLOWER "\"reflgenincludedirectory\": \"${WORK}/${name}/include/\"" include_entry)
    string(FIND "${output_lower}" "${executable_entry}" executable_at)
    string(FIND "${output_lower}" "${include_entry}" include_at)
    if(executable_at EQUAL -1)
        message(FATAL_ERROR "${name}: ReflgenExecutable is not ${WORK}/${name}/${expected_executable}:\n${output}")
    endif()
    if(include_at EQUAL -1)
        message(FATAL_ERROR "${name}: ReflgenIncludeDirectory is not ${WORK}/${name}/include/:\n${output}")
    endif()
    message(STATUS "${name}: generator and include directory found")
endfunction()

check_layout(install "${WORK}/install/share/reflgen/msbuild/reflgen.targets" "bin/reflgen.exe")
check_layout(vcpkg "${WORK}/vcpkg/share/reflgen/msbuild/reflgen.targets" "tools/reflgen/reflgen.exe")
check_layout(nuget "${WORK}/nuget/build/native/reflgen.targets" "bin/reflgen.exe")

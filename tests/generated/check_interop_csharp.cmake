# 생성된 C# P/Invoke 선언을 컴파일하고 native 라이브러리를 실제로 부른다.
# cmake -DDOTNET=<dotnet> -DPROJECT=<csproj> -DSOURCE=<generated .cs> -DLIBRARY=<native dll> -DWORK=<dir>
#       -P check_interop_csharp.cmake
cmake_minimum_required(VERSION 3.25)

# 소스 트리에 obj/bin 을 남기지 않게 모든 산출물을 빌드 디렉터리 아래에 둔다.
file(REMOVE_RECURSE "${WORK}")
execute_process(
    COMMAND "${DOTNET}" build "${PROJECT}" --nologo -c Release --artifacts-path "${WORK}"
            "-p:ReflgenInteropSource=${SOURCE}" "-p:ReflgenNativeLibrary=${LIBRARY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the generated C# bindings did not compile:\n${output}")
endif()

file(GLOB_RECURSE assemblies "${WORK}/bin/InteropRuntime.dll")
list(LENGTH assemblies assembly_count)
if(NOT assembly_count EQUAL 1)
    message(FATAL_ERROR "expected one InteropRuntime.dll under ${WORK}/bin, found: ${assemblies}")
endif()
execute_process(
    COMMAND "${DOTNET}" ${assemblies}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
message(STATUS "${output}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the C# P/Invoke calls failed (${result})")
endif()

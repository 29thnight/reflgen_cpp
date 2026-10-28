cmake_minimum_required(VERSION 3.25)
foreach(name MSBUILD GENERATOR SOURCE WORK)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "check_fsharp_table: ${name} is not set")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/api.h" [=[
#pragma once
inline constexpr unsigned ApiVersion = 46;
struct Api { int version, structSize; int (*Add)(int, int); };
using Binding [[reflgen::table("ScriptApi", "version", "structSize", ApiVersion)]] = Api;
]=])
file(WRITE "${WORK}/main.cpp" "#include \"api.h\"\nint fixture() { return ApiVersion; }\n")
set(project_text [=[
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals"><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion></PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration"><ConfigurationType>StaticLibrary</ConfigurationType><PlatformToolset>$(DefaultPlatformToolset)</PlatformToolset></PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <IntDir>$(ProjectDir)obj\</IntDir><OutDir>$(ProjectDir)bin\</OutDir>
    <ReflgenModule>native_consumer</ReflgenModule><ReflgenRegistration>false</ReflgenRegistration>
    <ReflgenFSharpTable>true</ReflgenFSharpTable><ReflgenFSharpNamespace>Tests.MSBuild</ReflgenFSharpNamespace>
    <ReflgenExecutable>@GENERATOR@</ReflgenExecutable>
  </PropertyGroup>
  <ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp20</LanguageStandard><WarningLevel>Level4</WarningLevel><TreatWarningAsError>true</TreatWarningAsError><AdditionalOptions>/utf-8 /permissive- /Zc:__cplusplus %(AdditionalOptions)</AdditionalOptions></ClCompile></ItemDefinitionGroup>
  <ItemGroup><ClCompile Include="main.cpp"/><ClInclude Include="api.h"/></ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <Import Project="@SOURCE@/msbuild/reflgen.targets" />
</Project>
]=])
string(CONFIGURE "${project_text}" project_text @ONLY)
set(project "${WORK}/native_consumer.vcxproj")
file(WRITE "${project}" "${project_text}")
set(build "${MSBUILD}" "${project}" /nologo /v:minimal /p:Configuration=Debug /p:Platform=x64 /nodeReuse:false)
execute_process(COMMAND ${build} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "MSBuild native table consumer failed:\n${output}\n${error}")
endif()
set(base "${WORK}/obj/reflgen/reflgen_native_consumer")
foreach(suffix _native.fs _native.cpp _native.json .optional.outputs)
    if(NOT EXISTS "${base}${suffix}")
        message(FATAL_ERROR "MSBuild did not generate ${base}${suffix}")
    endif()
endforeach()
execute_process(COMMAND ${build} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR output MATCHES "reflgen: generating reflection")
    message(FATAL_ERROR "MSBuild table generation did not remain up to date:\n${output}\n${error}")
endif()
file(REMOVE "${base}_native.fs")
execute_process(COMMAND ${build} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT EXISTS "${base}_native.fs")
    message(FATAL_ERROR "MSBuild did not regenerate a missing F# output:\n${output}\n${error}")
endif()
file(WRITE "${WORK}/obj/reflgen/user-owned.txt" "preserve this user file\n")
execute_process(COMMAND ${build} /t:Clean /p:ReflgenFSharpTable=false
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/obj/reflgen/user-owned.txt")
    message(FATAL_ERROR "MSBuild clean did not preserve the unrelated file:\n${output}\n${error}")
endif()
foreach(suffix _native.fs _native.cpp _native.json .optional.outputs)
    if(EXISTS "${base}${suffix}")
        message(FATAL_ERROR "MSBuild clean retained owned output ${base}${suffix}")
    endif()
endforeach()
file(WRITE "${base}_native.fs" "user F# source\n")
execute_process(COMMAND ${build} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "existing unowned optional output")
    message(FATAL_ERROR "MSBuild did not reject an unowned F# output:\n${output}\n${error}")
endif()
file(READ "${base}_native.fs" user_source)
if(NOT user_source STREQUAL "user F# source\n")
    message(FATAL_ERROR "MSBuild changed an unowned F# file")
endif()
message(STATUS "MSBuild F# table: ABI compilation, incremental regeneration and safe clean passed")

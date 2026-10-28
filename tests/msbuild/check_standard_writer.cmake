# Tests output wiring only; compiler capability is covered separately by the real compile probe.
foreach(name MSBUILD SOURCE WORK)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "check_standard_writer: ${name} is not set")
    endif()
endforeach()
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/writer.proj" "<Project>
 <PropertyGroup>
  <ReflgenSelectedBackend>Std</ReflgenSelectedBackend>
  <ReflgenModule>fixture</ReflgenModule>
  <ReflgenOutputDirectory>${WORK}/out/</ReflgenOutputDirectory>
  <_ReflgenInjection>${WORK}/out/reflgen_fixture.h</_ReflgenInjection>
  <_ReflgenModuleSource>${WORK}/out/reflgen_fixture.cpp</_ReflgenModuleSource>
  <ReflgenRegistrationHeaders>${WORK}/custom.h</ReflgenRegistrationHeaders>
 </PropertyGroup>
 <ItemGroup><ClInclude Include=\"${WORK}/types.h\"/><ReflgenReflectType Include=\"game::player\"/><ReflgenReflectType Include=\"game::enemy\"/><ReflgenStandardImport Include=\"game.types\"/></ItemGroup>
 <Import Project=\"${SOURCE}/msbuild/reflgen.standard.targets\"/>
 <Target Name=\"Test\" DependsOnTargets=\"ReflgenGenerateStandard\">
  <Error Condition=\"'@(ClCompile)' != '${WORK}/out/reflgen_fixture.cpp'\" Text=\"Registration source missing\"/>
 </Target>
</Project>")
execute_process(COMMAND "${MSBUILD}" "${WORK}/writer.proj" /t:Test /nologo /nodeReuse:false
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "standard writer failed: ${output}\n${error}")
endif()
file(READ "${WORK}/out/reflgen_fixture.cpp" text)
string(REPLACE "\\" "/" text "${text}")
foreach(expected "registry.add<game::player>();" "registry.add<game::enemy>();" "import game.types;"
                 "#include \"${WORK}/custom.h\"")
    string(FIND "${text}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing '${expected}':\n${text}")
    endif()
endforeach()
file(READ "${WORK}/out/reflgen_fixture.h" text)
if(NOT text MATCHES "void register_fixture\\(registry&\\);")
    message(FATAL_ERROR "Malformed declaration: ${text}")
endif()
if(NOT text MATCHES "void register_fixture\\(\\);")
    message(FATAL_ERROR "Default-registry overload missing: ${text}")
endif()
if(NOT text MATCHES "import game.types;")
    message(FATAL_ERROR "Provider import missing: ${text}")
endif()
message(STATUS "standard registration writer passed")

# Optional header-generator outputs must not silently disappear on a capable compiler.
foreach(option ReflgenInterop ReflgenFSharpTable)
file(WRITE "${WORK}/request.proj" "<Project>
 <PropertyGroup><${option}>true</${option}></PropertyGroup>
 <ItemGroup><ClCompile Include=\"main.cpp\"><LanguageStandard>stdcpplatest</LanguageStandard></ClCompile><ReflgenReflectType Include=\"game::player\"/></ItemGroup>
 <Import Project=\"${SOURCE}/msbuild/reflgen.standard.targets\"/>
 <Target Name=\"Test\" DependsOnTargets=\"_ReflgenStandardRequest\">
  <Error Condition=\"'$(_ReflgenWantsStandard)' != 'false'\" Text=\"${option} must keep the header generator\"/>
 </Target>
</Project>")
execute_process(COMMAND "${MSBUILD}" "${WORK}/request.proj" /t:Test /nologo /nodeReuse:false
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${option} AUTO request failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${MSBUILD}" "${WORK}/request.proj" /t:Test /p:ReflgenReflectionBackend=Std /nologo /nodeReuse:false
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "RG0263")
    message(FATAL_ERROR "forced STD must reject ${option}: ${output}\n${error}")
endif()
endforeach()

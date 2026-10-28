file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /interface /c "${SOURCE}/imported_base.ixx"
    /ifcOutput "${WORK}/base.ifc" "/Fo${WORK}/base.obj"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "base module failed: ${out}${err}")
endif()
execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /interface /c "${SOURCE}/imported_user.ixx"
    /reference "reflgen.test.imported_base=${WORK}/base.ifc"
    /ifcOutput "${WORK}/user.ifc" "/Fo${WORK}/user.obj"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "consumer module failed: ${out}${err}")
endif()
execute_process(COMMAND "${INSPECTOR}" "${WORK}/user.ifc" "${WORK}/base.ifc"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "imported template reference inspection failed (${result}): ${out}${err}")
endif()
execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /interface /c "${SOURCE}/imported_specialized.ixx"
    /ifcOutput "${WORK}/specialized.ifc" "/Fo${WORK}/specialized.obj"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "specialized provider failed: ${out}${err}")
endif()
execute_process(COMMAND "${INSPECTOR}" "${WORK}/user.ifc" "${WORK}/specialized.ifc"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(result EQUAL 0)
    message(FATAL_ERROR "specialized or mismatched provider must be rejected")
endif()
file(REMOVE "${WORK}/reflgen_imported.h")
execute_process(COMMAND "${GENERATOR}" --ifc "${WORK}/user.ifc" --import reflgen.test.imported_user
    --module imported --output "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(result EQUAL 0 OR NOT err MATCHES "RG0200.*imported base template cannot be verified \\(module reflgen.test.imported_base\\)" OR
   EXISTS "${WORK}/reflgen_imported.h")
    message(FATAL_ERROR "imported base template must fail with a useful diagnostic: ${out}${err}")
endif()

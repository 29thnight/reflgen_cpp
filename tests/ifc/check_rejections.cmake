file(MAKE_DIRECTORY "${WORK}")
function(reject name declaration expected)
    set(source "${WORK}/${name}.ixx")
    set(ifc "${WORK}/${name}.ifc")
    file(REMOVE "${WORK}/reflgen_${name}.h")
    file(WRITE "${source}" "export module rejected;\n${declaration}\n")
    execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /wd5030 /interface /c "${source}" ${ARGN}
        /ifcOutput "${ifc}" "/Fo${WORK}/${name}.obj"
        RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "rejection fixture did not compile: ${log}${error}")
    endif()
    execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /wd5030 /interface /P "/Fi${ifc}.i" "${source}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "rejection metadata did not preprocess: ${log}${error}")
    endif()
    execute_process(COMMAND "${GENERATOR}" --ifc "${ifc}" --metadata "${ifc}.i" --import rejected --module "${name}" --output "${WORK}"
        RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "RG0200.*${expected}" OR EXISTS "${WORK}/reflgen_${name}.h")
        message(FATAL_ERROR "unsupported ${name} did not fail safely: ${log}${error}")
    endif()
    if(NOT error MATCHES "\\.ixx\\([0-9]+,[0-9]+\\): error RG0200")
        message(FATAL_ERROR "diagnostic does not point to the original module source: ${error}")
    endif()
endfunction()
reject(bitfield "export struct bad { unsigned bits : 2; };" "bit-field")
reject(reference "export struct bad { int& value; };" "reference field")
reject(reference_alias "using int_ref = int&; export struct bad { int_ref value; };" "reference field")
reject(private_without_friend "export class [[reflgen::reflect]] bad { int value = 1; };" "friend")
reject(wrong_friend "namespace unrelated { struct access; } export class [[reflgen::reflect]] bad { friend struct unrelated::access; int value = 1; };" "friend")
reject(module_local_friend "export namespace reflgen { struct access {}; } export class [[reflgen::reflect]] bad { friend struct reflgen::access; int value = 1; };" "friend")
reject(private_reflected_base "export struct base { int x; }; export class bad : private base { public: int y; };" "base")
# Ignoring a helper does not make its reflected ancestors safe to omit through a non-public edge.
foreach(access IN ITEMS private protected)
    reject(${access}_ignored_intermediate
        "export struct base { int x; };\nexport struct [[reflgen::ignore]] helper : public base {};\nexport struct bad : ${access} helper { int y; };"
        "non-public reflected base")
endforeach()
reject(inaccessible_middle_edge
    "export struct base { int x; };\nexport struct [[reflgen::ignore]] lower : public base {};\nexport struct [[reflgen::ignore]] upper : private lower {};\nexport struct bad : public upper { int y; };"
    "non-public reflected base")
reject(private_crtp_intermediate
    "export struct base { int x; };\nexport template<class Derived, class Base> struct [[reflgen::ignore]] helper : public Base {};\nexport struct bad : private helper<bad, base> { int y; };"
    "non-public reflected base")
file(WRITE "${WORK}/foreign.ixx" "export module foreign.base;\nexport struct foreign_base { int value = 1; };\n")
execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /interface /c "${WORK}/foreign.ixx"
    /ifcOutput "${WORK}/foreign.ifc" "/Fo${WORK}/foreign.obj"
    RESULT_VARIABLE foreign_result OUTPUT_VARIABLE foreign_log ERROR_VARIABLE foreign_error)
if(NOT foreign_result EQUAL 0)
    message(FATAL_ERROR "imported-base fixture did not compile: ${foreign_log}${foreign_error}")
endif()
reject(imported_base "import foreign.base;\nexport struct bad : foreign_base { int added = 2; };" "(imported|reference)"
    /reference "foreign.base=${WORK}/foreign.ifc")
message(STATUS "IFC unsupported declarations were rejected without partial outputs")

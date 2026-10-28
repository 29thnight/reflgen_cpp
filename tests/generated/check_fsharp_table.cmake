# Native table output is opt-in, deterministic, discoverable and fail-closed.
cmake_minimum_required(VERSION 3.25)
foreach(name GENERATOR WORK)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "check_fsharp_table: ${name} is not set")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/source")
set(header "${WORK}/source/native_table.h")
set(directory "${WORK}/output")
set(base "${directory}/reflgen_native_test")
set(valid_source [=[
namespace native_fixture {
inline constexpr unsigned ApiVersion = 46;
struct Api {
    int version;
    int structSize;
    int (*Add)(int, int);
};
using Binding [[reflgen::table("ScriptApi", "version", "structSize", ApiVersion)]] = Api;
}
]=])
file(WRITE "${header}" "${valid_source}")
function(run_generator)
    execute_process(COMMAND "${GENERATOR}" --module native_test --output "${directory}" ${ARGN}
        "${header}" -- --target=x86_64-unknown-linux-gnu
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "native table generation failed:\n${output}\n${error}")
    endif()
endfunction()

run_generator()
file(GLOB unexpected "${base}_native.*")
if(unexpected)
    message(FATAL_ERROR "native bindings were emitted without --fsharp-table: ${unexpected}")
endif()
run_generator(--fsharp-table --fsharp-namespace Tests.Native --discover)
set(outputs "${base}_native.fs" "${base}_native.cpp" "${base}_native.json" "${base}.h" "${base}.cpp"
    "${directory}/native_table.reflgen.h")
foreach(path IN LISTS outputs)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "missing generated output ${path}")
    endif()
    file(SHA256 "${path}" hash)
    file(TIMESTAMP "${path}" stamp "%Y-%m-%dT%H:%M:%S" UTC)
    set("hash_${path}" "${hash}")
    set("stamp_${path}" "${stamp}")
endforeach()
file(READ "${base}_native.json" manifest)
string(JSON format GET "${manifest}" format)
string(JSON version GET "${manifest}" version)
string(JSON pointer_width GET "${manifest}" pointer_width)
string(JSON table_count LENGTH "${manifest}" tables)
string(JSON api_version GET "${manifest}" tables 0 version)
if(NOT format STREQUAL "reflgen.fsharp-native" OR NOT version EQUAL 1 OR
   NOT pointer_width EQUAL 64 OR NOT table_count EQUAL 1 OR NOT api_version EQUAL 46)
    message(FATAL_ERROR "unexpected native table manifest:\n${manifest}")
endif()
file(READ "${base}_native.fs" fsharp)
if(NOT fsharp MATCHES "Tests.*Native" OR fsharp MATCHES "DllImport|ScriptCore")
    message(FATAL_ERROR "the generated F# table must use the selected namespace without a C# bridge:\n${fsharp}")
endif()

function(check_preserved)
    foreach(path IN LISTS outputs)
        file(SHA256 "${path}" hash)
        file(TIMESTAMP "${path}" stamp "%Y-%m-%dT%H:%M:%S" UTC)
        if(NOT hash STREQUAL "${hash_${path}}" OR NOT stamp STREQUAL "${stamp_${path}}")
            message(FATAL_ERROR "existing output was unexpectedly rewritten: ${path}")
        endif()
    endforeach()
endfunction()
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
run_generator(--fsharp-table --fsharp-namespace Tests.Native --discover)
check_preserved()

# A manifest may not replace F# source, and invalid options may not rewrite any previous output.
execute_process(COMMAND "${GENERATOR}" --module native_test --output "${directory}"
    --fsharp-table --declarations-json "${base}_native.fs" "${header}"
    -- --target=x86_64-unknown-linux-gnu
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "RG0001.*collides")
    message(FATAL_ERROR "colliding F# output was not rejected:\n${output}\n${error}")
endif()
check_preserved()
execute_process(COMMAND "${GENERATOR}" --module native_test --output "${directory}"
    --fsharp-namespace Tests.Native "${header}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "RG0001.*requires --fsharp-table")
    message(FATAL_ERROR "unenabled namespace option was not rejected:\n${output}\n${error}")
endif()
check_preserved()
execute_process(COMMAND "${GENERATOR}" --module native_test --output "${directory}"
    --fsharp-table --fsharp-namespace "Invalid-Namespace" "${header}"
    -- --target=x86_64-unknown-linux-gnu
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "RG0400")
    message(FATAL_ERROR "invalid F# namespace was not rejected:\n${output}\n${error}")
endif()
check_preserved()

# Validation failures preserve all successful outputs, including ordinary reflection output.
string(REPLACE "\"structSize\"" "\"missing_size\"" invalid_source "${valid_source}")
file(WRITE "${header}" "${invalid_source}")
execute_process(COMMAND "${GENERATOR}" --module native_test --output "${directory}"
    --fsharp-table "${header}" -- --target=x86_64-unknown-linux-gnu
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "native_table.h\\([0-9]+,[0-9]+\\): error RG040[01]:")
    message(FATAL_ERROR "invalid table was not rejected with its source location:\n${output}\n${error}")
endif()
check_preserved()

# Function pointer slots have identical sizes even when their signatures change.
# The fingerprint must detect this ABI drift without relying on sizeof(table).
string(JSON original_fingerprint GET "${manifest}" fingerprint)
string(JSON original_size GET "${manifest}" tables 0 size)
string(REPLACE "int (*Add)(int, int)" "int (*Add)(int, unsigned)" changed_source "${valid_source}")
file(WRITE "${header}" "${changed_source}")
run_generator(--fsharp-table --fsharp-namespace Tests.Native)
file(READ "${base}_native.json" changed_manifest)
string(JSON changed_fingerprint GET "${changed_manifest}" fingerprint)
string(JSON changed_size GET "${changed_manifest}" tables 0 size)
if(changed_fingerprint STREQUAL original_fingerprint OR NOT changed_size EQUAL original_size)
    message(FATAL_ERROR "a changed function signature was not detected independently of table size")
endif()

# Removing the opt-in alias under --discover emits an empty current contract.
file(WRITE "${header}" "struct unselected { int value; };\n")
run_generator(--fsharp-table --discover)
file(READ "${base}_native.json" empty_manifest)
string(JSON empty_count LENGTH "${empty_manifest}" tables)
if(NOT empty_count EQUAL 0)
    message(FATAL_ERROR "removed table is still published:\n${empty_manifest}")
endif()
message(STATUS "F# native table: explicit outputs, deterministic regeneration, collision protection and fail-closed validation")

file(MAKE_DIRECTORY "${WORK}")
set(module reflgen.test.crtp.reflection)
execute_process(COMMAND "${GENERATOR}" --ifc "${IFC}" --import reflgen.test.crtp
    --module crtp_module --module-interface "${module}" --output "${WORK}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "module-interface generation failed: ${output}${error}")
endif()
set(runtime /MD)
if(CONFIG STREQUAL "Debug")
    set(runtime /MDd)
endif()
execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
    "${runtime}" "/I${INCLUDE}" /interface /c "${WORK}/${module}.ixx"
    /reference "reflgen.test.crtp=${IFC}" /ifcOutput "${WORK}/reflection.ifc"
    "/Fo${WORK}/reflection.obj"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "generated reflection module did not compile: ${output}${error}")
endif()
file(WRITE "${WORK}/consumer.cpp" [=[
#include "reflgen/json.h"
import reflgen.test.crtp;
import reflgen.test.crtp.reflection;
int main()
{
    game::guarded_point value;
    value.x = 17;
    value.shield = 29;
    const auto text = reflgen::json::to_string(value);
    const auto restored = reflgen::json::from_string<game::guarded_point>(text);
    reflgen::registry registry;
    reflgen::generated::register_crtp_module(registry);
    return restored.x == 17 && restored.shield == 29 ? 0 : 1;
}
]=])
execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
    "${runtime}" "/I${INCLUDE}" /c "${WORK}/consumer.cpp"
    /reference "reflgen.test.crtp=${IFC}" /reference "${module}=${WORK}/reflection.ifc"
    "/Fo${WORK}/consumer.obj"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "generated reflection module consumer did not compile: ${output}${error}")
endif()
execute_process(COMMAND "${COMPILER}" /nologo "${runtime}" "${OBJECT}"
    "${WORK}/reflection.obj" "${WORK}/consumer.obj" "/Fe${WORK}/consumer.exe"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "generated reflection module consumer did not link: ${output}${error}")
endif()
execute_process(COMMAND "${WORK}/consumer.exe" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "generated reflection module registration/roundtrip failed: ${result}")
endif()

# Every requested output must be checked before the first write. Empty/all-ignored
# modules have no extracted type positions, so the primary IFC source path is essential.
function(expect_collision name protected_path)
    file(SHA256 "${protected_path}" before)
    execute_process(COMMAND "${GENERATOR}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "collides with an input")
        message(FATAL_ERROR "${name} did not reject its input/output collision: ${result}\n${output}${error}")
    endif()
    file(SHA256 "${protected_path}" after)
    if(NOT before STREQUAL after)
        message(FATAL_ERROR "${name} changed the protected input")
    endif()
endfunction()

foreach(kind empty ignored)
    set(collision_dir "${WORK}/collision-${kind}")
    file(MAKE_DIRECTORY "${collision_dir}")
    set(native_module "reflgen.test.collision.${kind}")
    set(generated_module "${native_module}.generated")
    set(source "${collision_dir}/${generated_module}.ixx")
    set(body "")
    if(kind STREQUAL ignored)
        set(body "export struct [[reflgen::ignore]] hidden { int value; };\n")
    endif()
    file(WRITE "${source}" "export module ${native_module};\n${body}")
    set(provider "${collision_dir}/provider.ifc")
    execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /wd5030
        "${runtime}" /interface /c "${source}" /ifcOutput "${provider}" "/Fo${collision_dir}/provider.obj"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "cannot build ${kind} collision fixture: ${output}${error}")
    endif()
    set(metadata "${collision_dir}/provider.i")
    execute_process(COMMAND "${COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /wd5030
        "${runtime}" /P "${source}" "/Fi${metadata}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "cannot preprocess ${kind} collision fixture: ${output}${error}")
    endif()
    expect_collision("${kind} primary source" "${source}"
        --ifc "${provider}" --import "${native_module}" --module "collision_${kind}"
        --module-interface "${generated_module}" --output "${collision_dir}" --metadata "${metadata}")
    if(EXISTS "${collision_dir}/reflgen_collision_${kind}.attributes.tsv")
        message(FATAL_ERROR "${kind} collision published a partial attribute catalog")
    endif()

    # Keep the IFC and source intact while an output pathname aliases the /P input.
    set(sidecar_dir "${collision_dir}/sidecar")
    file(MAKE_DIRECTORY "${sidecar_dir}")
    set(sidecar_module "${native_module}.metadata")
    set(sidecar "${sidecar_dir}/${sidecar_module}.ixx")
    file(COPY_FILE "${metadata}" "${sidecar}")
    expect_collision("${kind} module/metadata" "${sidecar}"
        --ifc "${provider}" --import "${native_module}" --module "collision_${kind}"
        --module-interface "${sidecar_module}" --output "${sidecar_dir}" --metadata "${sidecar}")
    if(EXISTS "${sidecar_dir}/reflgen_collision_${kind}.attributes.tsv")
        message(FATAL_ERROR "metadata collision published a partial attribute catalog")
    endif()

    set(catalog_dir "${collision_dir}/catalog")
    file(MAKE_DIRECTORY "${catalog_dir}")
    set(catalog "${catalog_dir}/reflgen_collision_${kind}.attributes.tsv")
    file(COPY_FILE "${metadata}" "${catalog}")
    expect_collision("${kind} catalog/metadata" "${catalog}"
        --ifc "${provider}" --import "${native_module}" --module "collision_${kind}"
        --module-interface "${sidecar_module}" --output "${catalog_dir}" --metadata "${catalog}")
    if(EXISTS "${catalog_dir}/${sidecar_module}.ixx")
        message(FATAL_ERROR "catalog collision published the module before validating every output")
    endif()
endforeach()

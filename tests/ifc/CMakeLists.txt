# Explicit IFC output path makes the provider/generator dependency independent of CMake's private BMI layout.
set(ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/types.ifc")
set(object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/types.obj")
add_custom_command(OUTPUT "${ifc}" "${object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
            "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>"
            /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/types.ixx"
            /ifcOutput "${ifc}" /Fo${object}
    DEPENDS types.ixx VERBATIM)
set_source_files_properties("${object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_library(reflgen_ifc_types STATIC "${object}")
set_target_properties(reflgen_ifc_types PROPERTIES LINKER_LANGUAGE CXX)
reflgen_generate_ifc(reflgen_ifc_game PROVIDER reflgen_ifc_types IFC "${ifc}"
    IMPORT reflgen.test.types MODULE ifc_game)
add_executable(reflgen_ifc_test main.cpp)
target_link_libraries(reflgen_ifc_test PRIVATE reflgen_ifc_game)
target_compile_options(reflgen_ifc_test PRIVATE /W4 /WX /permissive-)
add_test(NAME reflgen_ifc_runtime COMMAND reflgen_ifc_test)

# A separate module without unrelated opaque partial specializations exercises
# the conservative CRTP path without silently accepting unknown specializations.
set(crtp_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crtp.ifc")
set(crtp_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crtp.obj")
add_custom_command(OUTPUT "${crtp_ifc}" "${crtp_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
            "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>"
            /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/crtp/types.ixx"
            /ifcOutput "${crtp_ifc}" /Fo${crtp_object}
    DEPENDS crtp/types.ixx VERBATIM)
set_source_files_properties("${crtp_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_library(reflgen_ifc_crtp_types STATIC "${crtp_object}")
set_target_properties(reflgen_ifc_crtp_types PROPERTIES LINKER_LANGUAGE CXX)
reflgen_generate_ifc(reflgen_ifc_crtp PROVIDER reflgen_ifc_crtp_types IFC "${crtp_ifc}"
    IMPORT reflgen.test.crtp MODULE ifc_crtp)
add_executable(reflgen_ifc_crtp_test crtp/main.cpp)
target_link_libraries(reflgen_ifc_crtp_test PRIVATE reflgen_ifc_crtp)
target_compile_options(reflgen_ifc_crtp_test PRIVATE /W4 /WX /permissive-)
add_test(NAME reflgen_ifc_crtp_runtime COMMAND reflgen_ifc_crtp_test)
add_test(NAME reflgen_ifc_module_interface
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DCOMPILER=${CMAKE_CXX_COMPILER}"
        "-DIFC=${crtp_ifc}" "-DOBJECT=${crtp_object}" "-DINCLUDE=${PROJECT_SOURCE_DIR}/include"
        "-DCONFIG=$<CONFIG>" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/module-interface/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_module_interface.cmake")
add_executable(reflgen_ifc_reference_probe crtp/inspect_reference.cpp)
target_link_libraries(reflgen_ifc_reference_probe PRIVATE reflgen_ifc_reader)
add_dependencies(reflgen_ifc_crtp_test reflgen_ifc_reference_probe)
add_test(NAME reflgen_ifc_crtp_imported_rejected
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DCOMPILER=${CMAKE_CXX_COMPILER}"
            "-DINSPECTOR=$<TARGET_FILE:reflgen_ifc_reference_probe>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/crtp" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/crtp-imported/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/crtp/check_imported.cmake")
add_test(NAME reflgen_ifc_crtp_snapshot_receipt_experiment
    COMMAND "${CMAKE_COMMAND}" "-DCOMPILER=${CMAKE_CXX_COMPILER}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/crtp"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/crtp-snapshot-receipt/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/crtp/check_snapshot_receipt.cmake")

set(crtp_adversary_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crtp-adversary.ifc")
set(crtp_adversary_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crtp-adversary.obj")
add_custom_command(OUTPUT "${crtp_adversary_ifc}" "${crtp_adversary_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
            "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>"
            /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/crtp/adversary.ixx"
            /ifcOutput "${crtp_adversary_ifc}" /Fo${crtp_adversary_object}
    DEPENDS crtp/adversary.ixx VERBATIM)
add_custom_target(reflgen_ifc_crtp_adversary DEPENDS "${crtp_adversary_ifc}" "${crtp_adversary_object}")
add_dependencies(reflgen_ifc_crtp_test reflgen_ifc_crtp_adversary)
add_test(NAME reflgen_ifc_crtp_adversary
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DIFC=${crtp_adversary_ifc}"
            "-DIMPORT_NAME=reflgen.test.crtp_adversary" "-DINTERFACE_NAME=reflgen.test.crtp_adversary_reflection"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/crtp-adversary-output/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/crtp/check_adversary.cmake")

set(crtp_nonmatching_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crtp-nonmatching.ifc")
set(crtp_nonmatching_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/crtp-nonmatching.obj")
add_custom_command(OUTPUT "${crtp_nonmatching_ifc}" "${crtp_nonmatching_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
            "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>"
            /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/crtp/nonmatching.ixx"
            /ifcOutput "${crtp_nonmatching_ifc}" /Fo${crtp_nonmatching_object}
    DEPENDS crtp/nonmatching.ixx VERBATIM)
add_custom_target(reflgen_ifc_crtp_nonmatching DEPENDS "${crtp_nonmatching_ifc}" "${crtp_nonmatching_object}")
add_dependencies(reflgen_ifc_crtp_test reflgen_ifc_crtp_nonmatching)
add_test(NAME reflgen_ifc_crtp_nonmatching_fails_closed
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DIFC=${crtp_nonmatching_ifc}"
            "-DIMPORT_NAME=reflgen.test.crtp_nonmatching" "-DINTERFACE_NAME=reflgen.test.crtp_nonmatching_reflection"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/crtp-nonmatching-output/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/crtp/check_adversary.cmake")
add_test(NAME reflgen_ifc_generator
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DIFC=${ifc}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/generator-check/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_generator.cmake")
add_test(NAME reflgen_ifc_rejections
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DCOMPILER=${CMAKE_CXX_COMPILER}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rejections/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_rejections.cmake")
find_program(REFLGEN_IFC_MSBUILD NAMES MSBuild msbuild)
find_program(REFLGEN_IFC_POWERSHELL NAMES pwsh powershell)
if(REFLGEN_IFC_MSBUILD AND REFLGEN_IFC_POWERSHELL)
    if(TARGET reflgen_cli)
      add_test(NAME reflgen_ifc_mixed
        COMMAND "${REFLGEN_IFC_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/check_mixed.ps1"
            -Generator "$<TARGET_FILE:reflgen_ifc>" -HeaderGenerator "$<TARGET_FILE:reflgen_cli>"
            -MSBuild "${REFLGEN_IFC_MSBUILD}" -Work "${CMAKE_CURRENT_BINARY_DIR}/mixed"
            -Configuration "$<CONFIG>")
    endif()
    add_test(NAME reflgen_ifc_msbuild
        COMMAND "${REFLGEN_IFC_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/check_msbuild.ps1"
            -Generator "$<TARGET_FILE:reflgen_ifc>" -MSBuild "${REFLGEN_IFC_MSBUILD}"
            -Work "${CMAKE_CURRENT_BINARY_DIR}/msbuild" -Configuration "$<CONFIG>")
endif()

# Attributes supplied by MSVC preprocessing, with a user attribute in a separately compiled module.
set(attribute_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/attributes.ifc")
set(attribute_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/attributes.obj")
set(annotated_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/annotated.ifc")
set(annotated_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/annotated.obj")
add_custom_command(OUTPUT "${attribute_ifc}" "${attribute_object}" "${annotated_ifc}" "${annotated_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /wd5030
        "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>" /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/attributes/attributes.ixx"
        /ifcOutput "${attribute_ifc}" /Fo${attribute_object}
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /wd5030 /DREFLGEN_ATTRIBUTE_TEST
        "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>" /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/attributes/types.ixx"
        /reference "reflgen.test.attributes=${attribute_ifc}" /ifcOutput "${annotated_ifc}" /Fo${annotated_object}
    DEPENDS attributes/attributes.ixx attributes/types.ixx VERBATIM)
set_source_files_properties("${attribute_object}" "${annotated_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_library(reflgen_annotated_types STATIC "${attribute_object}" "${annotated_object}")
set_target_properties(reflgen_annotated_types PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(reflgen_annotated_types PRIVATE REFLGEN_ATTRIBUTE_TEST)
reflgen_generate_ifc(reflgen_annotated PROVIDER reflgen_annotated_types IFC "${annotated_ifc}"
    IMPORT reflgen.test.annotated MODULE annotated SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/attributes/types.ixx"
    ATTRIBUTE_SCOPES editor ATTRIBUTE_MODULES "reflgen.test.attributes=${attribute_ifc}")
add_executable(reflgen_ifc_attributes_test attributes/main.cpp)
target_link_libraries(reflgen_ifc_attributes_test PRIVATE reflgen_annotated)
target_compile_options(reflgen_ifc_attributes_test PRIVATE /W4 /WX /permissive-)
add_test(NAME reflgen_ifc_attributes COMMAND reflgen_ifc_attributes_test)
add_test(NAME reflgen_ifc_metadata
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:reflgen_ifc>" "-DIFC=${annotated_ifc}"
        "-DMETADATA=${CMAKE_CURRENT_BINARY_DIR}/reflgen-ifc/reflgen_annotated/$<CONFIG>/reflgen_annotated.i"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/metadata-check/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/attributes/check_metadata.cmake")

# Engine-shaped declarations exercise the actual provider -> generator -> registration -> consumer path.
set(engine_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/engine.ifc")
set(engine_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/engine.obj")
set(serializer_ifc "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/engine-serialization.ifc")
set(serializer_object "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/engine-serialization.obj")
add_custom_command(OUTPUT "${engine_ifc}" "${engine_object}" "${serializer_ifc}" "${serializer_object}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /wd5030 /permissive-
        "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>"
        "/I${PROJECT_SOURCE_DIR}/include" /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/engine/types.ixx"
        /ifcOutput "${engine_ifc}" "/Fo${engine_object}"
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /permissive-
        "$<IF:$<CONFIG:Debug>,/MDd,/MD>" "/D$<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>"
        /interface /c "${CMAKE_CURRENT_SOURCE_DIR}/engine/serialization.ixx"
        /ifcOutput "${serializer_ifc}" "/Fo${serializer_object}"
    DEPENDS engine/types.ixx engine/serialization.ixx "${PROJECT_SOURCE_DIR}/include/reflgen/core/hook.h" VERBATIM)
set_source_files_properties("${engine_object}" "${serializer_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_library(reflgen_engine_types STATIC "${engine_object}" "${serializer_object}")
set_target_properties(reflgen_engine_types PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(reflgen_engine_types PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_compile_options(reflgen_engine_types PRIVATE /wd5030)
reflgen_generate_ifc(reflgen_engine PROVIDER reflgen_engine_types IFC "${engine_ifc}"
    IMPORT reflgen.test.engine MODULE engine SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/engine/types.ixx"
    REGISTRATION_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/engine/serializers.h"
    REGISTRATION_MODULES "reflgen.test.engine.serialization=${serializer_ifc}")
add_executable(reflgen_ifc_engine_test engine/main.cpp)
target_link_libraries(reflgen_ifc_engine_test PRIVATE reflgen_engine)
target_compile_options(reflgen_ifc_engine_test PRIVATE /W4 /WX /permissive-)
add_test(NAME reflgen_ifc_engine COMMAND reflgen_ifc_engine_test)
if(REFLGEN_IFC_MSBUILD AND REFLGEN_IFC_POWERSHELL)
    add_test(NAME reflgen_ifc_engine_msbuild
        COMMAND "${REFLGEN_IFC_POWERSHELL}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/check_engine_msbuild.ps1"
            -Generator "$<TARGET_FILE:reflgen_ifc>" -MSBuild "${REFLGEN_IFC_MSBUILD}"
            -Work "${CMAKE_CURRENT_BINARY_DIR}/engine-msbuild" -Configuration "$<CONFIG>")
endif()

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

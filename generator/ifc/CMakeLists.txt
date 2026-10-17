# Optional MSVC IFC frontend. Dependencies are pinned; normal header builds remain offline.
include(FetchContent)
FetchContent_Declare(reflgen_ifc_sdk
    GIT_REPOSITORY https://github.com/microsoft/ifc.git
    GIT_TAG 096b22d357d8af1c5f10d5f1895b8e2e80eaae5a
    SOURCE_SUBDIR reflgen-unused)
FetchContent_Declare(reflgen_ifc_gsl
    GIT_REPOSITORY https://github.com/microsoft/GSL.git
    GIT_TAG 2828399820ef4928cc89b65605dca5dc68efca6e
    SOURCE_SUBDIR reflgen-unused)
# Build only the reader, without the SDK's tools, installation rules or package-manager requirements.
FetchContent_MakeAvailable(reflgen_ifc_sdk reflgen_ifc_gsl)
add_library(reflgen_ifc_reader STATIC
    ${reflgen_ifc_sdk_SOURCE_DIR}/src/file.cxx
    ${reflgen_ifc_sdk_SOURCE_DIR}/src/sgraph.cxx
    ${reflgen_ifc_sdk_SOURCE_DIR}/src/input-file.cxx
    ${reflgen_ifc_sdk_SOURCE_DIR}/src/ifc-reader/operators.cxx
    src/reader.cpp
    ${reflgen_ifc_sdk_SOURCE_DIR}/src/ifc-reader/util.cxx
    ${reflgen_ifc_sdk_SOURCE_DIR}/src/hash_win.cxx)
target_include_directories(reflgen_ifc_reader SYSTEM PUBLIC
    ${reflgen_ifc_sdk_SOURCE_DIR}/include ${reflgen_ifc_gsl_SOURCE_DIR}/include)
target_compile_definitions(reflgen_ifc_reader PRIVATE NOMINMAX)
target_compile_features(reflgen_ifc_reader PUBLIC cxx_std_23)
target_link_libraries(reflgen_ifc_reader PRIVATE bcrypt)
add_executable(reflgen_ifc src/main.cpp src/metadata.cpp ../src/reflgen.manifest)
set_target_properties(reflgen_ifc PROPERTIES OUTPUT_NAME reflgen-ifc)
target_link_libraries(reflgen_ifc PRIVATE reflgen_ifc_reader reflgen_generator_core)
target_compile_options(reflgen_ifc PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
add_executable(reflgen::ifc ALIAS reflgen_ifc)

# reflgen 생성기 — libclang C API 로 header 를 읽는다.
#
# libclang 을 찾는 순서:
#   1. REFLGEN_LIBCLANG_DIR (import library 가 있는 디렉터리)
#   2. Visual Studio 동봉 LLVM — cl.exe·clang-cl.exe 위치에서 거슬러 찾는다(VS 만 있으면 된다)
#   3. 흔한 시스템 경로(apt·Homebrew LLVM)
# C API header 는 third_party/clang-c 에 둔 LLVM 22.1.3 판을 쓴다(C API 는 ABI 가 안정적이다).

set(REFLGEN_LIBCLANG_DIR "" CACHE PATH "Directory containing the libclang import library (libclang.lib / libclang.so)")

set(reflgen_libclang_hints "${REFLGEN_LIBCLANG_DIR}")
get_filename_component(reflgen_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
list(APPEND reflgen_libclang_hints
    "${reflgen_compiler_dir}/../lib"                       # VC/Tools/Llvm/x64/bin/clang-cl.exe
    "${reflgen_compiler_dir}/../../../../../Llvm/x64/lib"  # VC/Tools/MSVC/<ver>/bin/Hostx64/x64/cl.exe
    "$ENV{VCINSTALLDIR}Tools/Llvm/x64/lib"
    "C:/Program Files/LLVM/lib"
    /usr/lib/llvm-22/lib /usr/lib/llvm-21/lib /usr/lib/llvm-20/lib /usr/lib/llvm-19/lib /usr/lib/llvm-18/lib
    /opt/homebrew/opt/llvm/lib /usr/local/opt/llvm/lib)

find_library(REFLGEN_LIBCLANG NAMES libclang clang HINTS ${reflgen_libclang_hints})
if(NOT REFLGEN_LIBCLANG)
    message(WARNING "reflgen: libclang was not found; the generator is not built. "
                    "Set REFLGEN_LIBCLANG_DIR to the directory containing libclang.")
    return()
endif()
message(STATUS "reflgen: libclang = ${REFLGEN_LIBCLANG}")

add_library(reflgen_generator_core STATIC
    src/attribute_scan.cpp)
target_include_directories(reflgen_generator_core PUBLIC src)
target_compile_features(reflgen_generator_core PUBLIC cxx_std_20)

add_executable(reflgen_cli
    src/catalog.cpp
    src/clang_api.cpp
    src/class_scope.cpp
    src/emit.cpp
    src/extract.cpp
    src/main.cpp)
set_target_properties(reflgen_cli PROPERTIES OUTPUT_NAME reflgen)
if(WIN32)
    # 경로를 UTF-8 로 다루게 한다(매니페스트 머리말 참고). MSVC 계열 링커가 소스 목록의 .manifest 를 넣는다.
    target_sources(reflgen_cli PRIVATE src/reflgen.manifest)
endif()
target_include_directories(reflgen_cli SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/clang-c/include")
target_link_libraries(reflgen_cli PRIVATE reflgen_generator_core "${REFLGEN_LIBCLANG}")

foreach(reflgen_tool_target reflgen_generator_core reflgen_cli)
    set_target_properties(${reflgen_tool_target} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${reflgen_tool_target} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${reflgen_tool_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

# Windows 는 실행 파일 옆에 libclang.dll 이 있어야 한다.
if(WIN32)
    get_filename_component(reflgen_libclang_lib_dir "${REFLGEN_LIBCLANG}" DIRECTORY)
    find_file(REFLGEN_LIBCLANG_DLL libclang.dll HINTS "${reflgen_libclang_lib_dir}/../bin" "${reflgen_libclang_lib_dir}")
    if(REFLGEN_LIBCLANG_DLL)
        add_custom_command(TARGET reflgen_cli POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${REFLGEN_LIBCLANG_DLL}" "$<TARGET_FILE_DIR:reflgen_cli>"
            VERBATIM)
    else()
        message(WARNING "reflgen: libclang.dll was not found next to ${REFLGEN_LIBCLANG}; copy it next to reflgen.exe")
    endif()
endif()

add_executable(reflgen::cli ALIAS reflgen_cli)

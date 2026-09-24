# reflgen_generate — target 의 header 들에서 [[reflgen::…]] 를 읽어 reflection 코드를 만든다.
#
#   reflgen_generate(my_target
#       HEADERS include/game/player.h include/game/item.h
#       [MODULE game]                      # 등록 함수 이름: reflgen::generated::register_game()
#       [ATTRIBUTE_SCOPES game editor]     # reflgen 외에 스키마로 옮길 attribute 이름공간
#       [OUTPUT_DIRECTORY dir])            # 구성(config)마다 그 아래 <config>/ 에 만든다
#
# header 는 아무것도 include 하지 않는다 — attribute 만 단다. 생성물을 묶은 주입 header
# (<OUTPUT_DIRECTORY>/<config>/reflgen_<module>.h)를 target 과 그것을 링크하는 target 의 모든 번역 단위에
# 강제 include(/FI, -include)한다(PUBLIC, BUILD_INTERFACE). 생성은 컴파일 전에 돌고, header 나 그것이
# include 하는 파일이 바뀌면 다시 돈다(depfile).
#
# 생성기 실행 파일은 이 저장소 안에서 빌드하면 reflgen::cli target 이고, 아니면
# REFLGEN_EXECUTABLE 로 경로를 준다.

# 함수는 정의될 때의 정책을 기억한다. 부르는 프로젝트의 cmake_minimum_required 와 무관하게
# DEPFILE 경로 변환(CMP0116) 등이 같은 규칙으로 동작하게 여기서 고정한다.
cmake_policy(PUSH)
cmake_policy(VERSION 3.25)

function(reflgen_generate target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "MODULE;OUTPUT_DIRECTORY" "HEADERS;ATTRIBUTE_SCOPES")
    if(NOT ARG_HEADERS)
        message(FATAL_ERROR "reflgen_generate(${target}): HEADERS is required")
    endif()
    if(NOT ARG_MODULE)
        set(ARG_MODULE "${target}")
    endif()
    string(MAKE_C_IDENTIFIER "${ARG_MODULE}" module)
    if(NOT ARG_OUTPUT_DIRECTORY)
        set(ARG_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reflgen/${target}")
    endif()
    # 구성마다 정의·include 경로가 다를 수 있으니 출력도 나눈다. 한 경로를 두 구성이 번갈아 쓰면
    # Ninja Multi-Config 에서는 같은 출력을 두 규칙이 만들게 된다.
    set(output_directory "${ARG_OUTPUT_DIRECTORY}/$<CONFIG>")

    if(TARGET reflgen_cli)
        set(generator "$<TARGET_FILE:reflgen_cli>")
        set(generator_dependency reflgen_cli)
    elseif(REFLGEN_EXECUTABLE)
        set(generator "${REFLGEN_EXECUTABLE}")
        set(generator_dependency "${REFLGEN_EXECUTABLE}")
    else()
        message(FATAL_ERROR "reflgen_generate(${target}): the generator is not available; build it "
                            "(libclang required) or set REFLGEN_EXECUTABLE")
    endif()

    set(headers)
    set(outputs)
    foreach(header IN LISTS ARG_HEADERS)
        get_filename_component(absolute "${header}" ABSOLUTE)
        get_filename_component(stem "${absolute}" NAME_WLE)
        list(APPEND headers "${absolute}")
        list(APPEND outputs "${output_directory}/${stem}.reflgen.h")
    endforeach()
    set(injection "${output_directory}/reflgen_${module}.h")
    list(APPEND outputs "${injection}" "${output_directory}/reflgen_${module}.cpp")

    # 컴파일에 쓰는 include 경로·정의·표준을 생성기의 파싱에도 그대로 준다. 목록은 generator
    # expression 이라 구성(configure) 뒤에야 정해지므로 파일로 넘긴다. 표준은 CXX_STANDARD 와
    # compile features(cxx_std_NN) 양쪽에서 온다 — 생성기가 그중 가장 높은 것을 쓴다.
    set(arguments_file "${output_directory}/reflgen_${module}.args")
    set(include_directories "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")
    set(definitions "$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>")
    set(standard "$<TARGET_PROPERTY:${target},CXX_STANDARD>")
    set(features "$<TARGET_PROPERTY:${target},COMPILE_FEATURES>")
    file(GENERATE OUTPUT "${arguments_file}" CONTENT
"$<$<BOOL:${include_directories}>:-I$<JOIN:${include_directories},\n-I>\n>\
$<$<BOOL:${definitions}>:-D$<JOIN:${definitions},\n-D>\n>\
$<$<BOOL:${standard}>:-std=c++${standard}\n>\
$<$<IN_LIST:cxx_std_20,${features}>:-std=c++20\n>\
$<$<IN_LIST:cxx_std_23,${features}>:-std=c++23\n>\
$<$<IN_LIST:cxx_std_26,${features}>:-std=c++26\n>")

    set(scope_arguments)
    foreach(scope IN LISTS ARG_ATTRIBUTE_SCOPES)
        list(APPEND scope_arguments --attribute-scope "${scope}")
    endforeach()

    # HEADERS 가 include 하는 파일(상수·부모 클래스를 담은 공용 header 등)은 생성기가 depfile 로 알린다.
    set(depfile "${output_directory}/reflgen_${module}.d")
    add_custom_command(
        OUTPUT ${outputs}
        COMMAND "${generator}" --module "${module}" --output "${output_directory}" --clang-args-file "${arguments_file}"
                --depfile "${depfile}" ${scope_arguments} ${headers}
        DEPENDS ${headers} "${arguments_file}" ${generator_dependency}
        DEPFILE "${depfile}"
        COMMENT "reflgen: generating reflection for ${target}"
        VERBATIM)

    target_sources(${target} PRIVATE ${outputs})
    # [[reflgen::…]] 는 컴파일러에게 모르는 attribute 다 — 경고(/W4·-Werror 에서 오류)를 끈다.
    # 이 header 를 쓰는 모든 target 에 필요하므로 PUBLIC 이다. 주입 header 도 같은 이유로 PUBLIC 이다 —
    # header 가 생성 파일을 include 하지 않으므로 reflection 은 강제 include 로만 소비자에게 간다.
    # -include 는 붙여 쓴다 — 떼어 쓰면 CMake 가 같은 옵션 조각을 하나로 합쳐 버린다.
    target_compile_options(${target} PUBLIC
        "$<$<CXX_COMPILER_ID:MSVC>:/wd5030>"
        "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unknown-attributes>"
        "$<$<CXX_COMPILER_ID:GNU>:-Wno-attributes>"
        "$<BUILD_INTERFACE:$<IF:$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>,/FI${injection},-include${injection}>>")
endfunction()

cmake_policy(POP)

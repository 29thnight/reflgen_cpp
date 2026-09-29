# reflgen_generate — target 의 header 들에서 [[reflgen::…]] 를 읽어 reflection 코드를 만든다.
#
#   reflgen_generate(my_target
#       HEADERS include/game/player.h include/game/item.h
#       [MODULE game]                      # 등록 함수 이름: reflgen::generated::register_game()
#       [ATTRIBUTE_SCOPES game editor]     # reflgen 외에 스키마로 옮길 attribute 이름공간
#       [ATTRIBUTE_HEADERS include/game/attributes.h] # 그 이름공간의 attribute 타입 정의 — 주입 header 가 include
#       [REGISTRATION_HEADERS include/game/serializers.h] # 등록 함수가 맨 앞에서 include(아래)
#       [OUTPUT_DIRECTORY dir]             # 구성(config)마다 그 아래 <config>/ 에 만든다
#       [NO_REGISTRATION])                 # 등록 함수를 컴파일하지 않는다 — 서술만 쓴다(아래)
#
# NO_REGISTRATION: 등록 함수(reflgen_<module>.cpp)를 만들되 컴파일하지 않는다. 서술(schema_of·for_each_field)만 쓰고
# reflgen 의 등록소·직렬화는 쓰지 않는 코드베이스(자기 직렬화·등록을 가진 엔진)용이다 — 쓰지 않을 런타임 서술자의
# 컴파일 시간을 치르지 않는다.
#
# REGISTRATION_HEADERS: 등록 함수(reflgen_<module>.cpp)가 서술자를 만들기 전에 include 하는 header. 등록소의 서술자는
# 등록 함수의 번역 단위에서 만들어지고, 그 번역 단위는 HEADERS 만 본다 — 다른 번역 단위와 같은 서술이 되려면 그곳도
# 같은 것을 보아야 한다. 둘이 여기 들어간다:
#   - 반영 타입의 header 가 include 하지 않는 곳에 둔 reflgen::serializer 특수화(보지 못하면 그 필드는 직렬화기 없이
#     남는다).
#   - 반영 타입의 header 가 전방 선언만 하는 서술된 필드 타입(std::shared_ptr<Material> 등)의 header(불완전하면 그
#     타입의 서술이 실체화되며 컴파일이 멈춘다).
# 반영 클래스가 없는 모듈의 등록 함수는 include 하지 않는다.
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
    cmake_parse_arguments(PARSE_ARGV 1 ARG "NO_REGISTRATION" "MODULE;OUTPUT_DIRECTORY"
                          "HEADERS;ATTRIBUTE_SCOPES;ATTRIBUTE_HEADERS;REGISTRATION_HEADERS")
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
    set(module_source "${output_directory}/reflgen_${module}.cpp")
    list(APPEND outputs "${injection}" "${module_source}")

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
    # 사용자 attribute 타입의 정의 — 생성 header 는 원본 header 를 include 하지 않으므로 주입 header 가 이것을 include
    # 해서 attribute 타입을 보게 한다. 없으면 사용자 attribute 를 쓰는 header 는 생성물이 원본을 include 한다.
    foreach(attribute_header IN LISTS ARG_ATTRIBUTE_HEADERS)
        get_filename_component(attribute_header "${attribute_header}" ABSOLUTE)
        list(APPEND scope_arguments --attribute-header "${attribute_header}")
    endforeach()
    foreach(registration_header IN LISTS ARG_REGISTRATION_HEADERS)
        get_filename_component(registration_header "${registration_header}" ABSOLUTE)
        list(APPEND scope_arguments --registration-header "${registration_header}")
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

    # NO_REGISTRATION 이면 등록 함수만 target 에서 뺀다 — 생성은 그대로 돈다(다른 출력이 target 에 있다).
    set(compiled_outputs ${outputs})
    if(ARG_NO_REGISTRATION)
        list(REMOVE_ITEM compiled_outputs "${module_source}")
    endif()
    target_sources(${target} PRIVATE ${compiled_outputs})
    # [[reflgen::…]] 는 컴파일러에게 모르는 attribute 다 — 경고(/W4·-Werror 에서 오류)를 끈다.
    # 이 header 를 쓰는 모든 target 에 필요하므로 PUBLIC 이다. 주입 header 도 같은 이유로 PUBLIC 이다 —
    # header 가 생성 파일을 include 하지 않으므로 reflection 은 강제 include 로만 소비자에게 간다.
    # -include 는 붙여 쓴다 — 떼어 쓰면 CMake 가 같은 옵션 조각을 하나로 합쳐 버린다.
    # 모두 C++ 번역 단위에만 붙인다 — 같은 target 의 C 소스(서드파티 C 코드 등)가 C++ header 를 받으면 깨진다.
    target_compile_options(${target} PUBLIC
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/wd5030>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:Clang,AppleClang>>:-Wno-unknown-attributes>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:-Wno-attributes>"
        "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:$<IF:$<STREQUAL:$<CXX_COMPILER_FRONTEND_VARIANT>,MSVC>,/FI${injection},-include${injection}>>>")
endfunction()

cmake_policy(POP)

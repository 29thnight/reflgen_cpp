# reflgen_generate — target 의 header 들에서 [[reflgen::…]] 를 읽어 reflection 코드를 만든다.
#
#   reflgen_generate(my_target
#       HEADERS include/game/player.h include/game/item.h
#       [MODULE game]                      # 등록 함수 이름: reflgen::generated::register_game()
#       [ATTRIBUTE_SCOPES game editor]     # reflgen 외에 스키마로 옮길 attribute 이름공간
#       [ATTRIBUTE_HEADERS include/game/attributes.h] # 그 이름공간의 attribute 타입 정의 — 주입 header 가 include
#       [REGISTRATION_HEADERS include/game/serializers.h] # 등록 함수가 맨 앞에서 include(아래)
#       [OUTPUT_DIRECTORY dir]             # 구성(config)마다 그 아래 <config>/ 에 만든다
#       [DECLARATIONS_JSON file]           # 선택 선언 목록(JSON). 상대 경로는 구성별 출력 디렉터리 기준
#       [INTEROP]                          # 선택 C ABI·C# 바인딩을 만들고 C++ 구현을 컴파일
#       [INTEROP_LIBRARY name]             # C# 에서 불러올 native 라이브러리 이름(기본: module)
#       [INTEROP_NAMESPACE name]           # C# 이름공간(기본: Reflgen.Generated)
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
# DECLARATIONS_JSON: 다른 생성기가 읽을 버전이 붙은 선언 목록. 내용이 같으면 다시 쓰지 않는다.
# REFLGEN_DECLARATIONS_JSON target 속성으로 풀린 경로를 얻는다($<CONFIG> 포함 가능). 출력 디렉터리 안에만
# 둘 수 있고, 절대 경로에도 구성마다 나뉘도록 $<CONFIG> 를 넣는다. 기존 reflection 생성·주입은 그대로다.
#
# INTEROP: reflgen_<module>_interop.h/.cpp/.cs 를 만든다. C++ 구현은 NO_REGISTRATION 과 무관하게
# 컴파일하고 C# 파일은 컴파일하지 않는다. REFLGEN_INTEROP_CSHARP target 속성으로 C# 경로를 얻는다.
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
    cmake_parse_arguments(PARSE_ARGV 1 ARG "NO_REGISTRATION;INTEROP"
                          "MODULE;OUTPUT_DIRECTORY;DECLARATIONS_JSON;INTEROP_LIBRARY;INTEROP_NAMESPACE"
                          "HEADERS;ATTRIBUTE_SCOPES;ATTRIBUTE_HEADERS;REGISTRATION_HEADERS")
    foreach(argument IN ITEMS DECLARATIONS_JSON INTEROP_LIBRARY INTEROP_NAMESPACE)
        if(argument IN_LIST ARG_KEYWORDS_MISSING_VALUES)
            message(FATAL_ERROR "reflgen_generate(${target}): ${argument} requires a value")
        endif()
    endforeach()
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
    set(reflection_outputs ${outputs})
    set(optional_outputs)

    set(declaration_arguments)
    if(ARG_DECLARATIONS_JSON)
        if(IS_ABSOLUTE "${ARG_DECLARATIONS_JSON}")
            set(declarations_json "${ARG_DECLARATIONS_JSON}")
        else()
            set(declarations_json "${output_directory}/${ARG_DECLARATIONS_JSON}")
        endif()
        get_filename_component(declarations_json "${declarations_json}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(is_multi_config AND NOT declarations_json MATCHES "\\$<CONFIG>")
            message(FATAL_ERROR "reflgen_generate(${target}): DECLARATIONS_JSON must retain $<CONFIG> "
                                "in a multi-config build; use a relative file name or a config-specific absolute path")
        endif()
        list(APPEND outputs "${declarations_json}")
        list(APPEND optional_outputs "${declarations_json}")
        list(APPEND declaration_arguments --declarations-json "${declarations_json}")
        set_property(TARGET ${target} PROPERTY REFLGEN_DECLARATIONS_JSON "${declarations_json}")
        # 생성·의존성·clean 에는 참여하되 파일 확장자에 관계없이 컴파일하지 않는다.
        set_source_files_properties("${declarations_json}" PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()

    set(interop_arguments)
    if(ARG_INTEROP)
        get_filename_component(interop_header "${output_directory}/reflgen_${module}_interop.h" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        get_filename_component(interop_source "${output_directory}/reflgen_${module}_interop.cpp" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        get_filename_component(interop_csharp "${output_directory}/reflgen_${module}_interop.cs" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        list(APPEND outputs "${interop_header}" "${interop_source}" "${interop_csharp}")
        list(APPEND optional_outputs "${interop_header}" "${interop_source}" "${interop_csharp}")
        list(APPEND interop_arguments --interop)
        if(ARG_INTEROP_LIBRARY)
            list(APPEND interop_arguments --interop-library "${ARG_INTEROP_LIBRARY}")
        endif()
        if(ARG_INTEROP_NAMESPACE)
            list(APPEND interop_arguments --interop-namespace "${ARG_INTEROP_NAMESPACE}")
        endif()
        set_property(TARGET ${target} PROPERTY REFLGEN_INTEROP_CSHARP "${interop_csharp}")
        set_source_files_properties("${interop_csharp}" PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()

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
    set(ownership_command)
    set(ownership_file)
    set(ownership_template)
    if(optional_outputs)
        # clean 이 아직 생성하지 않은 사용자 파일을 지우지 않게 한다. 선택 출력은 생성 디렉터리 안에만 두고,
        # 기존 파일은 이전 생성이 성공한 뒤 쓴 소유 목록에 있는 것만 받는다(구성별로 검사한다).
        get_filename_component(optional_root "${output_directory}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        set(ownership_file "${optional_root}/reflgen_${module}.optional.outputs")
        set(ownership_template "${ownership_file}.in")
        get_target_property(existing_sources ${target} SOURCES)
        set(reserved_files ${headers} ${ARG_ATTRIBUTE_HEADERS} ${ARG_REGISTRATION_HEADERS})
        set(absolute_reserved_files)
        foreach(reserved IN LISTS reserved_files)
            get_filename_component(reserved "${reserved}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            list(APPEND absolute_reserved_files "${reserved}")
        endforeach()
        if(existing_sources)
            get_target_property(target_source_directory ${target} SOURCE_DIR)
            foreach(source IN LISTS existing_sources)
                get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${target_source_directory}")
                list(APPEND absolute_reserved_files "${source}")
            endforeach()
        endif()
        set(input_files ${absolute_reserved_files})
        list(APPEND absolute_reserved_files ${reflection_outputs} "${arguments_file}" "${depfile}"
             "${output_directory}/reflgen_${module}.attributes.tsv" "${ownership_file}" "${ownership_template}")
        get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(is_multi_config)
            set(configurations ${CMAKE_CONFIGURATION_TYPES})
        else()
            set(configurations single_config)
        endif()
        foreach(configuration IN LISTS configurations)
            if(NOT is_multi_config)
                set(configuration "${CMAKE_BUILD_TYPE}")
            endif()
            string(REPLACE "$<CONFIG>" "${configuration}" configured_root "${optional_root}")
            string(REPLACE "$<CONFIG>" "${configuration}" configured_ownership "${ownership_file}")
            get_filename_component(configured_root "${configured_root}" ABSOLUTE)
            get_filename_component(configured_ownership "${configured_ownership}" ABSOLUTE)
            set(owned_outputs)
            set(seen_outputs)
            foreach(helper IN ITEMS "${configured_ownership}" "${configured_ownership}.in")
                foreach(input IN LISTS input_files)
                    string(REPLACE "$<CONFIG>" "${configuration}" configured_input "${input}")
                    get_filename_component(configured_input "${configured_input}" ABSOLUTE)
                    if(helper STREQUAL configured_input)
                        message(FATAL_ERROR "reflgen_generate(${target}): ownership file collides with an input: ${helper}")
                    endif()
                endforeach()
                if(EXISTS "${helper}" OR IS_SYMLINK "${helper}")
                    file(STRINGS "${helper}" signature LIMIT_COUNT 1 ENCODING UTF-8)
                    if(NOT signature STREQUAL "reflgen.optional.outputs.v1")
                        message(FATAL_ERROR "reflgen_generate(${target}): refusing an existing unowned bookkeeping file: ${helper}")
                    endif()
                endif()
            endforeach()
            if(EXISTS "${configured_ownership}")
                file(STRINGS "${configured_ownership}" ownership_lines ENCODING UTF-8)
                foreach(owned IN LISTS ownership_lines)
                    if(IS_ABSOLUTE "${owned}")
                        get_filename_component(owned "${owned}" ABSOLUTE)
                        list(APPEND owned_outputs "${owned}")
                    endif()
                endforeach()
            endif()
            foreach(optional_output IN LISTS optional_outputs)
                string(REPLACE "$<CONFIG>" "${configuration}" candidate "${optional_output}")
                get_filename_component(candidate "${candidate}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
                if(candidate IN_LIST seen_outputs)
                    message(FATAL_ERROR "reflgen_generate(${target}): optional outputs collide: ${candidate}")
                endif()
                list(APPEND seen_outputs "${candidate}")
                cmake_path(IS_PREFIX configured_root "${candidate}" NORMALIZE inside_output)
                if(NOT inside_output OR candidate STREQUAL configured_root OR candidate MATCHES "\\$<")
                    message(FATAL_ERROR "reflgen_generate(${target}): optional outputs must stay inside "
                                        "the configuration-specific output directory (only $<CONFIG> is supported): ${candidate}")
                endif()
                foreach(reserved IN LISTS absolute_reserved_files)
                    string(REPLACE "$<CONFIG>" "${configuration}" reserved "${reserved}")
                    get_filename_component(reserved "${reserved}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
                    if(candidate STREQUAL reserved)
                        message(FATAL_ERROR "reflgen_generate(${target}): optional output collides with an input "
                                            "or reserved generated file: ${candidate}")
                    endif()
                endforeach()
                if((EXISTS "${candidate}" OR IS_SYMLINK "${candidate}") AND NOT candidate IN_LIST owned_outputs)
                    message(FATAL_ERROR "reflgen_generate(${target}): refusing an existing unowned optional output: "
                                        "${candidate}; choose a new output path or remove it only if it is a stale generated file")
                endif()
            endforeach()
        endforeach()
        # Make 도 command 문자열만 바뀐 경우를 잡도록 옵션을 의존 파일에 남긴다.
        string(SHA256 optional_options_hash "${declaration_arguments};${interop_arguments}")
        file(GENERATE OUTPUT "${ownership_template}" CONTENT
             "reflgen.optional.outputs.v1\n$<JOIN:${optional_outputs},\n>\n# options: ${optional_options_hash}\n")
        list(APPEND ownership_command COMMAND "${CMAKE_COMMAND}" -E copy_if_different
             "${ownership_template}" "${ownership_file}")
    endif()
    add_custom_command(
        OUTPUT ${outputs}
        BYPRODUCTS ${ownership_file}
        COMMAND "${generator}" --module "${module}" --output "${output_directory}" --clang-args-file "${arguments_file}"
                --depfile "${depfile}" ${declaration_arguments} ${interop_arguments} ${scope_arguments} ${headers}
        ${ownership_command}
        DEPENDS ${headers} "${arguments_file}" ${generator_dependency} ${ownership_template}
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

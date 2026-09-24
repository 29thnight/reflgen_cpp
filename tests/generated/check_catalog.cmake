# attribute 카탈로그 시험 — game_types.h 에서 생성된 reflgen_generated_tests.attributes.tsv 가 편집기에 내보낼
# 것만 담는가.
#
#   cmake -DCATALOG=<…/reflgen_generated_tests.attributes.tsv> -P check_catalog.cmake

if(NOT EXISTS "${CATALOG}")
    message(FATAL_ERROR "the catalog ${CATALOG} was not generated")
endif()
file(STRINGS "${CATALOG}" lines ENCODING UTF-8)
list(GET lines 0 header)
if(NOT header STREQUAL "reflgen-attributes\t1")
    message(FATAL_ERROR "unexpected catalog header '${header}'")
endif()

set(expected
    "directive\treflgen\treflect\t"
    "directive\treflgen\tignore\t"
    "directive\treflgen\tattribute\t"
    "attribute\treflgen\trange\trange(T min_value, T max_value)\t"
    "attribute\treflgen\ttransient\ttransient\t"
    # [[reflgen::attribute]] 를 단 사용자 attribute — 생성자 시그니처가 따라온다.
    "attribute\tgenerated_tests\ttooltip\ttooltip(std::string_view value)\t")
foreach(prefix IN LISTS expected)
    set(found FALSE)
    foreach(line IN LISTS lines)
        string(FIND "${line}" "${prefix}" position)
        if(position EQUAL 0)
            set(found TRUE)
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "the catalog has no line starting with '${prefix}':\n${lines}")
    endif()
endforeach()

# 표지를 쓴 이름공간의 나머지 타입(반영하는 데이터 타입 포함)과 reflgen 의 attribute 아닌 타입은 없어야 한다.
foreach(name stats hero entity limits plain)
    foreach(line IN LISTS lines)
        if(line MATCHES "^attribute\tgenerated_tests\t${name}\t")
            message(FATAL_ERROR "'generated_tests::${name}' is not an attribute but is in the catalog")
        endif()
    endforeach()
endforeach()
foreach(line IN LISTS lines)
    if(line MATCHES "^attribute\treflgen\t(attribute_ref|attribute_list|registry)\t")
        message(FATAL_ERROR "a non-attribute reflgen type is in the catalog: ${line}")
    endif()
endforeach()
message(STATUS "attribute catalog: ok")

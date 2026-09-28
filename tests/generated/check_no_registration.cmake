# NO_REGISTRATION 시험 — reflgen_description_only target 의 소스에 생성물(주입 header)은 붙고 등록 함수
# (reflgen_description_only.cpp)는 빠졌는가. 빌드가 되는 것만으로는 모른다 — 등록 함수도 컴파일된다.
#
#   cmake -DSOURCES=<…/description_only_sources_<CONFIG>.txt> -P check_no_registration.cmake

if(NOT EXISTS "${SOURCES}")
    message(FATAL_ERROR "the source list ${SOURCES} was not generated")
endif()
file(STRINGS "${SOURCES}" sources)
set(injection FALSE)
set(registration FALSE)
foreach(source IN LISTS sources)
    get_filename_component(name "${source}" NAME)
    if(name STREQUAL "reflgen_description_only.h")
        set(injection TRUE)
    elseif(name STREQUAL "reflgen_description_only.cpp")
        set(registration TRUE)
    endif()
endforeach()
if(NOT injection)
    message(FATAL_ERROR "the generated injection header is not attached to the target:\n${sources}")
endif()
if(registration)
    message(FATAL_ERROR "the register function is compiled despite NO_REGISTRATION:\n${sources}")
endif()

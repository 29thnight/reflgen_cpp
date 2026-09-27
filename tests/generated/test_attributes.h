#pragma once
// 사용자 attribute 타입 — ATTRIBUTE_HEADERS 로 주입 header 가 include 한다. 생성 header 는 원본 header 를 include
// 하지 않으므로 생성 코드가 사용자 attribute 타입을 보는 길이 이것이다.
#include "reflgen/core/static_string.h"
#include <string_view>

namespace generated_tests
{
    // 사용자 정의 attribute — ATTRIBUTE_SCOPES 에 generated_tests 를 넣었으므로 옮겨진다. 이 이름공간에는 데이터
    // 타입도 있으므로 [[reflgen::attribute]] 로 편집기 자동완성 카탈로그에 이것만 내보낸다.
    // 문자열은 static_string 에 담는다 — 구조적 타입이라 C++26 주석 값으로도 그대로 쓰인다.
    struct [[reflgen::attribute]] tooltip
    {
        reflgen::static_string text;

        constexpr explicit tooltip(std::string_view value) : text(value) {}
    };
} // namespace generated_tests

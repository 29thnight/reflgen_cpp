#pragma once
// 생성기 지시어의 타입 — reflect, ignore, attribute.
//
// C++20/23 에서는 생성기가 [[reflgen::reflect]]·[[reflgen::ignore]]·[[reflgen::attribute]] 를 지시어로 읽는다:
// 반영 대상을 고르고 멤버를 빼고 자동완성에 내보낼 attribute 를 표시할 뿐, 스키마에는 옮기지 않는다.
// 이 타입들은 C++26 주석 문법의 값이 되려고 있다 — [[=reflgen::reflect("game.player")]], [[=reflgen::ignore{}]].
// 그때 네이티브 백엔드가 annotations_of 로 같은 일을 한다. 속성이 아니므로 .with() 에 붙일 수 없다.
//
// attributes.h 가 아니라 여기 두는 이유: 생성기의 자동완성 카탈로그는 attributes.h 의 구조체를 attribute 로
// 내보낸다. 지시어는 카탈로그에 지시어로 따로 나온다.
#include "reflgen/core/static_string.h"
#include <string_view>
#include <type_traits>

namespace reflgen
{
    // 클래스·열거형을 반영 대상으로, 메서드를 스키마에 넣을 것으로 표시한다. 클래스에 준 이름은 등록 키·다형
    // 태그가 된다(없으면 한정된 타입 이름).
    struct reflect
    {
        static_string name;

        constexpr reflect() noexcept = default;
        constexpr explicit reflect(std::string_view schema_name) noexcept : name(schema_name) {}
    };

    // 이 멤버를 반영에서 뺀다.
    struct ignore
    {
    };

    // 이 타입을 편집기 자동완성의 attribute 로 내보낸다.
    struct attribute
    {
    };

    template<class T>
    inline constexpr bool is_directive_v =
        std::is_same_v<T, reflect> || std::is_same_v<T, ignore> || std::is_same_v<T, attribute>;
} // namespace reflgen

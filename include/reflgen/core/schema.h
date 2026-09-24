#pragma once
// 서술의 단일 창구와 질의 표면.
//
//   reflgen::reflectable<T>        서술이 있는가 (클래스 안 reflect() 또는 reflection<T>)
//   reflgen::schema_of<T>          로컬 스키마의 정본 — 타입당 프로그램 전역 하나
//   reflgen::for_each_field<T>(f)  부모 우선으로 모든 필드 서술자를 돈다
//   reflgen::for_each_field(obj, f) 같은 순서로 (서술자, 멤버 참조)를 돈다
//
// C++26 백엔드가 들어오면 schema_of<T> 를 만드는 방법만 바뀐다(^^T 에서 읽는다).
// 질의 표면은 그대로 둔다 — 소비자가 서술의 출처를 모르게 하는 것이 이 파일의 목적이다.
#include "reflgen/core/descriptor.h"
#include "reflgen/core/hook.h"
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace reflgen
{
template<class T>
concept reflectable = std::is_class_v<T> && (detail::has_member_reflection<T> || detail::has_external_reflection<T>);

namespace detail
{
template<class>
inline constexpr bool dependent_false = false;

// 부모의 reflect() 가 상속으로 보이는 경우를 가려낸다. 그대로 쓰면 파생 타입이
// 부모의 스키마로 조용히 직렬화된다 — 필드가 사라지는데 아무것도 붉어지지 않는다.
template<class T>
consteval bool member_reflection_is_own() noexcept
{
    if constexpr (has_member_reflection<T>)
    {
        using result = decltype(access::reflect<T>());
        static_assert(is_type_schema<result>::value, "reflect() must return reflgen::schema<T>(...)");
        return std::is_same_v<typename result::type, T>;
    }
    else
    {
        return false;
    }
}

template<class T>
consteval auto resolve_schema()
{
    constexpr bool own = member_reflection_is_own<T>();
    static_assert(!(own && has_external_reflection<T>),
                  "T is described twice: both an in-class reflect() and a reflgen::reflection<T> specialization exist");

    if constexpr (own)
    {
        return access::reflect<T>();
    }
    else if constexpr (has_external_reflection<T>)
    {
        using result = std::remove_cvref_t<decltype(reflection<T>::value)>;
        static_assert(is_type_schema<result>::value, "reflgen::reflection<T>::value must be reflgen::schema<T>(...)");
        static_assert(std::is_same_v<typename result::type, T>,
                      "reflgen::reflection<T>::value describes a different type than T");
        return reflection<T>::value;
    }
    else
    {
        static_assert(dependent_false<T>, "T inherits reflect() from a base class but does not declare its own; "
                                          "declare static consteval auto reflect() in T (or specialize reflection<T>)");
    }
}
} // namespace detail

// 런타임 소비자(직렬화, 런타임 서술자)는 모두 이 변수를 참조로 읽는다 — 소비처마다
// 정적 사본을 두지 않는다. 속성 값의 주소가 곧 런타임 속성 표의 원소가 되므로
// 정본이 하나여야 한다.
template<reflectable T>
inline constexpr auto schema_of = detail::resolve_schema<T>();

template<reflectable T>
using schema_type_t = std::remove_cvref_t<decltype(schema_of<T>)>;

template<reflectable T>
using direct_bases_t = typename schema_type_t<T>::base_types;

namespace detail
{
// 서술 없는 부모(base<B> 로 적었지만 B 에 서술이 없는 경우)는 필드 몫이 없다 —
// 업캐스트 경로에만 쓰인다.
template<class T, class F>
constexpr void visit_fields(F& function)
{
    if constexpr (reflectable<T>)
    {
        [&]<class... Bases>(type_list<Bases...>) { (visit_fields<Bases>(function), ...); }(direct_bases_t<T>{});
        std::apply([&](const auto&... fields) { (function(fields), ...); }, schema_of<T>.fields);
    }
}

template<class T, class F>
constexpr void visit_methods(F& function)
{
    if constexpr (reflectable<T>)
    {
        [&]<class... Bases>(type_list<Bases...>) { (visit_methods<Bases>(function), ...); }(direct_bases_t<T>{});
        std::apply([&](const auto&... methods) { (function(methods), ...); }, schema_of<T>.methods);
    }
}
} // namespace detail

// 부모 우선 — base<> 선언 순서대로 부모의 필드를 먼저, 그다음 자기 필드. 직렬화 키
// 순서와 편집기 표시 순서가 선언 계층을 따르게 하려는 것이다. 다이아몬드 상속의
// 공통 조상은 경로마다 한 번씩 나온다.
template<class T, class F>
constexpr void for_each_field(F&& function)
{
    detail::visit_fields<T>(function);
}

template<class Object, class F>
    requires reflectable<std::remove_cvref_t<Object>>
constexpr void for_each_field(Object&& object, F&& function)
{
    auto visitor = [&](const auto& field) { function(field, object.*field.pointer); };
    detail::visit_fields<std::remove_cvref_t<Object>>(visitor);
}

template<class T, class F>
constexpr void for_each_method(F&& function)
{
    detail::visit_methods<T>(function);
}

// 부모 몫을 포함한 필드 수.
template<class T>
consteval std::size_t field_count() noexcept
{
    std::size_t count = 0;
    for_each_field<T>([&](const auto&) { ++count; });
    return count;
}
} // namespace reflgen

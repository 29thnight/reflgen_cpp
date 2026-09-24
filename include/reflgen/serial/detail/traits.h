#pragma once
// 직렬화 범주 판정. 한 타입이 여러 모양을 동시에 가질 수 있다 — std::string 은
// 범위이기도 하고, std::filesystem::path 도 범위이며, C++26 의 std::optional 은
// 범위가 된다. 그래서 개념별 부분 특수화로 가르지 않고, 우선순위를 한 곳(category_of)
// 에 적어 모호함이 생길 여지를 없앴다. 사용자 특수화(serializer<T>)는 언제나 먼저다.
#include "reflgen/core/schema.h"
#include "reflgen/serial/detail/default_container_traits.h"
#include "reflgen/serial/detail/shape.h"
#include "reflgen/serial/fwd.h"
#include <atomic>
#include <complex>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace reflgen::detail
{
template<class T>
concept has_custom_serializer = !requires { typename serializer<T>::primary_template; };

enum class value_category : unsigned char
{
    unsupported,
    custom,
    boolean,
    character,
    byte,
    integer,
    floating,
    enumeration,
    null,
    object,
    string,
    string_view,
    c_string,
    char_array,
    optional,
    pointer,
    variant,
    complex,
    bitset,
    duration,
    time_point,
    path,
    adapter,
    atomic,
    reference,
    bytes,
    container,   // 맵·시퀀스 — container_traits (사용자 특수화 또는 기본)
    fixed_range, // std::array · C 배열 · std::span
    range,       // 읽기 규약을 모르는 범위(뷰 등) — 쓰기만 된다
    tuple,
};

// 우선순위가 곧 의미다. 순서를 바꾸기 전에 머리말의 "여러 모양" 예를 다시 볼 것.
template<class T>
consteval value_category category_of() noexcept
{
    using category = value_category;
    if constexpr (has_custom_serializer<T>)
    {
        return category::custom;
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        return category::boolean;
    }
    else if constexpr (character<T>)
    {
        return category::character;
    }
    else if constexpr (std::is_same_v<T, std::byte>)
    {
        return category::byte;
    }
    else if constexpr (std::is_integral_v<T>)
    {
        return category::integer;
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        return category::floating;
    }
    else if constexpr (std::is_enum_v<T>)
    {
        return category::enumeration;
    }
    else if constexpr (std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::monostate>)
    {
        return category::null;
    }
    else if constexpr (reflectable<T>)
    {
        return category::object;
    }
    else if constexpr (is_specialization_of_v<T, std::basic_string>)
    {
        return category::string;
    }
    else if constexpr (is_specialization_of_v<T, std::basic_string_view>)
    {
        return category::string_view;
    }
    else if constexpr (c_string<T>)
    {
        return category::c_string;
    }
    else if constexpr (char_array<T>)
    {
        return category::char_array;
    }
    else if constexpr (is_specialization_of_v<T, std::optional>)
    {
        return category::optional;
    }
    else if constexpr (smart_pointer<T>)
    {
        return category::pointer;
    }
    else if constexpr (is_specialization_of_v<T, std::variant>)
    {
        return category::variant;
    }
    else if constexpr (is_specialization_of_v<T, std::complex>)
    {
        return category::complex;
    }
    else if constexpr (is_bitset_v<T>)
    {
        return category::bitset;
    }
    else if constexpr (is_duration_v<T>)
    {
        return category::duration;
    }
    else if constexpr (is_time_point_v<T>)
    {
        return category::time_point;
    }
    else if constexpr (std::is_same_v<T, std::filesystem::path>)
    {
        return category::path;
    }
    else if constexpr (has_user_container_traits<T>)
    {
        return category::container;
    }
    else if constexpr (container_adapter<T>)
    {
        return category::adapter;
    }
    else if constexpr (is_specialization_of_v<T, std::atomic>)
    {
        return category::atomic;
    }
    else if constexpr (is_specialization_of_v<T, std::reference_wrapper>)
    {
        return category::reference;
    }
    else if constexpr (byte_blob<T>)
    {
        return category::bytes;
    }
    else if constexpr (has_default_container_traits<T>)
    {
        return category::container;
    }
    else if constexpr (std::ranges::input_range<const T> && fixed_size_range<T>)
    {
        return category::fixed_range;
    }
    else if constexpr (std::ranges::input_range<const T>)
    {
        return category::range;
    }
    else if constexpr (tuple_like<T>)
    {
        return category::tuple;
    }
    else
    {
        return category::unsupported;
    }
}

template<class Tuple, class Predicate>
consteval bool all_tuple_elements(Predicate predicate) noexcept
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (predicate(std::type_identity<std::tuple_element_t<Is, Tuple>>{}) && ...);
    }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template<class Variant, class Predicate>
consteval bool all_alternatives(Predicate predicate) noexcept
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return (predicate(std::type_identity<std::variant_alternative_t<Is, Variant>>{}) && ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template<class T>
consteval bool is_serializable() noexcept;

template<class T>
consteval bool is_deserializable() noexcept;

template<class C>
consteval bool container_serializable() noexcept
{
    using traits = container_traits_of<C>;
    if constexpr (traits::kind == container_kind::map)
    {
        return is_serializable<typename traits::key_type>() && is_serializable<typename traits::mapped_type>();
    }
    else
    {
        return is_serializable<typename traits::value_type>();
    }
}

template<class C>
consteval bool container_deserializable() noexcept
{
    using traits = container_traits_of<C>;
    if constexpr (traits::kind == container_kind::map)
    {
        using key = typename traits::key_type;
        using mapped = typename traits::mapped_type;
        constexpr bool key_readable = (traits::unique_keys && key_codec<key>) || is_deserializable<key>();
        return std::default_initializable<key> && key_readable && std::default_initializable<mapped> &&
               is_deserializable<mapped>();
    }
    else
    {
        using element = typename traits::value_type;
        constexpr bool fillable =
            traits_with_inserter<traits, C> || traits_with_add<traits, C> || traits_with_assign<traits, C>;
        return fillable && std::default_initializable<element> && is_deserializable<element>();
    }
}

// 쓸 수 있는가. 서술된 클래스(object)에서 재귀를 멈춘다 — 자기 자신을 담는 트리
// 타입(std::vector<std::unique_ptr<node>>)에서 판정이 끝나지 않는 것을 막는다.
// 필드 하나하나는 그 타입을 실제로 쓸 때 static_assert 가 따로 검사한다.
template<class T>
consteval bool is_serializable() noexcept
{
    using U = std::remove_cv_t<T>;
    using category = value_category;
    constexpr category kind = category_of<U>();

    if constexpr (kind == category::unsupported)
    {
        return false;
    }
    else if constexpr (kind == category::custom)
    {
        return requires(writer& out, const U& value) { serializer<U>::write(out, value); };
    }
    else if constexpr (kind == category::optional || kind == category::atomic)
    {
        return is_serializable<typename U::value_type>();
    }
    else if constexpr (kind == category::pointer)
    {
        using element = typename U::element_type;
        return !std::is_array_v<element> && (std::is_polymorphic_v<element> || is_serializable<element>());
    }
    else if constexpr (kind == category::variant)
    {
        return all_alternatives<U>([]<class A>(std::type_identity<A>) { return is_serializable<A>(); });
    }
    else if constexpr (kind == category::tuple)
    {
        return all_tuple_elements<U>([]<class E>(std::type_identity<E>) { return is_serializable<E>(); });
    }
    else if constexpr (kind == category::container)
    {
        return container_serializable<U>();
    }
    else if constexpr (kind == category::fixed_range || kind == category::range)
    {
        return is_serializable<std::ranges::range_value_t<const U>>();
    }
    else if constexpr (kind == category::adapter)
    {
        return is_serializable<typename U::container_type>();
    }
    else if constexpr (kind == category::reference)
    {
        return is_serializable<typename U::type>();
    }
    else if constexpr (kind == category::duration || kind == category::time_point)
    {
        return is_serializable<typename U::rep>();
    }
    else
    {
        return true;
    }
}

template<class T>
consteval bool is_deserializable() noexcept
{
    using category = value_category;
    if constexpr (std::is_const_v<T>)
    {
        return false;
    }
    else
    {
        constexpr category kind = category_of<T>();
        if constexpr (kind == category::unsupported || kind == category::string_view || kind == category::c_string ||
                      kind == category::range)
        {
            return false;
        }
        else if constexpr (kind == category::custom)
        {
            return requires(reader& in, T& value) { serializer<T>::read(in, value); };
        }
        else if constexpr (kind == category::optional || kind == category::atomic)
        {
            using element = typename T::value_type;
            return std::default_initializable<element> && is_deserializable<element>();
        }
        else if constexpr (kind == category::pointer)
        {
            using element = typename T::element_type;
            constexpr bool default_owner =
                std::is_same_v<T, std::unique_ptr<element>> || std::is_same_v<T, std::shared_ptr<element>>;
            if constexpr (!default_owner || std::is_array_v<element>)
            {
                return false;
            }
            else if constexpr (std::is_polymorphic_v<element>)
            {
                return std::has_virtual_destructor_v<element>;
            }
            else
            {
                return std::default_initializable<element> && is_deserializable<element>();
            }
        }
        else if constexpr (kind == category::variant)
        {
            return all_alternatives<T>(
                []<class A>(std::type_identity<A>) { return std::default_initializable<A> && is_deserializable<A>(); });
        }
        else if constexpr (kind == category::tuple)
        {
            return all_tuple_elements<T>([]<class E>(std::type_identity<E>) { return is_deserializable<E>(); });
        }
        else if constexpr (kind == category::container)
        {
            return container_deserializable<T>();
        }
        else if constexpr (kind == category::fixed_range || kind == category::bytes)
        {
            using reference = std::ranges::range_reference_t<T>;
            using element = std::ranges::range_value_t<T>;
            if constexpr (kind == category::bytes && requires(T& blob) { blob.clear(); })
            {
                return requires(T& blob, const std::byte* data) { blob.assign(data, data); };
            }
            else
            {
                return !std::is_const_v<std::remove_reference_t<reference>> && is_deserializable<element>();
            }
        }
        else if constexpr (kind == category::adapter)
        {
            return is_deserializable<typename T::container_type>();
        }
        else if constexpr (kind == category::reference)
        {
            return is_deserializable<typename T::type>();
        }
        else
        {
            return true;
        }
    }
}
} // namespace reflgen::detail

namespace reflgen
{
template<class T>
concept serializable = detail::is_serializable<T>();

template<class T>
concept deserializable = detail::is_deserializable<T>();
} // namespace reflgen

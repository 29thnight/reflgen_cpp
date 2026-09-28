#pragma once
// 직렬화 범주 판정. 한 타입이 여러 모양을 동시에 가질 수 있다 — std::string 은
// 범위이기도 하고, std::filesystem::path 도 범위이며, C++26 의 std::optional 은
// 범위가 된다. 그래서 개념별 부분 특수화로 가르지 않고, 우선순위를 한 곳(category_of)
// 에 적어 모호함이 생길 여지를 없앴다. 사용자 특수화(serializer<T>)는 언제나 먼저다.
#include "reflgen/core/attributes.h"
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

    template<class Field>
    inline constexpr bool is_transient_field = Field::template has_attribute<transient>();

    // ── 쓸 수 있는가 · 읽을 수 있는가 ───────────────────────────────────────────────────────
    // 판정은 두 깊이다.
    //   깊게(is_serializable·is_deserializable, serializable·deserializable 개념) — 서술된 클래스는 그 필드까지
    //     내려간다. 런타임 서술자는 이것으로 필드와 타입의 쓰기·읽기 썽크를 만들지 정한다 — 만들 수 없는 것은
    //     비워 두고(쓰면 런타임 오류), 등록 함수의 컴파일을 멈추지 않는다.
    //   얕게(…_shallow) — 서술된 클래스는 참으로 본다. 쓰기·읽기(write_object·read_object)의 필드별 static_assert
    //     가 쓴다 — 품은 클래스의 필드는 그 클래스를 쓸 때 따로 단정되므로 오류가 실제로 못 쓰는 필드를 가리킨다.
    // 깊은 판정은 판정 중인 서술된 클래스들(Visiting — type_list)을 들고 내려간다. 다시 만난 클래스는 참으로
    // 본다 — 그 필드는 바깥 판정이 이미 보고 있다. 그래서 자기 자신을 담는 트리 타입에서도 판정이 끝난다.
    struct shallow_check
    {
    };

    template<class List, class T>
    inline constexpr bool visiting = false;

    template<class... Ts, class T>
    inline constexpr bool visiting<type_list<Ts...>, T> = (std::is_same_v<Ts, T> || ...);

    template<class List, class T>
    struct visit;

    template<class... Ts, class T>
    struct visit<type_list<Ts...>, T>
    {
        using type = type_list<Ts..., T>;
    };

    template<class T, class Visiting>
    consteval bool serializable_in() noexcept;

    template<class T, class Visiting>
    consteval bool deserializable_in() noexcept;

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

    // 서술된 클래스 T 의 필드 중 transient 가 아닌 것 전부(부모 몫 포함). Visiting 에는 T 가 이미 들어 있다.
    template<class T, class Visiting>
    consteval bool fields_serializable_in() noexcept
    {
        bool result = true;
        for_each_field<T>([&](const auto& field) {
            using field_type = std::remove_cvref_t<decltype(field)>;
            if constexpr (!is_transient_field<field_type>)
            {
                result = result && serializable_in<typename field_type::value_type, Visiting>();
            }
        });
        return result;
    }

    template<class T, class Visiting>
    consteval bool fields_deserializable_in() noexcept
    {
        bool result = true;
        for_each_field<T>([&](const auto& field) {
            using field_type = std::remove_cvref_t<decltype(field)>;
            if constexpr (!is_transient_field<field_type>)
            {
                result = result && deserializable_in<typename field_type::value_type, Visiting>();
            }
        });
        return result;
    }

    template<class C, class Visiting>
    consteval bool container_serializable() noexcept
    {
        using traits = container_traits_of<C>;
        if constexpr (traits::kind == container_kind::map)
        {
            return serializable_in<typename traits::key_type, Visiting>() &&
                   serializable_in<typename traits::mapped_type, Visiting>();
        }
        else
        {
            return serializable_in<typename traits::value_type, Visiting>();
        }
    }

    template<class C, class Visiting>
    consteval bool container_deserializable() noexcept
    {
        using traits = container_traits_of<C>;
        if constexpr (traits::kind == container_kind::map)
        {
            using key = typename traits::key_type;
            using mapped = typename traits::mapped_type;
            constexpr bool key_readable = (traits::unique_keys && key_codec<key>) || deserializable_in<key, Visiting>();
            return std::default_initializable<key> && key_readable && std::default_initializable<mapped> &&
                   deserializable_in<mapped, Visiting>();
        }
        else
        {
            using element = typename traits::value_type;
            constexpr bool fillable =
                traits_with_inserter<traits, C> || traits_with_add<traits, C> || traits_with_assign<traits, C>;
            return fillable && std::default_initializable<element> && deserializable_in<element, Visiting>();
        }
    }

    template<class T, class Visiting>
    consteval bool serializable_in() noexcept
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
        else if constexpr (kind == category::object)
        {
            if constexpr (std::is_same_v<Visiting, shallow_check> || visiting<Visiting, U>)
            {
                return true;
            }
            else
            {
                return fields_serializable_in<U, typename visit<Visiting, U>::type>();
            }
        }
        else if constexpr (kind == category::optional || kind == category::atomic)
        {
            return serializable_in<typename U::value_type, Visiting>();
        }
        else if constexpr (kind == category::pointer)
        {
            // 다형 포인터는 동적 타입을 등록소에서 찾는다 — 정적 타입으로는 판정하지 않는다.
            using element = typename U::element_type;
            return !std::is_array_v<element> &&
                   (std::is_polymorphic_v<element> || serializable_in<element, Visiting>());
        }
        else if constexpr (kind == category::variant)
        {
            return all_alternatives<U>([]<class A>(std::type_identity<A>) { return serializable_in<A, Visiting>(); });
        }
        else if constexpr (kind == category::tuple)
        {
            return all_tuple_elements<U>([]<class E>(std::type_identity<E>) { return serializable_in<E, Visiting>(); });
        }
        else if constexpr (kind == category::container)
        {
            return container_serializable<U, Visiting>();
        }
        else if constexpr (kind == category::fixed_range || kind == category::range)
        {
            return serializable_in<std::ranges::range_value_t<const U>, Visiting>();
        }
        else if constexpr (kind == category::adapter)
        {
            return serializable_in<typename U::container_type, Visiting>();
        }
        else if constexpr (kind == category::reference)
        {
            return serializable_in<typename U::type, Visiting>();
        }
        else if constexpr (kind == category::duration || kind == category::time_point)
        {
            return serializable_in<typename U::rep, Visiting>();
        }
        else
        {
            return true;
        }
    }

    template<class T, class Visiting>
    consteval bool deserializable_in() noexcept
    {
        using category = value_category;
        if constexpr (std::is_const_v<T>)
        {
            return false;
        }
        else
        {
            constexpr category kind = category_of<T>();
            if constexpr (kind == category::unsupported || kind == category::string_view ||
                          kind == category::c_string || kind == category::range)
            {
                return false;
            }
            else if constexpr (kind == category::custom)
            {
                return requires(reader& in, T& value) { serializer<T>::read(in, value); };
            }
            else if constexpr (kind == category::object)
            {
                if constexpr (std::is_same_v<Visiting, shallow_check> || visiting<Visiting, T>)
                {
                    return true;
                }
                else
                {
                    return fields_deserializable_in<T, typename visit<Visiting, T>::type>();
                }
            }
            else if constexpr (kind == category::optional || kind == category::atomic)
            {
                using element = typename T::value_type;
                return std::default_initializable<element> && deserializable_in<element, Visiting>();
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
                    return std::default_initializable<element> && deserializable_in<element, Visiting>();
                }
            }
            else if constexpr (kind == category::variant)
            {
                return all_alternatives<T>([]<class A>(std::type_identity<A>) {
                    return std::default_initializable<A> && deserializable_in<A, Visiting>();
                });
            }
            else if constexpr (kind == category::tuple)
            {
                return all_tuple_elements<T>(
                    []<class E>(std::type_identity<E>) { return deserializable_in<E, Visiting>(); });
            }
            else if constexpr (kind == category::container)
            {
                return container_deserializable<T, Visiting>();
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
                    return !std::is_const_v<std::remove_reference_t<reference>> &&
                           deserializable_in<element, Visiting>();
                }
            }
            else if constexpr (kind == category::adapter)
            {
                return deserializable_in<typename T::container_type, Visiting>();
            }
            else if constexpr (kind == category::reference)
            {
                return deserializable_in<typename T::type, Visiting>();
            }
            else
            {
                return true;
            }
        }
    }

    template<class T>
    consteval bool is_serializable() noexcept
    {
        return serializable_in<T, type_list<>>();
    }

    template<class T>
    consteval bool is_deserializable() noexcept
    {
        return deserializable_in<T, type_list<>>();
    }

    template<class T>
    consteval bool is_serializable_shallow() noexcept
    {
        return serializable_in<T, shallow_check>();
    }

    template<class T>
    consteval bool is_deserializable_shallow() noexcept
    {
        return deserializable_in<T, shallow_check>();
    }

    // 서술된 클래스 T 의 필드가 모두 쓰일 수 있는가(깊게) — 런타임 서술자가 인스턴스 쓰기 썽크를 만들지 정한다.
    template<class T>
    consteval bool object_fields_serializable() noexcept
    {
        return fields_serializable_in<T, type_list<T>>();
    }

    template<class T>
    consteval bool object_fields_deserializable() noexcept
    {
        return fields_deserializable_in<T, type_list<T>>();
    }
} // namespace reflgen::detail

namespace reflgen
{
    template<class T>
    concept serializable = detail::is_serializable<T>();

    template<class T>
    concept deserializable = detail::is_deserializable<T>();
} // namespace reflgen

#pragma once
// 서술자 — 레시피(schema)를 이루는 항목들.
//
//   reflgen::schema<T>(
//       reflgen::base<Component>,
//       reflgen::field<&T::hp_>.named("hp").with(reflgen::range(0, 100)),
//       reflgen::method<&T::fire>.parameters("shots"))
//   .named("game.player")
//   .with(reflgen::display_name("Player"))
//
// 정체성(포인터·소유 타입·값 타입)은 타입에, 이름과 속성은 값에 든다. 값이라 괄호
// 없이 쓰고(field<&T::x>), .named/.with 가 새 값을 돌려준다(원본은 불변).
//
// 레시피는 **로컬**이다: 이 타입이 직접 선언한 필드·메서드와 직계 부모만 적는다.
// 상속 합성은 질의(for_each_field)가 계산한다.
#include "reflgen/core/name.h"
#include <array>
#include <cstddef>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace reflgen
{
    template<class... Ts>
    struct type_list
    {
        static constexpr std::size_t size = sizeof...(Ts);
    };

    namespace detail
    {
        template<class... Lists>
        struct concat_type_lists
        {
            using type = type_list<>;
        };

        template<class... As>
        struct concat_type_lists<type_list<As...>>
        {
            using type = type_list<As...>;
        };

        template<class... As, class... Bs, class... Rest>
        struct concat_type_lists<type_list<As...>, type_list<Bs...>, Rest...>
            : concat_type_lists<type_list<As..., Bs...>, Rest...>
        {
        };

        // 데이터 멤버 포인터와 멤버 함수 포인터가 같은 부분 특수화에 걸린다 —
        // 멤버 함수 포인터의 Value 는 함수 타입이다.
        template<class P>
        struct member_pointer_traits;

        template<class Value, class Owner>
        struct member_pointer_traits<Value Owner::*>
        {
            using owner_type = Owner;
            using value_type = Value;
        };

        template<class A, class... Ts>
        consteval std::size_t first_index_of() noexcept
        {
            constexpr bool matches[] = {std::is_same_v<A, Ts>..., false};
            for (std::size_t i = 0; i < sizeof...(Ts); ++i)
            {
                if (matches[i])
                {
                    return i;
                }
            }
            return sizeof...(Ts);
        }
    } // namespace detail

    template<auto Member, class... Attrs>
    struct field_descriptor
    {
        static_assert(std::is_member_object_pointer_v<decltype(Member)>,
                      "reflgen::field<> takes a pointer to a data member (&T::member)");

        using pointer_type = decltype(Member);
        using owner_type = typename detail::member_pointer_traits<pointer_type>::owner_type;
        using value_type = typename detail::member_pointer_traits<pointer_type>::value_type;

        static constexpr pointer_type pointer = Member;

        std::string_view name = member_name_of<Member>();
        std::tuple<Attrs...> attributes;

        consteval field_descriptor named(std::string_view new_name) const
        {
            return field_descriptor{new_name, attributes};
        }

        template<class... More>
        consteval field_descriptor<Member, Attrs..., More...> with(More... more) const
        {
            return {name, std::tuple_cat(attributes, std::tuple<More...>{more...})};
        }

        template<class A>
        static constexpr bool has_attribute() noexcept
        {
            return (std::is_same_v<A, Attrs> || ...);
        }

        // 같은 타입이 여럿 붙었으면 첫 번째다.
        template<class A>
        constexpr const A& attribute() const noexcept
        {
            static_assert(has_attribute<A>(),
                          "the field does not carry this attribute; check has_attribute<A>() first");
            return std::get<detail::first_index_of<A, Attrs...>()>(attributes);
        }
    };

    template<auto Member>
    inline constexpr field_descriptor<Member> field{};

    template<auto Function, std::size_t ParameterCount = 0, class... Attrs>
    struct method_descriptor
    {
        static_assert(std::is_member_function_pointer_v<decltype(Function)>,
                      "reflgen::method<> takes a pointer to a member function (&T::function)");

        using pointer_type = decltype(Function);
        using owner_type = typename detail::member_pointer_traits<pointer_type>::owner_type;

        static constexpr pointer_type pointer = Function;

        std::string_view name = member_name_of<Function>();
        std::array<std::string_view, ParameterCount> parameter_names{};
        std::tuple<Attrs...> attributes;

        consteval method_descriptor named(std::string_view new_name) const
        {
            return method_descriptor{new_name, parameter_names, attributes};
        }

        template<class... Names>
        consteval method_descriptor<Function, sizeof...(Names), Attrs...> parameters(Names... names) const
        {
            static_assert(ParameterCount == 0, "parameters() may be applied only once");
            return {name, {std::string_view{names}...}, attributes};
        }

        template<class... More>
        consteval method_descriptor<Function, ParameterCount, Attrs..., More...> with(More... more) const
        {
            return {name, parameter_names, std::tuple_cat(attributes, std::tuple<More...>{more...})};
        }

        template<class A>
        static constexpr bool has_attribute() noexcept
        {
            return (std::is_same_v<A, Attrs> || ...);
        }

        template<class A>
        constexpr const A& attribute() const noexcept
        {
            static_assert(has_attribute<A>(),
                          "the method does not carry this attribute; check has_attribute<A>() first");
            return std::get<detail::first_index_of<A, Attrs...>()>(attributes);
        }

        template<class Object, class... Args>
        constexpr decltype(auto) invoke(Object&& object, Args&&... args) const
        {
            return std::invoke(Function, std::forward<Object>(object), std::forward<Args>(args)...);
        }
    };

    template<auto Function>
    inline constexpr method_descriptor<Function> method{};

    template<class Base>
    struct base_descriptor
    {
        using type = Base;
    };

    template<class Base>
    inline constexpr base_descriptor<Base> base{};

    template<class T, class Bases, class Fields, class Methods, class... Attrs>
    struct type_schema
    {
        using type = T;
        using base_types = Bases;

        static constexpr std::size_t field_count = std::tuple_size_v<Fields>;
        static constexpr std::size_t method_count = std::tuple_size_v<Methods>;

        std::string_view name = type_name_of<T>();
        Fields fields;
        Methods methods;
        std::tuple<Attrs...> attributes;

        // 다형 태그·등록 키가 되는 이름. 컴파일러가 만든 이름은 컴파일러마다 다를 수
        // 있으므로(템플릿 타입) 파일에 남는 타입은 여기서 이름을 못 박는다.
        consteval type_schema named(std::string_view new_name) const
        {
            return type_schema{new_name, fields, methods, attributes};
        }

        template<class... More>
        consteval type_schema<T, Bases, Fields, Methods, Attrs..., More...> with(More... more) const
        {
            return {name, fields, methods, std::tuple_cat(attributes, std::tuple<More...>{more...})};
        }

        template<class A>
        static constexpr bool has_attribute() noexcept
        {
            return (std::is_same_v<A, Attrs> || ...);
        }

        template<class A>
        constexpr const A& attribute() const noexcept
        {
            static_assert(has_attribute<A>(), "the type does not carry this attribute; check has_attribute<A>() first");
            return std::get<detail::first_index_of<A, Attrs...>()>(attributes);
        }
    };

    namespace detail
    {
        template<class E>
        struct is_field_descriptor : std::false_type
        {
        };

        template<auto Member, class... Attrs>
        struct is_field_descriptor<field_descriptor<Member, Attrs...>> : std::true_type
        {
        };

        template<class E>
        struct is_method_descriptor : std::false_type
        {
        };

        template<auto Function, std::size_t N, class... Attrs>
        struct is_method_descriptor<method_descriptor<Function, N, Attrs...>> : std::true_type
        {
        };

        template<class E>
        struct is_base_descriptor : std::false_type
        {
        };

        template<class Base>
        struct is_base_descriptor<base_descriptor<Base>> : std::true_type
        {
        };

        template<class S>
        struct is_type_schema : std::false_type
        {
        };

        template<class T, class Bases, class Fields, class Methods, class... Attrs>
        struct is_type_schema<type_schema<T, Bases, Fields, Methods, Attrs...>> : std::true_type
        {
        };

        template<class E>
        struct base_list_of_entry
        {
            using type = type_list<>;
        };

        template<class Base>
        struct base_list_of_entry<base_descriptor<Base>>
        {
            using type = type_list<Base>;
        };

        // 레시피 인자에서 한 종류만 골라 순서를 보존한 튜플로 만든다.
        template<template<class> class Predicate, class... Es>
        consteval auto pick(const std::tuple<Es...>& entries)
        {
            return std::apply(
                [](const auto&... entry) {
                    return std::tuple_cat([&] {
                        if constexpr (Predicate<std::remove_cvref_t<decltype(entry)>>::value)
                        {
                            return std::tuple{entry};
                        }
                        else
                        {
                            return std::tuple<>{};
                        }
                    }()...);
                },
                entries);
        }

        template<class T, class E>
        consteval void check_entry() noexcept
        {
            if constexpr (is_field_descriptor<E>::value || is_method_descriptor<E>::value)
            {
                using owner = typename E::owner_type;
                static_assert(std::is_same_v<owner, T> || std::is_base_of_v<owner, T>,
                              "reflgen::schema<T>: the member belongs to a type that is neither T nor a base of T");
            }
            else if constexpr (is_base_descriptor<E>::value)
            {
                using base_type = typename E::type;
                static_assert(std::is_base_of_v<base_type, T> && !std::is_same_v<base_type, T>,
                              "reflgen::schema<T>: reflgen::base<B> requires B to be a base class of T");
            }
            else
            {
                static_assert(is_field_descriptor<E>::value,
                              "reflgen::schema accepts only reflgen::field, reflgen::method and reflgen::base entries; "
                              "attach type attributes with .with(...)");
            }
        }

        // 상수 평가가 이 함수들에 닿으면 컴파일이 멈춘다 — 진단에 찍히는 함수 이름이
        // 곧 오류 설명이다(consteval 안에서 문자열 메시지를 띄울 표준 수단이 없다).
        inline void member_name_unavailable_use_named() {}

        template<class Tuple>
        consteval void check_names(const Tuple& descriptors)
        {
            std::apply(
                [](const auto&... descriptor) {
                    (
                        [&] {
                            if (descriptor.name.empty())
                            {
                                member_name_unavailable_use_named();
                            }
                        }(),
                        ...);
                },
                descriptors);
        }
    } // namespace detail

    template<class T, class... Entries>
    consteval auto schema(Entries... entries)
    {
        static_assert(std::is_class_v<T>, "reflgen::schema<T>: T must be a class type");
        (detail::check_entry<T, Entries>(), ...);

        const std::tuple<Entries...> all{entries...};
        const auto fields = detail::pick<detail::is_field_descriptor>(all);
        const auto methods = detail::pick<detail::is_method_descriptor>(all);
        detail::check_names(fields);
        detail::check_names(methods);

        using bases = typename detail::concat_type_lists<typename detail::base_list_of_entry<Entries>::type...>::type;
        return type_schema<T, bases, std::remove_const_t<decltype(fields)>, std::remove_const_t<decltype(methods)>>{
            type_name_of<T>(), fields, methods, {}};
    }
} // namespace reflgen

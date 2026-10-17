#pragma once
#include "reflgen/core/features.h"
#include "reflgen/core/attributes.h"
#include "reflgen/core/descriptor.h"

#if REFLGEN_USE_STD_REFLECTION
namespace reflgen::detail
{
    template<class A, std::meta::info Item>
    consteval bool standard_has_annotation()
    {
        return !std::meta::annotations_of_with_type(Item, ^^A).empty();
    }

    template<class T>
    consteval bool standard_eligible()
    {
        if constexpr (!requires { sizeof(T); } || (!std::is_class_v<T> && !std::is_enum_v<T>)) return false;
        else return standard_has_annotation<reflect, ^^T>() && !standard_has_annotation<ignore, ^^T>() &&
                    !standard_has_annotation<attribute, ^^T>();
    }

    template<std::meta::info Annotation>
    consteval auto standard_annotation_entry()
    {
        using Raw = typename [:std::meta::type_of(Annotation):];
        using A = std::remove_cv_t<Raw>;
        if constexpr (is_directive_v<A>) return std::tuple{};
        else return std::tuple{std::meta::extract<A>(Annotation)};
    }

    template<std::meta::info Item, class Descriptor, std::size_t... I>
    consteval auto standard_with_annotations(Descriptor descriptor, std::index_sequence<I...>)
    {
        auto values = std::tuple_cat(standard_annotation_entry<std::meta::annotations_of(Item)[I]>()...);
        return std::apply([&](auto... attrs) { return descriptor.with(attrs...); }, values);
    }
    template<std::meta::info Item, class Descriptor>
    consteval auto standard_with_annotations(Descriptor descriptor)
    {
        return standard_with_annotations<Item>(descriptor, std::make_index_sequence<std::meta::annotations_of(Item).size()>{});
    }

    template<std::meta::info Member, std::size_t... I>
    consteval auto standard_method(std::index_sequence<I...>)
    {
        return method<&[:Member:]>.parameters(
            (std::meta::has_identifier(std::meta::parameters_of(Member)[I])
                ? std::meta::identifier_of(std::meta::parameters_of(Member)[I]) : std::string_view{})...)
            .named(std::meta::identifier_of(Member));
    }

    template<std::meta::info Member>
    consteval auto standard_member()
    {
        if constexpr (standard_has_annotation<ignore, Member>()) return std::tuple{};
        else if constexpr (std::meta::is_nonstatic_data_member(Member))
        {
            static_assert(!std::meta::is_bit_field(Member), "reflgen: bit-fields require ignore annotation or a manual schema");
            static_assert(!std::meta::is_reference_type(std::meta::type_of(Member)), "reflgen: reference fields require ignore annotation or a manual schema");
            return std::tuple{standard_with_annotations<Member>(field<&[:Member:]>.named(std::meta::identifier_of(Member)))};
        }
        else if constexpr (std::meta::is_function(Member) && standard_has_annotation<reflect, Member>())
        {
            static_assert(!std::meta::is_static_member(Member), "reflgen: reflected methods must be implicit-object member methods");
            return std::tuple{standard_with_annotations<Member>(standard_method<Member>(
                std::make_index_sequence<std::meta::parameters_of(Member).size()>{}))};
        }
        else return std::tuple{};
    }

    template<class T, std::size_t... I>
    consteval auto standard_schema_members(std::index_sequence<I...>)
    {
        constexpr auto context = std::meta::access_context::unprivileged();
        static_assert(std::meta::bases_of(^^T, context).empty(), "reflgen: standard backend public inheritance requires a manual schema");
        auto entries = std::tuple_cat(standard_member<std::meta::members_of(^^T, context)[I]>()...);
        auto result = std::apply([](auto... entry) { return schema<T>(entry...); }, entries);
        constexpr auto marker = std::meta::extract<reflect>(std::meta::annotations_of_with_type(^^T, ^^reflect)[0]);
        return standard_with_annotations<^^T>(result.named(marker.name.empty() ? type_name_of<T>() : marker.name.view()));
    }
    template<class T>
    consteval auto standard_schema()
    {
        return standard_schema_members<T>(std::make_index_sequence<
            std::meta::members_of(^^T, std::meta::access_context::unprivileged()).size()>{});
    }
}
#else
namespace reflgen::detail
{
    template<class T> consteval bool standard_eligible() { return false; }
}
#endif

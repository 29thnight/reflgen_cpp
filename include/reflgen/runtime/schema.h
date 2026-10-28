#pragma once
// Runtime-only descriptions are independent of the authoring/serialization schema. Adding one
// exposes native scripting metadata without opting the C++ type into reflgen::serialize or YAML.
#include "reflgen/core/schema.h"

namespace reflgen
{
    template<class T>
    struct runtime_schema;

    template<class T>
    concept runtime_reflectable = std::is_class_v<T> && (requires { runtime_schema<T>::value; } || reflectable<T>);

    template<runtime_reflectable T>
    inline constexpr auto runtime_schema_of = [] {
        if constexpr (requires { runtime_schema<T>::value; })
        {
            using result = std::remove_cvref_t<decltype(runtime_schema<T>::value)>;
            static_assert(detail::is_type_schema<result>::value, "runtime_schema<T>::value must be schema<T>(...)");
            static_assert(std::is_same_v<typename result::type, T>, "runtime_schema<T> describes a different type");
            return runtime_schema<T>::value;
        }
        else return schema_of<T>;
    }();

    template<runtime_reflectable T>
    using runtime_direct_bases_t = typename std::remove_cvref_t<decltype(runtime_schema_of<T>)>::base_types;

    template<class T, class F>
    constexpr void for_each_runtime_field(F&& function)
    {
        if constexpr (runtime_reflectable<T>)
        {
            [&]<class... Bases>(type_list<Bases...>) { (for_each_runtime_field<Bases>(function), ...); }(runtime_direct_bases_t<T>{});
            std::apply([&](const auto&... fields) { (function(fields), ...); }, runtime_schema_of<T>.fields);
        }
    }
}

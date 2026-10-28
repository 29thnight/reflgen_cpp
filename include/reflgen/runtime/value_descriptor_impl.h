#pragma once
// Included after type_descriptor is complete. All recursive edges are lazy getters.
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include "reflgen/serial/detail/container.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <variant>
#if __has_include(<expected>)
#include <expected>
#endif

namespace reflgen
{
    inline const value_descriptor& value_view::descriptor() const
    {
        if (!descriptor_) throw std::invalid_argument("empty runtime value view");
        return *descriptor_;
    }

    inline void value_view::retain_parent(const value_view& parent)
    {
        if (!parent.is_writable()) writable_ = nullptr;
        if (!parent.storage_ || parent.storage_ == storage_) return;
        if (!storage_) storage_ = parent.storage_;
        else storage_ = std::make_shared<std::pair<std::shared_ptr<const void>, std::shared_ptr<const void>>>(storage_, parent.storage_);
    }

    inline void value_descriptor::serialize(writer& out, const void* value) const
    {
        if (!value || !write_) throw std::invalid_argument("runtime value is not serializable: " + std::string(name_));
        write_(out, value);
    }

    inline void value_descriptor::deserialize(reader& in, void* value) const
    {
        if (!value || !read_) throw std::invalid_argument("runtime value is not deserializable: " + std::string(name_));
        read_(in, value);
    }

    inline void value_descriptor::assign_serialized(reader& in, void* value) const
    {
        if (!value || !assign_) throw std::invalid_argument("runtime value has no safe staged assignment: " + std::string(name_));
        assign_(in, value);
    }

    inline void value_view::serialize(writer& out) const { descriptor().serialize(out, data_); }
    inline void value_view::deserialize(reader& in) const
    {
        if (!writable_) throw std::invalid_argument("runtime value view is readonly");
        descriptor().assign_serialized(in, writable_);
    }

    inline std::size_t value_view::size() const
    {
        if (!*this || !descriptor().count_) throw std::invalid_argument("runtime value has no indexed children");
        return descriptor().count_(data_);
    }

    inline value_view value_view::select_member(std::string_view name) const
    {
        if (!*this) throw std::invalid_argument("empty runtime value view");
        const auto* object = descriptor().reflected_type();
        if (!object) throw std::invalid_argument("runtime value has no reflected members");
        const auto* field = object->find_field(name);
        if (!field) throw std::out_of_range("unknown runtime member: " + std::string(name));
        void* writable = writable_ && !field->is_read_only() && !field->attributes().contains<readonly>()
            ? field->address(writable_) : nullptr;
        return value_view(field->value(), field->address(data_), writable, storage_);
    }

    inline value_view value_view::select_index(std::size_t index) const
    {
        if (!*this || !descriptor().select_) throw std::invalid_argument("runtime value has no indexed navigation");
        if (index >= size()) throw std::out_of_range("runtime value index is out of range");
        auto child = descriptor().select_(data_, writable_, index);
        child.retain_parent(*this);
        return child;
    }

    inline value_view value_view::select_key(std::size_t index) const
    {
        if (!*this || !descriptor().key_) throw std::invalid_argument("runtime value has no map keys");
        if (index >= size()) throw std::out_of_range("runtime map index is out of range");
        auto child = descriptor().key_(data_, nullptr, index);
        child.retain_parent(*this);
        return child;
    }

    inline value_view value_view::dereference() const
    {
        if (!descriptor().can_dereference()) throw std::invalid_argument("runtime value has no permitted dereference");
        if (size() == 0) throw std::out_of_range("runtime reference is empty");
        return select_index(0);
    }

    inline std::string_view value_shape_name(value_shape shape) noexcept
    {
        switch (shape)
        {
        case value_shape::unsupported: return "unsupported";
        case value_shape::void_value: return "void";
        case value_shape::boolean: return "boolean";
        case value_shape::signed_integer: return "signed_integer";
        case value_shape::unsigned_integer: return "unsigned_integer";
        case value_shape::floating: return "floating";
        case value_shape::enumeration: return "enum";
        case value_shape::string: return "string";
        case value_shape::record: return "record";
        case value_shape::sequence: return "sequence";
        case value_shape::map: return "map";
        case value_shape::optional: return "optional";
        case value_shape::pointer: return "pointer";
        case value_shape::variant: return "variant";
        case value_shape::tuple: return "tuple";
        case value_shape::custom: return "custom";
        case value_shape::expected: return "expected";
        }
        return "unsupported";
    }

    namespace detail
    {
        template<class T> inline constexpr bool runtime_expected = false;
        template<class T> inline constexpr bool runtime_span = false;
        template<class E, std::size_t N> inline constexpr bool runtime_span<std::span<E, N>> = true;
#if defined(__cpp_lib_expected)
        template<class T, class E> inline constexpr bool runtime_expected<std::expected<T, E>> = true;
#endif
        template<class T>
        concept runtime_indexed = requires(const T& value, std::size_t index) {
            { runtime_value_traits<T>::size(value) } -> std::convertible_to<std::size_t>;
            runtime_value_traits<T>::at(value, index);
        } && std::is_lvalue_reference_v<decltype(runtime_value_traits<T>::at(std::declval<const T&>(), std::size_t{}))>;

        template<class T>
        concept runtime_keyed = runtime_indexed<T> && requires(const T& value, std::size_t index) {
            runtime_value_traits<T>::key(value, index);
        } && std::is_lvalue_reference_v<decltype(runtime_value_traits<T>::key(std::declval<const T&>(), std::size_t{}))>;

        template<class T>
        consteval bool prefers_runtime_record_fields()
        {
            if constexpr (runtime_reflectable<T> && requires { runtime_value_traits<T>::prefer_record_fields; })
                return runtime_value_traits<T>::prefer_record_fields;
            else return false;
        }

        template<class T>
        consteval value_shape runtime_shape()
        {
            if constexpr (requires { runtime_value_traits<T>::shape; }) return runtime_value_traits<T>::shape;
            else if constexpr (std::is_void_v<T> || std::is_same_v<T, std::monostate>) return value_shape::void_value;
            else if constexpr (!complete_type<T>) return value_shape::unsupported;
            else if constexpr (std::is_same_v<T, bool>) return value_shape::boolean;
            else if constexpr (std::is_integral_v<T>) return std::is_signed_v<T> ? value_shape::signed_integer : value_shape::unsigned_integer;
            else if constexpr (std::is_floating_point_v<T>) return value_shape::floating;
            else if constexpr (std::is_enum_v<T>) return value_shape::enumeration;
            else if constexpr (runtime_reflectable<T>) return value_shape::record;
            else if constexpr (is_specialization_of_v<T, std::basic_string> ||
                               is_specialization_of_v<T, std::basic_string_view> || c_string<T> || char_array<T>) return value_shape::string;
            else if constexpr (is_specialization_of_v<T, std::optional>) return value_shape::optional;
            else if constexpr (std::is_pointer_v<T> || smart_pointer<T>) return value_shape::pointer;
            else if constexpr (is_specialization_of_v<T, std::variant>) return value_shape::variant;
            else if constexpr (runtime_expected<T>) return value_shape::expected;
            else if constexpr (has_user_container_traits<T> || has_default_container_traits<T>)
                return container_traits_of<T>::kind == container_kind::map ? value_shape::map : value_shape::sequence;
            else if constexpr (std::ranges::input_range<const T>) return value_shape::sequence;
            else if constexpr (tuple_like<T>) return value_shape::tuple;
            else if constexpr (has_custom_serializer<T>) return value_shape::custom;
            else return value_shape::unsupported;
        }

        template<class T>
        consteval auto runtime_children_types()
        {
            if constexpr (requires { typename runtime_value_traits<T>::children; }) return typename runtime_value_traits<T>::children{};
            else if constexpr (!complete_type<T> || std::is_void_v<T>) return type_list<>{};
            else if constexpr (runtime_shape<T>() == value_shape::string || runtime_reflectable<T>) return type_list<>{};
            else if constexpr (std::is_pointer_v<T>) return type_list<std::remove_pointer_t<T>>{};
            else if constexpr (smart_pointer<T>) return type_list<typename T::element_type>{};
            else if constexpr (is_specialization_of_v<T, std::optional>) return type_list<typename T::value_type>{};
            else if constexpr (runtime_expected<T>) return type_list<typename T::value_type, typename T::error_type>{};
            else if constexpr (is_specialization_of_v<T, std::variant>)
                return []<std::size_t... I>(std::index_sequence<I...>) { return type_list<std::variant_alternative_t<I, T>...>{}; }(std::make_index_sequence<std::variant_size_v<T>>{});
            else if constexpr (has_user_container_traits<T> || has_default_container_traits<T>)
            {
                using traits = container_traits_of<T>;
                if constexpr (traits::kind == container_kind::map) return type_list<typename traits::key_type, typename traits::mapped_type>{};
                else return type_list<typename traits::value_type>{};
            }
            else if constexpr (std::ranges::input_range<const T>) return type_list<std::ranges::range_value_t<const T>>{};
            else if constexpr (tuple_like<T>)
                return []<std::size_t... I>(std::index_sequence<I...>) { return type_list<std::tuple_element_t<I, T>...>{}; }(std::make_index_sequence<std::tuple_size_v<T>>{});
            else return type_list<>{};
        }

        template<class T>
        consteval std::string_view runtime_child_name(std::size_t index)
        {
            if constexpr (requires { runtime_value_traits<T>::child_names; })
            {
                static_assert(runtime_value_traits<T>::child_names.size() == decltype(runtime_children_types<T>())::size,
                              "runtime_value_traits child_names must match children");
                return runtime_value_traits<T>::child_names[index];
            }
            else if constexpr (runtime_shape<T>() == value_shape::map) return index == 0 ? "key" : "value";
            else if constexpr (runtime_expected<T>) return index == 0 ? "value" : "error";
            else if constexpr (runtime_shape<T>() == value_shape::variant) return "alternative";
            else if constexpr (runtime_shape<T>() == value_shape::tuple) return "item";
            else return "element";
        }

        template<class T, class... Children, std::size_t... I>
        consteval auto make_value_children(type_list<Children...>, std::index_sequence<I...>)
        {
            return std::array<value_child_info, sizeof...(Children)>{value_child_info{
                runtime_child_name<T>(I), &value_descriptor_of<std::remove_cv_t<Children>>}...};
        }

        template<class T>
        inline constexpr auto runtime_value_children = make_value_children<T>(runtime_children_types<T>(),
            std::make_index_sequence<decltype(runtime_children_types<T>())::size>{});

        template<class T, class Visiting>
        consteval bool runtime_value_serializable()
        {
            if constexpr (!complete_type<T> || std::is_void_v<T>) return false;
            else
            {
                if constexpr (requires { runtime_value_traits<T>::serializable; })
                    if (!runtime_value_traits<T>::serializable) return false;
                if constexpr (requires(writer& out, const T& value) { runtime_value_traits<T>::write(out, value); }) return true;
                else if constexpr (runtime_expected<T>)
                    return (std::is_void_v<typename T::value_type> || runtime_value_serializable<typename T::value_type, typename visit<Visiting, T>::type>()) &&
                        runtime_value_serializable<typename T::error_type, typename visit<Visiting, T>::type>();
                else if constexpr (has_custom_serializer<T> && !prefers_runtime_record_fields<T>()) return is_serializable<T>();
                else if constexpr (visiting<Visiting, T>) return true;
                else if constexpr (runtime_reflectable<T>)
                {
                    bool result = true;
                    for_each_runtime_field<T>([&](const auto& field) {
                        using F = std::remove_cvref_t<decltype(field)>;
                        if constexpr (!is_transient_field<F>) result = result && runtime_value_serializable<std::remove_cv_t<typename F::value_type>, typename visit<Visiting, T>::type>();
                    });
                    return result;
                }
                else if constexpr (smart_pointer<T>)
                {
                    if constexpr (!complete_type<typename T::element_type>) return false;
                    else return is_serializable<T>();
                }
                else if constexpr (runtime_shape<T>() == value_shape::map &&
                                   !(has_user_container_traits<T> || has_default_container_traits<T>) && !runtime_keyed<T>) return false;
                else if constexpr (runtime_shape<T>() == value_shape::sequence &&
                                   !(has_user_container_traits<T> || has_default_container_traits<T>) &&
                                   !std::ranges::input_range<const T> && !runtime_indexed<T>) return false;
                else if constexpr (runtime_shape<T>() == value_shape::sequence || runtime_shape<T>() == value_shape::map ||
                                   runtime_shape<T>() == value_shape::optional || runtime_shape<T>() == value_shape::tuple ||
                                   runtime_shape<T>() == value_shape::variant)
                    return []<class... Children>(type_list<Children...>) {
                        return (runtime_value_serializable<std::remove_cv_t<Children>, typename visit<Visiting, T>::type>() && ...);
                    }(runtime_children_types<T>());
                else return is_serializable<T>();
            }
        }

        template<class T, class Visiting>
        consteval bool runtime_value_deserializable()
        {
            if constexpr (!complete_type<T> || std::is_void_v<T> || std::is_const_v<T>) return false;
            else
            {
                if constexpr (requires { runtime_value_traits<T>::deserializable; })
                    if (!runtime_value_traits<T>::deserializable) return false;
                if constexpr (requires(reader& in, T& value) { runtime_value_traits<T>::read(in, value); }) return true;
                else if constexpr (runtime_expected<T>)
                    return (std::is_void_v<typename T::value_type> || (std::default_initializable<typename T::value_type> && runtime_value_deserializable<typename T::value_type, typename visit<Visiting, T>::type>())) &&
                        std::default_initializable<typename T::error_type> && std::is_move_assignable_v<T> &&
                        runtime_value_deserializable<typename T::error_type, typename visit<Visiting, T>::type>();
                else if constexpr (has_custom_serializer<T> && !prefers_runtime_record_fields<T>()) return is_deserializable<T>();
                else if constexpr (runtime_indexed<T>) return false; // navigation alone cannot rebuild a container
                else if constexpr (visiting<Visiting, T>) return true;
                else if constexpr (runtime_reflectable<T>)
                {
                    bool result = true;
                    for_each_runtime_field<T>([&](const auto& field) {
                        using F = std::remove_cvref_t<decltype(field)>;
                        if constexpr (!is_transient_field<F>) result = result && runtime_value_deserializable<typename F::value_type, typename visit<Visiting, T>::type>();
                    });
                    return result;
                }
                else if constexpr (smart_pointer<T>)
                {
                    if constexpr (!complete_type<typename T::element_type>) return false;
                    else return is_deserializable<T>();
                }
                else if constexpr ((runtime_shape<T>() == value_shape::sequence || runtime_shape<T>() == value_shape::map) &&
                                   (has_user_container_traits<T> || has_default_container_traits<T>))
                {
                    using traits = container_traits_of<T>;
                    if constexpr (traits::kind == container_kind::map)
                        return std::default_initializable<typename traits::key_type> && std::default_initializable<typename traits::mapped_type> &&
                            runtime_value_deserializable<typename traits::key_type, typename visit<Visiting, T>::type>() &&
                            runtime_value_deserializable<typename traits::mapped_type, typename visit<Visiting, T>::type>();
                    else return std::default_initializable<typename traits::value_type> &&
                        (traits_with_inserter<traits, T> || traits_with_add<traits, T> || traits_with_assign<traits, T>) &&
                        runtime_value_deserializable<typename traits::value_type, typename visit<Visiting, T>::type>();
                }
                else if constexpr (is_specialization_of_v<T, std::optional>) return std::default_initializable<typename T::value_type> &&
                    runtime_value_deserializable<typename T::value_type, typename visit<Visiting, T>::type>();
                else if constexpr (is_specialization_of_v<T, std::variant>) return []<class... Children>(type_list<Children...>) {
                    return ((std::default_initializable<Children> && runtime_value_deserializable<Children, typename visit<Visiting, T>::type>()) && ...);
                }(runtime_children_types<T>());
                else if constexpr (runtime_shape<T>() == value_shape::tuple) return []<class... Children>(type_list<Children...>) {
                    return (runtime_value_deserializable<Children, typename visit<Visiting, T>::type>() && ...);
                }(runtime_children_types<T>());
                else if constexpr (runtime_shape<T>() == value_shape::sequence && std::ranges::range<T>)
                    return fixed_size_range<T> && !std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<T>>> &&
                        runtime_value_deserializable<std::ranges::range_value_t<T>, typename visit<Visiting, T>::type>();
                else return is_deserializable<T>();
            }
        }

        template<class T, class Visiting = type_list<>>
        consteval bool runtime_assignment_safe()
        {
            if constexpr (std::is_void_v<T>) return true;
            else if constexpr (!complete_type<T> || std::is_const_v<T>) return false;
            else if constexpr (requires { runtime_value_traits<T>::assignment_safe; }) return runtime_value_traits<T>::assignment_safe;
            else if constexpr (visiting<Visiting, T>) return true;
            else if constexpr (std::ranges::view<T>) return false; // copying a view does not copy its native storage
            else if constexpr (std::is_pointer_v<T> || smart_pointer<T> || runtime_shape<T>() == value_shape::pointer) return false;
            else if constexpr (runtime_reflectable<T>)
            {
                bool safe = true;
                for_each_runtime_field<T>([&](const auto& field) {
                    using F = std::remove_cvref_t<decltype(field)>;
                    safe = safe && !F::is_indirect && !F::template has_attribute<readonly>() &&
                        runtime_assignment_safe<typename F::value_type, typename visit<Visiting, T>::type>();
                });
                return safe;
            }
            else if constexpr (runtime_expected<T>) return []<class... Children>(type_list<Children...>) {
                return (runtime_assignment_safe<std::remove_cv_t<Children>, typename visit<Visiting, T>::type>() && ...);
            }(runtime_children_types<T>());
            else if constexpr (has_custom_serializer<T> || runtime_indexed<T>) return false;
            else if constexpr (runtime_shape<T>() == value_shape::unsupported || runtime_shape<T>() == value_shape::custom) return false;
            else return []<class... Children>(type_list<Children...>) {
                return (runtime_assignment_safe<std::remove_cv_t<Children>, typename visit<Visiting, T>::type>() && ...);
            }(runtime_children_types<T>());
        }

        template<class T>
        void runtime_serialize(writer& out, const T& value)
        {
            if constexpr (requires { runtime_value_traits<T>::write(out, value); }) runtime_value_traits<T>::write(out, value);
            else if constexpr (runtime_expected<T>)
            {
                out.begin_object(1);
                if (value.has_value())
                {
                    out.write_key("value");
                    if constexpr (std::is_void_v<typename T::value_type>) out.write_null();
                    else runtime_serialize(out, *value);
                }
                else { out.write_key("error"); runtime_serialize(out, value.error()); }
                out.end_object();
            }
            else if constexpr (has_custom_serializer<T> && !prefers_runtime_record_fields<T>()) serialize(out, value);
            else if constexpr (runtime_shape<T>() == value_shape::map && runtime_keyed<T>)
            {
                // Explicit navigation adapters need not promise unique or string-encodable keys.
                const auto count = static_cast<std::size_t>(runtime_value_traits<T>::size(value));
                out.begin_array(count);
                for (std::size_t index = 0; index < count; ++index)
                {
                    out.begin_array(2);
                    runtime_serialize(out, runtime_value_traits<T>::key(value, index));
                    runtime_serialize(out, runtime_value_traits<T>::at(value, index));
                    out.end_array();
                }
                out.end_array();
            }
            else if constexpr (runtime_shape<T>() == value_shape::sequence && runtime_indexed<T>)
            {
                const auto count = static_cast<std::size_t>(runtime_value_traits<T>::size(value));
                out.begin_array(count);
                for (std::size_t index = 0; index < count; ++index)
                    runtime_serialize(out, runtime_value_traits<T>::at(value, index));
                out.end_array();
            }
            else if constexpr (runtime_reflectable<T>)
            {
                const auto& type = type_descriptor_of<T>();
                std::size_t count = 0;
                for (const auto& field : type.fields()) if (!field.attributes().template contains<transient>()) ++count;
                out.begin_object(count);
                for (const auto& field : type.fields()) if (!field.attributes().template contains<transient>())
                { out.write_key(field.key()); field.value().serialize(out, field.address(&value)); }
                out.end_object();
            }
            else if constexpr ((runtime_shape<T>() == value_shape::sequence || runtime_shape<T>() == value_shape::map) &&
                                   (has_user_container_traits<T> || has_default_container_traits<T>))
            {
                using traits = container_traits_of<T>;
                if constexpr (traits::kind == container_kind::map)
                {
                    if constexpr (map_as_object<traits>)
                    {
                        out.begin_object(traits::size(value));
                        traits::for_each(value, [&](const auto& key, const auto& mapped) { out.write_key(encode_key(key)); runtime_serialize(out, mapped); });
                        out.end_object();
                    }
                    else
                    {
                        out.begin_array(traits::size(value));
                        traits::for_each(value, [&](const auto& key, const auto& mapped) { out.begin_array(2); runtime_serialize(out, key); runtime_serialize(out, mapped); out.end_array(); });
                        out.end_array();
                    }
                }
                else
                {
                    out.begin_array(traits::size(value));
                    traits::for_each(value, [&](const auto& item) { runtime_serialize(out, item); });
                    out.end_array();
                }
            }
            else if constexpr (is_specialization_of_v<T, std::optional>)
            { if (value) runtime_serialize(out, *value); else out.write_null(); }
            else if constexpr (is_specialization_of_v<T, std::variant>)
            {
                if (value.valueless_by_exception()) throw serialization_error("cannot serialize a valueless variant");
                out.begin_array(2); out.write_uint(value.index());
                std::visit([&](const auto& item) { runtime_serialize(out, item); }, value);
                out.end_array();
            }
            else if constexpr (runtime_shape<T>() == value_shape::tuple)
            { out.begin_array(std::tuple_size_v<T>); std::apply([&](const auto&... item) { (runtime_serialize(out, item), ...); }, value); out.end_array(); }
            else if constexpr (runtime_shape<T>() == value_shape::sequence && std::ranges::input_range<const T>)
            { out.begin_array(range_size(value)); for (const auto& item : value) runtime_serialize(out, item); out.end_array(); }
            else serialize(out, value);
        }

        template<class T>
        void runtime_deserialize(reader& in, T& value)
        {
            if constexpr (requires { runtime_value_traits<T>::read(in, value); }) runtime_value_traits<T>::read(in, value);
#if defined(__cpp_lib_expected)
            else if constexpr (runtime_expected<T>)
            {
                static_cast<void>(in.begin_object()); std::string key;
                if (!in.next_key(key)) throw serialization_error("expected object requires value or error");
                if (key == "value")
                {
                    if constexpr (std::is_void_v<typename T::value_type>) { in.read_null(); value = T{}; }
                    else { typename T::value_type item{}; runtime_deserialize(in, item); value = T(std::move(item)); }
                }
                else if (key == "error")
                { typename T::error_type error{}; runtime_deserialize(in, error); value = T(std::unexpect, std::move(error)); }
                else throw serialization_error("expected object requires value or error");
                if (in.next_key(key)) throw serialization_error("expected object has more than one result");
                in.end_object();
            }
#endif
            else if constexpr (has_custom_serializer<T> && !prefers_runtime_record_fields<T>()) deserialize(in, value);
            else if constexpr (runtime_reflectable<T>)
            {
                const auto fields = type_descriptor_of<T>().fields();
                std::vector<bool> seen(fields.size(), false);
                static_cast<void>(in.begin_object());
                std::string key;
                while (in.next_key(key))
                {
                    bool found = false;
                    for (std::size_t index = 0; index < fields.size(); ++index)
                    {
                        const auto& field = fields[index];
                        if (field.key() != key || field.attributes().template contains<transient>()) continue;
                        if (seen[index]) throw serialization_error("duplicate runtime field: " + key);
                        if (field.is_read_only()) throw serialization_error("cannot deserialize const runtime field: " + key);
                        field.value().deserialize(in, field.address(&value));
                        seen[index] = true; found = true; break;
                    }
                    if (!found) in.skip_value();
                }
                in.end_object();
                for (std::size_t index = 0; index < fields.size(); ++index)
                    if (!seen[index] && fields[index].attributes().template contains<required>())
                        throw serialization_error("missing required runtime field: " + std::string(fields[index].key()));
            }
            else if constexpr ((runtime_shape<T>() == value_shape::sequence || runtime_shape<T>() == value_shape::map) &&
                                   (has_user_container_traits<T> || has_default_container_traits<T>))
            {
                using traits = container_traits_of<T>;
                if constexpr (traits::kind == container_kind::map)
                {
                    traits::clear(value);
                    if constexpr (map_as_object<traits>)
                    {
                        static_cast<void>(in.begin_object()); std::string key;
                        while (in.next_key(key))
                        {
                            auto native_key = decode_key<typename traits::key_type>(key);
                            typename traits::mapped_type mapped{}; runtime_deserialize(in, mapped);
                            traits::add(value, std::move(native_key), std::move(mapped));
                        }
                        in.end_object();
                    }
                    else
                    {
                        static_cast<void>(in.begin_array());
                        while (in.next_element())
                        {
                            static_cast<void>(in.begin_array());
                            typename traits::key_type key{}; typename traits::mapped_type mapped{};
                            if (!in.next_element()) throw serialization_error("runtime map entry expects key and value");
                            runtime_deserialize(in, key);
                            if (!in.next_element()) throw serialization_error("runtime map entry expects key and value");
                            runtime_deserialize(in, mapped);
                            if (in.next_element()) throw serialization_error("runtime map entry expects key and value");
                            in.end_array(); traits::add(value, std::move(key), std::move(mapped));
                        }
                        in.end_array();
                    }
                }
                else
                {
                    using E = typename traits::value_type;
                    const auto hint = in.begin_array();
                    const auto read_all = [&](auto&& add) {
                        while (in.next_element()) { E item{}; runtime_deserialize(in, item); add(std::move(item)); }
                    };
                    if constexpr (traits_with_inserter<traits, T> || traits_with_add<traits, T>)
                    {
                        traits::clear(value);
                        if constexpr (traits_with_reserve<traits, T>) if (hint) traits::reserve(value, bounded_reserve<E>(*hint));
                        if constexpr (traits_with_inserter<traits, T>) { auto add = traits::inserter(value); read_all(add); }
                        else read_all([&](E&& item) { traits::add(value, std::move(item)); });
                    }
                    else
                    {
                        std::vector<E> items;
                        if (hint) items.reserve(bounded_reserve<E>(*hint));
                        read_all([&](E&& item) { items.push_back(std::move(item)); });
                        traits::assign(value, std::move(items));
                    }
                    in.end_array();
                }
            }
            else if constexpr (is_specialization_of_v<T, std::optional>)
            {
                if (in.peek() == value_kind::null) { in.read_null(); value.reset(); }
                else { typename T::value_type item{}; runtime_deserialize(in, item); value = std::move(item); }
            }
            else if constexpr (is_specialization_of_v<T, std::variant>)
            {
                static_cast<void>(in.begin_array());
                if (!in.next_element()) throw serialization_error("runtime variant expects index and value");
                const auto index = in.read_uint();
                if (index >= std::variant_size_v<T> || !in.next_element()) throw serialization_error("runtime variant index is out of range or value is missing");
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    ((index == I ? static_cast<void>(runtime_deserialize(in, value.template emplace<I>())) : static_cast<void>(0)), ...);
                }(std::make_index_sequence<std::variant_size_v<T>>{});
                if (in.next_element()) throw serialization_error("runtime variant expects index and value");
                in.end_array();
            }
            else if constexpr (runtime_shape<T>() == value_shape::tuple)
            {
                static_cast<void>(in.begin_array());
                std::apply([&](auto&... item) {
                    [[maybe_unused]] const auto read_one = [&](auto& next) { if (!in.next_element()) throw serialization_error("runtime tuple has too few elements"); runtime_deserialize(in, next); };
                    (read_one(item), ...);
                }, value);
                if (in.next_element()) throw serialization_error("runtime tuple has too many elements");
                in.end_array();
            }
            else if constexpr (runtime_shape<T>() == value_shape::sequence && std::ranges::range<T>)
            {
                static_cast<void>(in.begin_array());
                for (auto& item : value) { if (!in.next_element()) throw serialization_error("fixed runtime range has too few elements"); runtime_deserialize(in, item); }
                if (in.next_element()) throw serialization_error("fixed runtime range has too many elements");
                in.end_array();
            }
            else deserialize(in, value);
        }

        template<class T>
        void write_runtime_value(writer& out, const void* value) { runtime_serialize(out, *static_cast<const T*>(value)); }
        template<class T>
        void read_runtime_value(reader& in, void* value) { runtime_deserialize(in, *static_cast<T*>(value)); }
        template<class T>
        void assign_runtime_value(reader& in, void* value)
        {
            T staged(*static_cast<T*>(value));
            runtime_deserialize(in, staged);
            using std::swap;
            swap(*static_cast<T*>(value), staged);
        }

        template<class T>
        value_view view_reference(T& value, bool writable)
        {
            using U = std::remove_cv_t<T>;
            void* mutable_address = nullptr;
            if constexpr (!std::is_const_v<T>) if (writable) mutable_address = std::addressof(value);
            return value_view(value_descriptor_of<U>(), std::addressof(value), mutable_address);
        }

        template<class T>
        consteval bool permits_mutable_borrow()
        {
            if constexpr (requires { runtime_value_traits<T>::mutable_borrow; }) return runtime_value_traits<T>::mutable_borrow;
            else return false;
        }

        template<class T>
        concept has_runtime_borrow = requires(const T& value) { runtime_value_traits<T>::get(value); } &&
            std::is_pointer_v<decltype(runtime_value_traits<T>::get(std::declval<const T&>()))>;

        template<class T>
        std::size_t borrowed_count(const void* value) { return runtime_value_traits<T>::get(*static_cast<const T*>(value)) ? 1 : 0; }
        template<class T>
        value_view borrowed_child(const void* value, void* writable, std::size_t)
        {
            if constexpr (permits_mutable_borrow<T>() && requires(T& v) { runtime_value_traits<T>::get(v); })
                if (writable) return view_reference(*runtime_value_traits<T>::get(*static_cast<T*>(writable)), true);
            return view_reference(*runtime_value_traits<T>::get(*static_cast<const T*>(value)), false);
        }

        template<class T>
        std::size_t optional_count(const void* value) { return static_cast<const T*>(value)->has_value() ? 1 : 0; }
        template<class T>
        value_view optional_child(const void* value, void* writable, std::size_t)
        {
            if (writable) return view_reference(**static_cast<T*>(writable), true);
            return view_reference(**static_cast<const T*>(value), false);
        }

        template<class T>
        std::size_t range_count(const void* value) { return range_size(*static_cast<const T*>(value)); }
        template<class T>
        std::size_t indexed_count(const void* value)
        { return static_cast<std::size_t>(runtime_value_traits<T>::size(*static_cast<const T*>(value))); }
        template<class T>
        value_view indexed_child(const void* value, void* writable, std::size_t index)
        {
            if constexpr (requires(T& item) { runtime_value_traits<T>::at(item, index); })
            {
                using result = decltype(runtime_value_traits<T>::at(std::declval<T&>(), index));
                if constexpr (std::is_lvalue_reference_v<result>)
                    if (writable) return view_reference(runtime_value_traits<T>::at(*static_cast<T*>(writable), index), true);
            }
            return view_reference(runtime_value_traits<T>::at(*static_cast<const T*>(value), index), false);
        }
        template<class T>
        value_view indexed_key(const void* value, void*, std::size_t index)
        { return view_reference(runtime_value_traits<T>::key(*static_cast<const T*>(value), index), false); }
        template<class T>
        value_view range_child(const void* value, void* writable, std::size_t index)
        {
            using V = std::ranges::range_value_t<T>;
            if constexpr (std::ranges::range<T> && std::is_lvalue_reference_v<std::ranges::range_reference_t<T>> &&
                          std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<T>>, V>)
            {
                if (writable)
                {
                    auto iterator = std::ranges::begin(*static_cast<T*>(writable));
                    std::ranges::advance(iterator, static_cast<std::ranges::range_difference_t<T>>(index));
                    return view_reference(*iterator, true);
                }
            }
            auto iterator = std::ranges::begin(*static_cast<const T*>(value));
            std::ranges::advance(iterator, static_cast<std::ranges::range_difference_t<const T>>(index));
            if constexpr (std::is_lvalue_reference_v<std::ranges::range_reference_t<const T>> &&
                          std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<const T>>, V>)
                return view_reference(*iterator, false);
            else
            {
                auto snapshot = std::make_shared<V>(*iterator);
                const auto* address = snapshot.get();
                return value_view(value_descriptor_of<V>(), address, nullptr, std::move(snapshot));
            }
        }

        template<class T>
        value_view map_child(const void* value, void* writable, std::size_t index)
        {
            if (writable)
            {
                auto iterator = std::ranges::begin(*static_cast<T*>(writable));
                std::ranges::advance(iterator, static_cast<std::ranges::range_difference_t<T>>(index));
                return view_reference(iterator->second, true);
            }
            auto iterator = std::ranges::begin(*static_cast<const T*>(value));
            std::ranges::advance(iterator, static_cast<std::ranges::range_difference_t<const T>>(index));
            return view_reference(iterator->second, false);
        }
        template<class T>
        value_view map_key(const void* value, void*, std::size_t index)
        {
            auto iterator = std::ranges::begin(*static_cast<const T*>(value));
            std::ranges::advance(iterator, static_cast<std::ranges::range_difference_t<const T>>(index));
            return view_reference(iterator->first, false);
        }

        template<class T>
        std::size_t tuple_count(const void*) { return std::tuple_size_v<T>; }
        template<class T>
        value_view tuple_child(const void* value, void* writable, std::size_t index)
        {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                value_view result;
                ((index == I ? static_cast<void>(result = writable ? view_reference(std::get<I>(*static_cast<T*>(writable)), true)
                    : view_reference(std::get<I>(*static_cast<const T*>(value)), false)) : static_cast<void>(0)), ...);
                return result;
            }(std::make_index_sequence<std::tuple_size_v<T>>{});
        }

        template<class T>
        std::size_t variant_count(const void* value) { return static_cast<const T*>(value)->valueless_by_exception() ? 0 : 1; }
        template<class T>
        value_view variant_child(const void* value, void* writable, std::size_t)
        {
            if (writable) return std::visit([](auto& item) { return view_reference(item, true); }, *static_cast<T*>(writable));
            return std::visit([](const auto& item) { return view_reference(item, false); }, *static_cast<const T*>(value));
        }

        template<class T>
        std::size_t expected_count(const void* value)
        {
            if constexpr (std::is_void_v<typename T::value_type>) return static_cast<const T*>(value)->has_value() ? 0 : 1;
            else return 1;
        }
        template<class T>
        value_view expected_child(const void* value, void* writable, std::size_t)
        {
            const auto& object = *static_cast<const T*>(value);
            if (object.has_value())
            {
                if constexpr (!std::is_void_v<typename T::value_type>)
                {
                    if (writable) return view_reference(**static_cast<T*>(writable), true);
                    return view_reference(*object, false);
                }
                else throw std::out_of_range("expected<void> has no success value storage");
            }
            if (writable) return view_reference(static_cast<T*>(writable)->error(), true);
            return view_reference(object.error(), false);
        }

        template<class T>
        consteval value_descriptor make_value_descriptor()
        {
            constexpr auto shape = runtime_shape<T>();
            constexpr bool known = shape != value_shape::unsupported &&
                (shape != value_shape::custom || requires { typename runtime_value_traits<T>::children; });
            typename value_descriptor::object_getter object = nullptr;
            const enum_descriptor* enumeration = nullptr;
            typename value_descriptor::write_function write = nullptr;
            typename value_descriptor::read_function read = nullptr;
            typename value_descriptor::read_function assign = nullptr;
            typename value_descriptor::count_function count = nullptr;
            typename value_descriptor::select_function select = nullptr;
            typename value_descriptor::select_function key = nullptr;
            bool has_navigation = false;
            std::size_t size = 0, alignment = 0;
            std::string_view reason;
            if constexpr (complete_type<T> && !std::is_void_v<T>)
            {
                size = sizeof(T); alignment = alignof(T);
                if constexpr (runtime_reflectable<T>) object = &type_descriptor_of<T>;
                if constexpr (std::is_enum_v<T>) enumeration = &enum_descriptor_of<T>();
                if constexpr (runtime_value_serializable<T>()) write = &write_runtime_value<T>;
                if constexpr (runtime_value_deserializable<T>()) read = &read_runtime_value<T>;
                if constexpr (runtime_value_deserializable<T>() && runtime_assignment_safe<T>() &&
                              std::is_copy_constructible_v<T> && std::is_nothrow_swappable_v<T>) assign = &assign_runtime_value<T>;
                if constexpr (has_runtime_borrow<T>)
                { count = &borrowed_count<T>; select = &borrowed_child<T>; has_navigation = true; }
                else if constexpr (is_specialization_of_v<T, std::optional>)
                { count = &optional_count<T>; select = &optional_child<T>; has_navigation = true; }
                else if constexpr (is_specialization_of_v<T, std::variant>)
                { count = &variant_count<T>; select = &variant_child<T>; has_navigation = true; }
                else if constexpr (runtime_expected<T>)
                { count = &expected_count<T>; select = &expected_child<T>; has_navigation = true; }
                else if constexpr (shape == value_shape::map && runtime_keyed<T>)
                { count = &indexed_count<T>; select = &indexed_child<T>; key = &indexed_key<T>; has_navigation = true; }
                else if constexpr (shape == value_shape::sequence && runtime_indexed<T>)
                { count = &indexed_count<T>; select = &indexed_child<T>; has_navigation = true; }
                else if constexpr (shape == value_shape::map && map_like<T>)
                { count = &range_count<T>; select = &map_child<T>; key = &map_key<T>; has_navigation = true; }
                else if constexpr (shape == value_shape::sequence && std::ranges::input_range<const T>)
                { count = &range_count<T>; select = &range_child<T>; has_navigation = true; }
                else if constexpr (shape == value_shape::tuple && tuple_like<T>)
                { count = &tuple_count<T>; select = &tuple_child<T>; has_navigation = true; }
            }
            if constexpr (requires { runtime_value_traits<T>::unsupported_reason; }) reason = runtime_value_traits<T>::unsupported_reason;
            else if constexpr (shape == value_shape::unsupported) reason = "no runtime shape or serializer contract";
            else if constexpr (shape == value_shape::custom && !known) reason = "custom serializer has no recursive runtime_value_traits shape";
            else if constexpr (shape == value_shape::pointer && !has_runtime_borrow<T>) reason = "pointer dereference requires an explicit runtime_value_traits borrow adapter";
            else if constexpr (shape == value_shape::sequence || shape == value_shape::map)
                if (!has_navigation) reason = "container shape is known but no stable indexed navigation is available";
            std::string_view name = type_name_of<T>();
            if constexpr (runtime_reflectable<T>) name = runtime_schema_of<T>.name;
            return value_descriptor(type_id_of<T>(), name, size, alignment, shape, known,
                runtime_value_children<T>, object, enumeration, write, read, assign, count, select, key, reason);
        }

        template<class T>
        inline constexpr value_descriptor runtime_value = make_value_descriptor<T>();

        template<class T>
        consteval bool serialized_argument_safe()
        {
            using U = std::remove_cvref_t<T>;
            if constexpr (requires { runtime_value_traits<U>::argument_safe; }) return runtime_value_traits<U>::argument_safe;
            else if constexpr (is_specialization_of_v<U, std::basic_string_view>) return true;
            else if constexpr (runtime_span<U>) return runtime_assignment_safe<std::remove_cv_t<typename U::element_type>>();
            else if constexpr (std::is_pointer_v<U> || runtime_shape<U>() == value_shape::pointer) return false;
            else return runtime_assignment_safe<U>();
        }

        template<class T>
        consteval bool runtime_argument_materializable()
        {
            if constexpr (runtime_span<T>)
                return std::default_initializable<std::remove_cv_t<typename T::element_type>> &&
                    !std::is_volatile_v<typename T::element_type> &&
                    !std::same_as<std::remove_cv_t<typename T::element_type>, bool>;
            else return std::default_initializable<T>;
        }

        template<class T>
        struct runtime_argument_slot
        {
            T value{};
            static constexpr bool readable = runtime_value_deserializable<T>();
            void read(reader& in) { runtime_deserialize(in, value); }
            T& get() noexcept { return value; }
            const T& get() const noexcept { return value; }
        };

        template<class Char, class Traits>
        struct runtime_argument_slot<std::basic_string_view<Char, Traits>>
        {
            std::basic_string<Char, Traits> storage;
            std::basic_string_view<Char, Traits> value;
            static constexpr bool readable = runtime_value_deserializable<std::basic_string<Char, Traits>>();
            void read(reader& in) { runtime_deserialize(in, storage); value = storage; }
            auto& get() noexcept { return value; }
            const auto& get() const noexcept { return value; }
        };

        template<character Char>
        struct runtime_argument_slot<const Char*>
        {
            std::basic_string<Char> storage;
            const Char* value = nullptr;
            static constexpr bool readable = runtime_value_deserializable<std::basic_string<Char>>();
            void read(reader& in) { runtime_deserialize(in, storage); value = storage.c_str(); }
            auto& get() noexcept { return value; }
            const auto& get() const noexcept { return value; }
        };

        template<class E, std::size_t N>
        struct runtime_argument_slot<std::span<E, N>>
        {
            using span_type = std::span<E, N>;
            std::vector<std::remove_cv_t<E>> storage;
            std::optional<span_type> value;
            static constexpr bool readable = runtime_value_deserializable<decltype(storage)>();
            void read(reader& in)
            {
                runtime_deserialize(in, storage);
                if constexpr (N != std::dynamic_extent)
                    if (storage.size() != N) throw serialization_error("method span argument has the wrong fixed extent");
                value.emplace(storage.data(), storage.size());
            }
            span_type& get() noexcept { return *value; }
            const span_type& get() const noexcept { return *value; }
        };

        template<class T, auto Function>
        consteval std::string_view serialized_method_reason()
        {
            using signature = method_signature_of<Function>;
            using R = std::remove_cvref_t<typename signature::result>;
            if constexpr (!std::is_void_v<R> && !runtime_value_serializable<R>()) return "method result is not serializable";
            else return []<class... Args>(type_list<Args...>) -> std::string_view {
                if constexpr (!(runtime_argument_materializable<std::remove_cvref_t<Args>>() && ...)) return "method argument cannot be materialized without a native factory";
                else if constexpr (!(serialized_argument_safe<Args>() && ...)) return "method argument requires an explicit safe value/ownership adapter";
                else if constexpr (!(runtime_argument_slot<std::remove_cvref_t<Args>>::readable && ...)) return "method argument is not deserializable";
                else if constexpr (!(runtime_value_serializable<std::remove_cvref_t<Args>>() && ...)) return "method argument cannot be serialized after invocation";
                else return {};
            }(typename signature::arguments{});
        }

        template<class T, auto Function>
        void invoke_serialized_method(void* object, reader& input, writer& response)
        {
            []<class... Args>(T& self, reader& arguments, writer& output, type_list<Args...>) {
                std::tuple<runtime_argument_slot<std::remove_cvref_t<Args>>...> values{};
                static_cast<void>(arguments.begin_array());
                std::apply([&](auto&... value) {
                    [[maybe_unused]] const auto read_one = [&](auto& item) {
                        if (!arguments.next_element()) throw serialization_error("method argument array has too few elements");
                        item.read(arguments);
                    };
                    (read_one(value), ...);
                }, values);
                if (arguments.next_element()) throw serialization_error("method argument array has too many elements");
                arguments.end_array();
                using R = typename method_signature_of<Function>::result;
                // Invoke before beginning the response. Native exceptions leave no partial envelope.
                const auto finish = [&](auto&& write_result) {
                    output.begin_object(2);
                    output.write_key("result"); write_result();
                    output.write_key("arguments"); output.begin_array(sizeof...(Args));
                    std::apply([&](const auto&... value) { (runtime_serialize(output, value.get()), ...); }, values);
                    output.end_array(); output.end_object();
                };
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    if constexpr (std::is_void_v<R>)
                    {
                        (self.*Function)(method_argument<Args>(std::addressof(std::get<I>(values).get()))...);
                        finish([&] { output.write_null(); });
                    }
                    else
                    {
                        decltype(auto) result = (self.*Function)(method_argument<Args>(std::addressof(std::get<I>(values).get()))...);
                        finish([&] { runtime_serialize(output, result); });
                    }
                }(std::index_sequence_for<Args...>{});
            }(*static_cast<T*>(object), input, response, typename method_signature_of<Function>::arguments{});
        }

        template<class T, auto Function>
        value_view borrow_method_result(void* object, bool writable)
        {
            using R = typename method_signature_of<Function>::result;
            if constexpr (std::is_lvalue_reference_v<R>)
            {
                auto& result = (static_cast<T*>(object)->*Function)();
                return view_reference(result, writable);
            }
            else
            {
                using U = std::remove_cvref_t<R>;
                auto storage = std::make_shared<U>((static_cast<T*>(object)->*Function)());
                auto* address = storage.get();
                void* mutable_address = nullptr;
                if constexpr (requires { runtime_value_traits<U>::retained_borrow; })
                    if (writable && runtime_value_traits<U>::retained_borrow && permits_mutable_borrow<U>()) mutable_address = address;
                return value_view(value_descriptor_of<U>(), address, mutable_address, std::move(storage));
            }
        }
    }

    template<class T>
    constexpr const value_descriptor& value_descriptor_of() noexcept
    {
        return detail::runtime_value<std::remove_cvref_t<T>>;
    }

    inline std::vector<const value_descriptor*> value_type_closure(std::span<const type_descriptor* const> roots)
    {
        std::map<std::uint64_t, const value_descriptor*> visited;
        const auto visit = [&](const auto& self, const value_descriptor& value) -> void {
            const auto [position, inserted] = visited.emplace(value.id().value(), &value);
            if (!inserted)
            {
                if (position->second->name() != value.name()) throw std::logic_error("runtime value type-id collision");
                return;
            }
            for (const auto& child : value.children()) self(self, child.value());
            if (const auto* object = value.reflected_type())
            {
                for (const auto& base : object->bases()) if (const auto* descriptor = base.descriptor()) self(self, descriptor->value());
                for (const auto& field : object->fields()) self(self, field.value());
                for (const auto& method : object->methods())
                {
                    self(self, method.result());
                    for (const auto& parameter : method.parameters()) self(self, parameter.value());
                }
            }
        };
        for (const auto* root : roots)
        {
            if (!root) throw std::invalid_argument("null root in runtime type closure");
            visit(visit, root->value());
        }
        std::vector<const value_descriptor*> result;
        for (const auto& [id, descriptor] : visited) { static_cast<void>(id); result.push_back(descriptor); }
        std::ranges::sort(result, {}, [](const auto* descriptor) { return descriptor->name(); });
        return result;
    }
}

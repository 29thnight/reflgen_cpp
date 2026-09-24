#pragma once
// 서술된 클래스 — 필드 이름(또는 serialized_name)을 키로 하는 객체.
//
// 읽기 의미론은 "있는 것만 덮어쓴다"다:
//   - 입력에 없는 필드는 기존 값을 유지한다(required 속성이 붙은 것은 실패).
//   - 모르는 키는 건너뛴다 — 필드를 지운 뒤에도 옛 파일이 읽힌다.
// 두 규칙 모두 파일 형식이 코드보다 오래 산다는 전제에서 나왔다.
#include "reflgen/core/attributes.h"
#include "reflgen/core/schema.h"
#include "reflgen/serial/detail/path.h"
#include "reflgen/serial/detail/traits.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/fwd.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace reflgen::detail
{
template<class Field>
constexpr std::string_view serialized_key(const Field& field) noexcept
{
    if constexpr (Field::template has_attribute<serialized_name>())
    {
        return field.template attribute<serialized_name>().value;
    }
    else
    {
        return field.name;
    }
}

template<class Field>
inline constexpr bool is_transient_field = Field::template has_attribute<transient>();

template<class T>
consteval std::size_t serialized_field_count() noexcept
{
    std::size_t count = 0;
    for_each_field<T>([&](const auto& field) {
        if constexpr (!is_transient_field<std::remove_cvref_t<decltype(field)>>)
        {
            ++count;
        }
    });
    return count;
}

// 부모와 자식이 같은 키를 쓰면 한 객체에 같은 키가 두 번 적힌다. 읽을 때 뒤의 것이
// 앞의 것을 덮으므로 값이 조용히 사라진다 — 컴파일 시점에 막는다.
template<class T>
consteval bool serialized_keys_unique() noexcept
{
    std::array<std::string_view, field_count<T>()> keys{};
    std::size_t count = 0;
    for_each_field<T>([&](const auto& field) {
        if constexpr (!is_transient_field<std::remove_cvref_t<decltype(field)>>)
        {
            keys[count] = serialized_key(field);
            ++count;
        }
    });
    for (std::size_t i = 0; i < count; ++i)
    {
        for (std::size_t j = i + 1; j < count; ++j)
        {
            if (keys[i] == keys[j])
            {
                return false;
            }
        }
    }
    return true;
}

// 필드가 모두 쓰일 수 있는가(한 단계만 본다 — 재귀 타입에서 판정이 끝나야 한다).
template<class T>
consteval bool object_fields_serializable() noexcept
{
    bool result = true;
    for_each_field<T>([&](const auto& field) {
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (!is_transient_field<field_type>)
        {
            result = result && is_serializable<typename field_type::value_type>();
        }
    });
    return result;
}

template<class T>
consteval bool object_fields_deserializable() noexcept
{
    bool result = true;
    for_each_field<T>([&](const auto& field) {
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (!is_transient_field<field_type>)
        {
            result = result && is_deserializable<typename field_type::value_type>();
        }
    });
    return result;
}

// transient 는 "읽지도 쓰지도 않는다", required 는 "입력에 반드시 있다" — 둘이 한 필드에
// 붙으면 그 타입은 어떤 입력으로도 읽히지 않는다. 런타임에 매번 실패하는 대신 막는다.
template<class T>
consteval bool attributes_consistent() noexcept
{
    bool result = true;
    for_each_field<T>([&](const auto& field) {
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (is_transient_field<field_type> && field_type::template has_attribute<required>())
        {
            result = false;
        }
    });
    return result;
}

template<class T>
void write_object(writer& out, const T& value)
{
    static_assert(serialized_keys_unique<T>(),
                  "two serialized fields of this type share a key; rename one with reflgen::serialized_name");
    static_assert(attributes_consistent<T>(), "a field is both reflgen::transient and reflgen::required");

    out.begin_object(serialized_field_count<T>());
    for_each_field<T>([&](const auto& field) {
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (!is_transient_field<field_type>)
        {
            static_assert(is_serializable<typename field_type::value_type>(),
                          "a field of this type cannot be serialized; mark it reflgen::transient or specialize "
                          "reflgen::serializer for its type");
            const std::string_view key = serialized_key(field);
            out.write_key(key);
            with_path(key, [&] { serialize(out, value.*field_type::pointer); });
        }
    });
    out.end_object();
}

template<class T>
void read_object(reader& in, T& value)
{
    static_assert(serialized_keys_unique<T>(),
                  "two serialized fields of this type share a key; rename one with reflgen::serialized_name");
    static_assert(attributes_consistent<T>(), "a field is both reflgen::transient and reflgen::required");

    constexpr std::size_t count = field_count<T>();
    std::array<bool, count> seen{};

    in.begin_object();
    std::string key;
    while (in.next_key(key))
    {
        bool matched = false;
        std::size_t index = 0;
        for_each_field<T>([&](const auto& field) {
            using field_type = std::remove_cvref_t<decltype(field)>;
            if constexpr (!is_transient_field<field_type>)
            {
                static_assert(is_deserializable<typename field_type::value_type>(),
                              "a field of this type cannot be deserialized; mark it reflgen::transient or "
                              "specialize reflgen::serializer for its type");
                if (!matched && key == serialized_key(field))
                {
                    matched = true;
                    seen[index] = true;
                    with_path(key, [&] { deserialize(in, value.*field_type::pointer); });
                }
            }
            ++index;
        });
        if (!matched)
        {
            in.skip_value();
        }
    }
    in.end_object();

    std::size_t index = 0;
    for_each_field<T>([&](const auto& field) {
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (field_type::template has_attribute<required>())
        {
            if (!seen[index])
            {
                throw serialization_error("missing required field").with_parent(serialized_key(field));
            }
        }
        ++index;
    });
}
} // namespace reflgen::detail

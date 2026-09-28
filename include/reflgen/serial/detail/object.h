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
    consteval void check_object_contract() noexcept
    {
        static_assert(serialized_keys_unique<T>(),
                      "two serialized fields of this type share a key; rename one with reflgen::serialized_name");
        static_assert(attributes_consistent<T>(), "a field is both reflgen::transient and reflgen::required");
    }

    // ★ 필드마다 실체화되는 것은 함수 하나(write_one_field·read_one_field)다 — 필드 방문 람다·with_path 람다를 겹겹이 두면
    //   Debug 빌드의 코드 생성이 필드 수에 몇 배로 붙는다. 부모의 필드는 부모 타입의 함수가 맡는다 — 자식 타입마다
    //   부모 필드를 다시 실체화하지 않는다. 순서는 for_each_field 와 같다(부모 먼저, base<> 선언 순서).
    template<class Owner, class Field>
    void write_one_field(writer& out, const Owner& value, const Field& field)
    {
        if constexpr (!is_transient_field<Field>)
        {
            // 얕은 판정 — 품은 서술된 클래스의 필드는 그 클래스를 쓸 때 단정된다(오류가 그 필드를 가리킨다).
            static_assert(is_serializable_shallow<typename Field::value_type>(),
                          "a field of this type cannot be serialized; mark it reflgen::transient or specialize "
                          "reflgen::serializer for its type");
            const std::string_view key = serialized_key(field);
            out.write_key(key);
            const path_scope scope(key);
            serialize(out, value.*Field::pointer);
        }
    }

    template<class T>
    void write_own_and_base_fields(writer& out, const T& value)
    {
        if constexpr (reflectable<T>)
        {
            [&]<class... Bases>(type_list<Bases...>) {
                (write_own_and_base_fields<Bases>(out, static_cast<const Bases&>(value)), ...);
            }(direct_bases_t<T>{});
            std::apply([&](const auto&... fields) { (write_one_field(out, value, fields), ...); }, schema_of<T>.fields);
        }
    }

    // 객체 몸통 — transient 가 아닌 필드마다 키와 값(부모 먼저). begin_object·end_object 는 부르는 쪽 몫이다.
    template<class T>
    void write_fields(writer& out, const T& value)
    {
        check_object_contract<T>();
        write_own_and_base_fields(out, value);
    }

    template<class Owner, class Field>
    bool read_one_field(reader& in, Owner& value, const Field& field, std::string_view key)
    {
        if constexpr (is_transient_field<Field>)
        {
            return false;
        }
        else
        {
            static_assert(is_deserializable_shallow<typename Field::value_type>(),
                          "a field of this type cannot be deserialized; mark it reflgen::transient or "
                          "specialize reflgen::serializer for its type");
            if (key != serialized_key(field))
            {
                return false;
            }
            const path_scope scope(key);
            deserialize(in, value.*Field::pointer);
            return true;
        }
    }

    // T 와 그 부모들의 필드 가운데 key 인 것의 순번(for_each_field<T> 순서). 없으면 field_count<T>().
    template<class T>
    std::size_t read_own_and_base_field(reader& in, T& value, std::string_view key)
    {
        constexpr std::size_t none = field_count<T>();
        if constexpr (!reflectable<T>)
        {
            return none;
        }
        else
        {
            std::size_t found = none;
            std::size_t offset = 0;
            [&]<class... Bases>(type_list<Bases...>) {
                (
                    [&] {
                        if (found == none)
                        {
                            const std::size_t index =
                                read_own_and_base_field<Bases>(in, static_cast<Bases&>(value), key);
                            if (index != field_count<Bases>())
                            {
                                found = offset + index;
                            }
                        }
                        offset += field_count<Bases>();
                    }(),
                    ...);
            }(direct_bases_t<T>{});
            if (found != none)
            {
                return found;
            }
            // 찾으면 멈춘다(|| 의 단락).
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                (void)((read_one_field(in, value, std::get<I>(schema_of<T>.fields), key) &&
                        ((found = offset + I), true)) ||
                       ...);
            }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(schema_of<T>.fields)>>>{});
            return found;
        }
    }

    // key 가 transient 가 아닌 필드의 키면 그 값을 읽고 필드의 순번(for_each_field 순서)을 돌려준다. 아니면 아무것도
    // 소비하지 않고 field_count<T>() 를 돌려준다.
    template<class T>
    std::size_t read_field_at(reader& in, T& value, std::string_view key)
    {
        check_object_contract<T>();
        return read_own_and_base_field(in, value, key);
    }

    template<class T>
    void write_object(writer& out, const T& value)
    {
        out.begin_object(serialized_field_count<T>());
        write_fields(out, value);
        out.end_object();
    }

    template<class T>
    void read_object(reader& in, T& value)
    {
        constexpr std::size_t count = field_count<T>();
        std::array<bool, count> seen{};

        in.begin_object();
        std::string key;
        while (in.next_key(key))
        {
            const std::size_t index = read_field_at(in, value, key);
            if (index == count)
            {
                in.skip_value();
            }
            else
            {
                seen[index] = true;
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
                    throw serialization_error("missing required field").with_child(serialized_key(field));
                }
            }
            ++index;
        });
    }
} // namespace reflgen::detail

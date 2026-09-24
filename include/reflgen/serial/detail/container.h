#pragma once
// 컨테이너 — container_traits(사용자 특수화 또는 기본) 위에서 동작하는 한 벌의 코드.
//
// 맵의 모양은 둘이다:
//   - 키가 유일하고 문자열·정수·열거형이면 객체: {"3": …, "7": …}
//     정수 키를 문자열로 적는 것은 JSON 객체 키가 문자열뿐이기 때문이다.
//   - 그 밖(multimap, 구조체 키)은 [키, 값] 쌍의 배열: [[k, v], …]
// 쓰기와 읽기가 같은 판정(map_as_object)을 써서 모양이 어긋날 수 없다.
#include "reflgen/core/enum.h"
#include "reflgen/core/name.h"
#include "reflgen/serial/detail/default_container_traits.h"
#include "reflgen/serial/detail/path.h"
#include "reflgen/serial/detail/scalar.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/fwd.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace reflgen::detail
{
template<class Integer>
std::string integer_to_key(Integer value)
{
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

template<class Integer>
Integer integer_from_key(std::string_view text)
{
    Integer value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        throw serialization_error("map key '" + std::string(text) + "' is not a valid " +
                                  std::string(type_name_of<Integer>()));
    }
    return value;
}

template<class Key>
std::string encode_key(const Key& key)
{
    if constexpr (string_key<Key>)
    {
        return std::string(key.data(), key.size());
    }
    else if constexpr (std::is_same_v<Key, bool>)
    {
        return key ? "true" : "false";
    }
    else if constexpr (std::is_enum_v<Key>)
    {
        const std::string_view name = enum_name(key);
        return name.empty() ? integer_to_key(static_cast<std::underlying_type_t<Key>>(key)) : std::string(name);
    }
    else
    {
        return integer_to_key(key);
    }
}

template<class Key>
Key decode_key(const std::string& text)
{
    if constexpr (string_key<Key>)
    {
        return Key(text.begin(), text.end());
    }
    else if constexpr (std::is_same_v<Key, bool>)
    {
        if (text == "true" || text == "false")
        {
            return text == "true";
        }
        throw serialization_error("map key '" + text + "' is not a valid bool");
    }
    else if constexpr (std::is_enum_v<Key>)
    {
        if (const auto value = enum_cast<Key>(text))
        {
            return *value;
        }
        return static_cast<Key>(integer_from_key<std::underlying_type_t<Key>>(text));
    }
    else
    {
        return integer_from_key<Key>(text);
    }
}

template<class Traits>
inline constexpr bool map_as_object = Traits::unique_keys && key_codec<typename Traits::key_type>;

template<class C>
void write_container(writer& out, const C& value)
{
    using traits = container_traits_of<C>;
    if constexpr (traits::kind == container_kind::map)
    {
        if constexpr (map_as_object<traits>)
        {
            out.begin_object(traits::size(value));
            traits::for_each(value, [&](const auto& key, const auto& mapped) {
                const std::string text = encode_key(key);
                out.write_key(text);
                with_path(text, [&] { serialize(out, mapped); });
            });
            out.end_object();
        }
        else
        {
            out.begin_array(traits::size(value));
            std::size_t index = 0;
            traits::for_each(value, [&](const auto& key, const auto& mapped) {
                with_index(index, [&] {
                    out.begin_array(2);
                    serialize(out, key);
                    serialize(out, mapped);
                    out.end_array();
                });
                ++index;
            });
            out.end_array();
        }
    }
    else
    {
        out.begin_array(traits::size(value));
        std::size_t index = 0;
        traits::for_each(value, [&](const auto& element) {
            with_index(index, [&] { serialize(out, element); });
            ++index;
        });
        out.end_array();
    }
}

template<class Traits, class C>
void read_map(reader& in, C& value)
{
    using key_type = typename Traits::key_type;
    using mapped_type = typename Traits::mapped_type;

    Traits::clear(value);
    if constexpr (map_as_object<Traits>)
    {
        in.begin_object();
        std::string text;
        while (in.next_key(text))
        {
            with_path(text, [&] {
                key_type key = decode_key<key_type>(text);
                mapped_type mapped{};
                deserialize(in, mapped);
                Traits::add(value, std::move(key), std::move(mapped));
            });
        }
        in.end_object();
    }
    else
    {
        in.begin_array();
        std::size_t index = 0;
        while (in.next_element())
        {
            with_index(index, [&] {
                key_type key{};
                mapped_type mapped{};
                in.begin_array();
                if (!in.next_element())
                {
                    throw serialization_error("map entry expects [key, value]");
                }
                deserialize(in, key);
                if (!in.next_element())
                {
                    throw serialization_error("map entry expects [key, value]");
                }
                deserialize(in, mapped);
                if (in.next_element())
                {
                    throw serialization_error("map entry expects [key, value]");
                }
                in.end_array();
                Traits::add(value, std::move(key), std::move(mapped));
            });
            ++index;
        }
        in.end_array();
    }
}

// 입력이 알려 준 원소 수로 미리 잡는 메모리의 상한(바이트). 백엔드는 원소 수를 "남은
// 입력 바이트 수"로만 묶을 수 있다 — 원소 하나가 1바이트라는 전제다. 원소 타입이 크면
// (sizeof 가 수백 바이트) 그 곱만큼 부풀어, 몇 MB 입력이 원소 하나 읽기도 전에 GB 단위
// 예약을 부른다. 예약은 이 예산까지만 하고, 넘는 몫은 실제로 읽힌 원소만큼 자란다.
inline constexpr std::size_t reserve_budget_bytes = std::size_t{1} << 20;

template<class Element>
constexpr std::size_t bounded_reserve(std::size_t hint) noexcept
{
    constexpr std::size_t element_size = sizeof(Element) > 0 ? sizeof(Element) : 1;
    constexpr std::size_t limit = reserve_budget_bytes / element_size > 0 ? reserve_budget_bytes / element_size : 1;
    return hint < limit ? hint : limit;
}

template<class Traits, class C>
void read_sequence(reader& in, C& value)
{
    using element = typename Traits::value_type;
    const std::optional<std::size_t> hint = in.begin_array();

    // 원소 하나를 읽어 sink 에 넘긴다. 실패 경로에 인덱스가 붙는다.
    const auto read_all = [&](auto&& sink) {
        std::size_t index = 0;
        while (in.next_element())
        {
            with_index(index, [&] {
                element item{};
                deserialize(in, item);
                sink(std::move(item));
            });
            ++index;
        }
    };

    if constexpr (traits_with_inserter<Traits, C> || traits_with_add<Traits, C>)
    {
        Traits::clear(value);
        if constexpr (traits_with_reserve<Traits, C>)
        {
            if (hint)
            {
                Traits::reserve(value, bounded_reserve<element>(*hint));
            }
        }
        if constexpr (traits_with_inserter<Traits, C>)
        {
            auto insert = Traits::inserter(value);
            read_all([&](element&& item) { insert(std::move(item)); });
        }
        else
        {
            read_all([&](element&& item) { Traits::add(value, std::move(item)); });
        }
    }
    else
    {
        static_assert(traits_with_assign<Traits, C>,
                      "container_traits must provide clear()+add() or assign() to be deserializable");
        std::vector<element> items;
        if (hint)
        {
            items.reserve(bounded_reserve<element>(*hint));
        }
        read_all([&](element&& item) { items.push_back(std::move(item)); });
        Traits::assign(value, std::move(items));
    }
    in.end_array();
}

template<class C>
void read_container(reader& in, C& value)
{
    using traits = container_traits_of<C>;
    if constexpr (traits::kind == container_kind::map)
    {
        read_map<traits>(in, value);
    }
    else
    {
        read_sequence<traits>(in, value);
    }
}

// 크기 고정 범위와 읽기 규약이 없는 범위(뷰)의 쓰기. 참조가 대리 객체면 값으로 바꾼다.
template<class Range>
void write_range(writer& out, const Range& value)
{
    using element = std::ranges::range_value_t<const Range>;
    out.begin_array(range_size(value));
    std::size_t index = 0;
    for (auto&& item : value)
    {
        with_index(index, [&] {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(item)>, element>)
            {
                serialize(out, item);
            }
            else
            {
                serialize(out, static_cast<element>(item));
            }
        });
        ++index;
    }
    out.end_array();
}

template<class Range>
void read_fixed_range(reader& in, Range& value)
{
    const std::size_t size = range_size(value);
    in.begin_array();
    std::size_t index = 0;
    auto iterator = std::ranges::begin(value);
    while (in.next_element())
    {
        if (index >= size)
        {
            throw serialization_error("expected " + std::to_string(size) + " elements, got more");
        }
        with_index(index, [&] { deserialize(in, *iterator); });
        ++iterator;
        ++index;
    }
    if (index != size)
    {
        throw serialization_error("expected " + std::to_string(size) + " elements, got " + std::to_string(index));
    }
    in.end_array();
}

template<class Blob>
void write_bytes(writer& out, const Blob& value)
{
    out.write_bytes(std::span<const std::byte>(std::ranges::data(value), std::ranges::size(value)));
}

template<class Blob>
void read_bytes(reader& in, Blob& value)
{
    const std::vector<std::byte> decoded = in.read_bytes();
    if constexpr (requires { value.clear(); })
    {
        value.assign(decoded.data(), decoded.data() + decoded.size());
    }
    else
    {
        const std::size_t size = std::ranges::size(value);
        if (decoded.size() != size)
        {
            throw serialization_error("expected " + std::to_string(size) + " bytes, got " +
                                      std::to_string(decoded.size()));
        }
        std::ranges::copy(decoded, std::ranges::begin(value));
    }
}

// 컨테이너 어댑터의 속 컨테이너(보호 멤버 c)에 닿는 표준 기법 — 파생 클래스에서
// 멤버 포인터를 만들면 접근 검사가 통과하고, 그 포인터는 기반 객체에 쓸 수 있다.
template<class Adapter>
struct adapter_access : Adapter
{
    static const typename Adapter::container_type& container(const Adapter& adapter)
    {
        return adapter.*(&adapter_access::c);
    }
};

// stack 은 바닥→꼭대기, queue 는 앞→뒤, priority_queue 는 힙 배열 순서로 적는다.
template<class Adapter>
void write_adapter(writer& out, const Adapter& value)
{
    serialize(out, adapter_access<Adapter>::container(value));
}

// push 로 다시 쌓는다 — priority_queue 의 비교자(상태가 있을 수 있다)를 보존하고
// 힙 불변식을 어댑터가 직접 세우게 한다.
template<class Adapter>
void read_adapter(reader& in, Adapter& value)
{
    typename Adapter::container_type items;
    deserialize(in, items);
    while (!value.empty())
    {
        value.pop();
    }
    for (auto& item : items)
    {
        value.push(std::move(item));
    }
}
} // namespace reflgen::detail

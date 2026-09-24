#pragma once
// 값 래퍼·합 타입·곱 타입 — optional, variant, tuple/pair, complex, bitset, chrono,
// filesystem::path, atomic, reference_wrapper.
#include "reflgen/serial/detail/path.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/fwd.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include <bitset>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace reflgen::detail
{
    [[noreturn]] inline void throw_element_count(std::size_t expected, std::size_t actual)
    {
        throw serialization_error("expected " + std::to_string(expected) + " elements, got " +
                                  (actual > expected ? "more" : std::to_string(actual)));
    }

    template<class Optional>
    void write_optional(writer& out, const Optional& value)
    {
        if (value.has_value())
        {
            serialize(out, *value);
        }
        else
        {
            out.write_null();
        }
    }

    // 값이 이미 있으면 그 자리에 읽는다 — 서술된 클래스라면 입력에 없는 필드가 기존
    // 값을 유지한다(객체 읽기와 같은 의미론). optional<optional<T>> 의 안쪽 비움은
    // null 하나로 겹쳐 구별되지 않는다.
    template<class Optional>
    void read_optional(reader& in, Optional& value)
    {
        if (in.peek() == value_kind::null)
        {
            in.read_null();
            value.reset();
            return;
        }
        auto& target = value.has_value() ? *value : value.emplace();
        deserialize(in, target);
    }

    // [인덱스, 값] 두 원소 배열. 타입 이름이 아니라 인덱스를 쓰는 이유: 표준 라이브러리
    // 타입의 이름은 구현마다 다르다(std::__cxx11::basic_string 등). 대안의 순서를
    // 바꾸면 옛 파일과 어긋난다는 대가가 있다 — 순서는 파일 형식의 일부다.
    template<class Variant>
    void write_variant(writer& out, const Variant& value)
    {
        if (value.valueless_by_exception())
        {
            throw serialization_error("cannot serialize a valueless variant");
        }
        out.begin_array(2);
        out.write_uint(value.index());
        with_index(1, [&] { std::visit([&](const auto& alternative) { serialize(out, alternative); }, value); });
        out.end_array();
    }

    template<class Variant, std::size_t... Is>
    void emplace_alternative(reader& in, Variant& value, std::size_t index, std::index_sequence<Is...>)
    {
        using thunk = void (*)(reader&, Variant&);
        static constexpr thunk table[] = {
            [](reader& source, Variant& target) { deserialize(source, target.template emplace<Is>()); }...};
        table[index](in, value);
    }

    template<class Variant>
    void read_variant(reader& in, Variant& value)
    {
        constexpr std::size_t alternatives = std::variant_size_v<Variant>;
        in.begin_array();
        if (!in.next_element())
        {
            throw serialization_error("variant expects [index, value]");
        }
        const std::uint64_t index = in.read_uint();
        if (index >= alternatives)
        {
            throw serialization_error("variant index " + std::to_string(index) + " is out of range (" +
                                      std::to_string(alternatives) + " alternatives)");
        }
        if (!in.next_element())
        {
            throw serialization_error("variant expects [index, value]");
        }
        with_index(1, [&] {
            emplace_alternative(in, value, static_cast<std::size_t>(index), std::make_index_sequence<alternatives>{});
        });
        if (in.next_element())
        {
            throw serialization_error("variant expects [index, value]");
        }
        in.end_array();
    }

    // 튜플 모양(std::tuple · std::pair · tuple_size 를 특수화한 사용자 타입)은 원소 배열.
    // get 은 ADL 로도 찾는다 — 사용자 튜플 모양 타입의 get 이 자기 네임스페이스에 있다.
    template<class Tuple>
    void write_tuple(writer& out, const Tuple& value)
    {
        constexpr std::size_t size = std::tuple_size_v<Tuple>;
        out.begin_array(size);
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            (with_index(Is, [&] { serialize(out, get<Is>(value)); }), ...);
        }(std::make_index_sequence<size>{});
        out.end_array();
    }

    template<class Tuple>
    void read_tuple(reader& in, Tuple& value)
    {
        constexpr std::size_t size = std::tuple_size_v<Tuple>;
        in.begin_array();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using std::get;
            (
                [&] {
                    if (!in.next_element())
                    {
                        throw_element_count(size, Is);
                    }
                    with_index(Is, [&] { deserialize(in, get<Is>(value)); });
                }(),
                ...);
        }(std::make_index_sequence<size>{});
        if (in.next_element())
        {
            throw_element_count(size, size + 1);
        }
        in.end_array();
    }

    template<class T>
    void write_complex(writer& out, const std::complex<T>& value)
    {
        out.begin_array(2);
        serialize(out, value.real());
        serialize(out, value.imag());
        out.end_array();
    }

    template<class T>
    void read_complex(reader& in, std::complex<T>& value)
    {
        std::tuple<T, T> parts{};
        read_tuple(in, parts);
        value = std::complex<T>(std::get<0>(parts), std::get<1>(parts));
    }

    // "0101…" 문자열 — 최상위 비트가 앞이다(std::bitset::to_string 과 같은 순서).
    template<std::size_t N>
    void write_bitset(writer& out, const std::bitset<N>& value)
    {
        out.write_string(value.to_string());
    }

    template<std::size_t N>
    void read_bitset(reader& in, std::bitset<N>& value)
    {
        const std::string text = in.read_string();
        if (text.size() != N || text.find_first_not_of("01") != std::string::npos)
        {
            throw serialization_error("expected a string of " + std::to_string(N) + " binary digits");
        }
        value = std::bitset<N>(text);
    }

    // duration 은 자기 단위의 틱 수, time_point 는 시계 기원으로부터의 틱 수다. 단위는
    // 타입이 정하므로 적지 않는다 — 읽는 쪽 타입이 같아야 뜻이 맞는다.
    template<class Duration>
    void write_duration(writer& out, const Duration& value)
    {
        serialize(out, value.count());
    }

    template<class Duration>
    void read_duration(reader& in, Duration& value)
    {
        typename Duration::rep count{};
        deserialize(in, count);
        value = Duration(count);
    }

    template<class TimePoint>
    void write_time_point(writer& out, const TimePoint& value)
    {
        serialize(out, value.time_since_epoch().count());
    }

    template<class TimePoint>
    void read_time_point(reader& in, TimePoint& value)
    {
        typename TimePoint::rep count{};
        deserialize(in, count);
        value = TimePoint(typename TimePoint::duration(count));
    }

    // 일반 형식('/' 구분자)의 UTF-8 로 적는다 — Windows 에서 쓴 경로가 다른 플랫폼에서도 읽힌다.
    inline void write_path(writer& out, const std::filesystem::path& value)
    {
        const std::u8string text = value.generic_u8string();
        out.write_string(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
    }

    inline void read_path(reader& in, std::filesystem::path& value)
    {
        const std::string text = in.read_string();
        value = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    template<class Atomic>
    void write_atomic(writer& out, const Atomic& value)
    {
        serialize(out, value.load());
    }

    template<class Atomic>
    void read_atomic(reader& in, Atomic& value)
    {
        typename Atomic::value_type loaded{};
        deserialize(in, loaded);
        value.store(loaded);
    }
} // namespace reflgen::detail

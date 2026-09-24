#pragma once
// 스칼라 — 정수·실수·문자·열거형·null.
//
// 정수는 데이터 모델의 64비트 값으로 넓혔다가 읽을 때 대상 타입의 범위를 검사한다.
// 좁히며 조용히 잘리는 것을 허용하지 않는다 — 300 이 uint8_t 로 44 가 되는 버그는
// 발견이 가장 늦는 부류다.
#include "reflgen/core/enum.h"
#include "reflgen/core/name.h"
#include "reflgen/serial/detail/utf.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace reflgen::detail
{
template<class T>
[[noreturn]] void throw_out_of_range(std::string value)
{
    throw serialization_error("value " + value + " is out of range for " + std::string(type_name_of<T>()));
}

template<class T>
void write_integer(writer& out, T value)
{
    if constexpr (std::is_signed_v<T>)
    {
        out.write_int(static_cast<std::int64_t>(value));
    }
    else
    {
        out.write_uint(static_cast<std::uint64_t>(value));
    }
}

template<class T>
T read_integer(reader& in)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        const std::uint64_t value = in.read_uint();
        if (value > 1)
        {
            throw_out_of_range<T>(std::to_string(value));
        }
        return value != 0;
    }
    else if constexpr (std::is_signed_v<T>)
    {
        const std::int64_t value = in.read_int();
        if (value < static_cast<std::int64_t>((std::numeric_limits<T>::min)()) ||
            value > static_cast<std::int64_t>((std::numeric_limits<T>::max)()))
        {
            throw_out_of_range<T>(std::to_string(value));
        }
        return static_cast<T>(value);
    }
    else
    {
        const std::uint64_t value = in.read_uint();
        if (value > static_cast<std::uint64_t>((std::numeric_limits<T>::max)()))
        {
            throw_out_of_range<T>(std::to_string(value));
        }
        return static_cast<T>(value);
    }
}

template<class T>
T read_floating(reader& in)
{
    const double value = in.read_float();
    if constexpr (std::numeric_limits<T>::max() < std::numeric_limits<double>::max())
    {
        // 무한·NaN 은 그대로 둔다(바이너리 포맷은 담을 수 있다). 유한값이 넘치는 것만 막는다.
        if (std::isfinite(value) && std::fabs(value) > static_cast<double>((std::numeric_limits<T>::max)()))
        {
            throw_out_of_range<T>(std::to_string(value));
        }
    }
    return static_cast<T>(value);
}

// 문자 하나는 길이 1인 문자열로 적는다 — 정수로 적으면 'A' 가 65 로 남아 파일을
// 읽는 사람이 곤란하다. signed char / unsigned char 는 문자가 아니라 정수로 본다.
template<class C>
void write_character(writer& out, C value)
{
    if constexpr (sizeof(C) == 1)
    {
        const char text = static_cast<char>(value);
        out.write_string(std::string_view(&text, 1));
    }
    else
    {
        std::string text;
        append_utf8(text, static_cast<char32_t>(value));
        out.write_string(text);
    }
}

template<class C>
C read_character(reader& in)
{
    const std::string text = in.read_string();
    if constexpr (sizeof(C) == 1)
    {
        if (text.size() != 1)
        {
            throw serialization_error("expected a single character, got a string of length " +
                                      std::to_string(text.size()));
        }
        return static_cast<C>(text.front());
    }
    else
    {
        const std::basic_string<C> units = from_utf8<C>(text);
        if (units.size() != 1)
        {
            throw serialization_error("expected a single character representable in " + std::string(type_name_of<C>()));
        }
        return units.front();
    }
}

// 이름이 있는 값은 이름으로, 없는 값(비트 플래그 조합, 스캔 범위 밖)은 기저 정수로
// 적는다. 읽을 때는 둘 다 받는다 — 열거자를 새로 추가하기 전에 쓴 파일도 읽힌다.
template<class E>
void write_enum(writer& out, E value)
{
    const std::string_view name = enum_name(value);
    if (!name.empty())
    {
        out.write_string(name);
    }
    else
    {
        write_integer(out, static_cast<std::underlying_type_t<E>>(value));
    }
}

template<class E>
E read_enum(reader& in)
{
    if (in.peek() == value_kind::string)
    {
        const std::string name = in.read_string();
        if (const auto value = enum_cast<E>(name))
        {
            return *value;
        }
        throw serialization_error("unknown enumerator '" + name + "' for " + std::string(type_name_of<E>()));
    }
    return static_cast<E>(read_integer<std::underlying_type_t<E>>(in));
}
} // namespace reflgen::detail

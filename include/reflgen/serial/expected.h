#pragma once
// std::expected (C++23) — {"value": v} 또는 {"error": e}.
//
// C++23 전용 헤더다. C++20 빌드에는 <expected> 가 없으므로 serializer.h 가 이 파일을
// 포함하지 않는다(전처리기로 표준 버전을 가르지 않는다는 원칙). C++23 이상에서
// std::expected 를 직렬화하려면 이 헤더를 포함한다. 포함하지 않고 쓰면 "직렬화할 수
// 없는 타입" static_assert 로 멈춘다 — 조용히 다른 모양으로 적히지 않는다.
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/serializer.h"
#include "reflgen/serial/writer.h"
#include <expected>
#include <string>
#include <type_traits>
#include <utility>

namespace reflgen
{
template<class T, class E>
struct serializer<std::expected<T, E>>
{
    static void write(writer& out, const std::expected<T, E>& value)
    {
        out.begin_object(1);
        if (value.has_value())
        {
            out.write_key("value");
            if constexpr (std::is_void_v<T>)
            {
                out.write_null();
            }
            else
            {
                serialize(out, *value);
            }
        }
        else
        {
            out.write_key("error");
            serialize(out, value.error());
        }
        out.end_object();
    }

    static void read(reader& in, std::expected<T, E>& value)
    {
        in.begin_object();
        std::string key;
        if (!in.next_key(key))
        {
            throw serialization_error("expected an object with a \"value\" or \"error\" key");
        }
        if (key == "value")
        {
            if constexpr (std::is_void_v<T>)
            {
                in.read_null();
                value = std::expected<T, E>();
            }
            else
            {
                T result{};
                deserialize(in, result);
                value = std::expected<T, E>(std::move(result));
            }
        }
        else if (key == "error")
        {
            E error{};
            deserialize(in, error);
            value = std::expected<T, E>(std::unexpect, std::move(error));
        }
        else
        {
            throw serialization_error("expected an object with a \"value\" or \"error\" key");
        }
        if (in.next_key(key))
        {
            throw serialization_error("unexpected key '" + key + "' in an expected object");
        }
        in.end_object();
    }
};
} // namespace reflgen

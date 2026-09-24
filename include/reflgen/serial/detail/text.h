#pragma once
// 문자열 — std::basic_string 전 문자 타입, string_view·C 문자열(쓰기 전용), 문자 배열.
// 데이터 모델의 문자열은 UTF-8 이다. 넓은 문자 타입은 utf.h 가 변환한다.
#include "reflgen/core/name.h"
#include "reflgen/serial/detail/utf.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace reflgen::detail
{
    template<class Char>
    void write_text(writer& out, std::basic_string_view<Char> text)
    {
        if constexpr (std::is_same_v<Char, char>)
        {
            out.write_string(text);
        }
        else
        {
            out.write_string(to_utf8(text));
        }
    }

    template<class String>
    void write_string(writer& out, const String& value)
    {
        using char_type = typename String::value_type;
        write_text(out, std::basic_string_view<char_type>(value.data(), value.size()));
    }

    // 특성(traits)·할당자가 기본이 아닌 문자열도 받는다 — 문자 단위로 옮겨 담는다.
    template<class String>
    void read_string(reader& in, String& value)
    {
        using char_type = typename String::value_type;
        if constexpr (std::is_same_v<String, std::string>)
        {
            value = in.read_string();
        }
        else
        {
            const std::basic_string<char_type> text = from_utf8<char_type>(in.read_string());
            value.assign(text.begin(), text.end());
        }
    }

    template<class Pointer>
    void write_c_string(writer& out, Pointer value)
    {
        if (value == nullptr)
        {
            out.write_null();
            return;
        }
        using char_type = std::remove_cv_t<std::remove_pointer_t<Pointer>>;
        write_text(out, std::basic_string_view<char_type>(value));
    }

    // 고정 길이 문자 배열은 첫 NUL 까지가 문자열이다(C 문자열 버퍼의 관례).
    template<class Char, std::size_t N>
    void write_char_array(writer& out, const Char (&value)[N])
    {
        std::size_t length = 0;
        while (length < N && value[length] != Char{})
        {
            ++length;
        }
        write_text(out, std::basic_string_view<Char>(value, length));
    }

    template<class Char, std::size_t N>
    void read_char_array(reader& in, Char (&value)[N])
    {
        const std::basic_string<Char> text = from_utf8<Char>(in.read_string());
        if (text.size() >= N)
        {
            throw serialization_error("string of length " + std::to_string(text.size()) +
                                      " does not fit a character array of size " + std::to_string(N));
        }
        for (std::size_t i = 0; i < N; ++i)
        {
            value[i] = i < text.size() ? text[i] : Char{};
        }
    }
} // namespace reflgen::detail

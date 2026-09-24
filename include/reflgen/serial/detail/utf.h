#pragma once
// UTF 변환. 직렬화 데이터 모델의 문자열은 UTF-8 이다 — 넓은 문자 타입(char16_t,
// char32_t, wchar_t)은 여기서 UTF-8 로 오가고, wchar_t 는 크기로 UTF-16(Windows)과
// UTF-32(그 밖)를 가른다. <codecvt> 는 C++17 에서 폐기 예정이 되어 쓰지 않는다.
//
// std::string(char)은 검사 없이 바이트 그대로 통과시킨다 — UTF-8 이 아닌 바이트를
// 담는 용도가 흔해서다. 포맷이 UTF-8 을 요구하면(JSON) 그 백엔드가 검사한다.
#include "reflgen/serial/error.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace reflgen::detail
{
    constexpr bool is_surrogate(char32_t code_point) noexcept
    {
        return code_point >= 0xD800 && code_point <= 0xDFFF;
    }

    inline void append_utf8(std::string& out, char32_t code_point)
    {
        if (code_point > 0x10FFFF || is_surrogate(code_point))
        {
            throw serialization_error("invalid Unicode code point");
        }
        if (code_point < 0x80)
        {
            out.push_back(static_cast<char>(code_point));
        }
        else if (code_point < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        else if (code_point < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

    // 과잉 길이 인코딩·서로게이트·범위 초과를 거부한다. position 을 전진시킨다.
    inline char32_t decode_utf8(std::string_view text, std::size_t& position)
    {
        const auto lead = static_cast<unsigned char>(text[position]);
        if (lead < 0x80)
        {
            ++position;
            return lead;
        }

        std::size_t length = 0;
        char32_t code_point = 0;
        char32_t minimum = 0;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            code_point = lead & 0x1F;
            minimum = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            code_point = lead & 0x0F;
            minimum = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            code_point = lead & 0x07;
            minimum = 0x10000;
        }
        else
        {
            throw serialization_error("invalid UTF-8 lead byte");
        }

        if (position + length > text.size())
        {
            throw serialization_error("truncated UTF-8 sequence");
        }
        for (std::size_t i = 1; i < length; ++i)
        {
            const auto continuation = static_cast<unsigned char>(text[position + i]);
            if ((continuation & 0xC0) != 0x80)
            {
                throw serialization_error("invalid UTF-8 continuation byte");
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (code_point < minimum || code_point > 0x10FFFF || is_surrogate(code_point))
        {
            throw serialization_error("invalid UTF-8 sequence");
        }
        position += length;
        return code_point;
    }

    inline void validate_utf8(std::string_view text)
    {
        std::size_t position = 0;
        while (position < text.size())
        {
            decode_utf8(text, position);
        }
    }

    template<class Char>
    std::string to_utf8(std::basic_string_view<Char> text)
    {
        std::string result;
        if constexpr (sizeof(Char) == 1)
        {
            result.assign(reinterpret_cast<const char*>(text.data()), text.size());
        }
        else if constexpr (sizeof(Char) == 2)
        {
            result.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const auto unit = static_cast<char32_t>(static_cast<char16_t>(text[i]));
                if (unit >= 0xD800 && unit <= 0xDBFF)
                {
                    const auto low =
                        i + 1 < text.size() ? static_cast<char32_t>(static_cast<char16_t>(text[i + 1])) : 0;
                    if (low < 0xDC00 || low > 0xDFFF)
                    {
                        throw serialization_error("unpaired UTF-16 surrogate");
                    }
                    append_utf8(result, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                    ++i;
                }
                else
                {
                    append_utf8(result, unit);
                }
            }
        }
        else
        {
            result.reserve(text.size());
            for (const Char unit : text)
            {
                append_utf8(result, static_cast<char32_t>(unit));
            }
        }
        return result;
    }

    template<class Char>
    std::basic_string<Char> from_utf8(std::string_view text)
    {
        std::basic_string<Char> result;
        if constexpr (sizeof(Char) == 1)
        {
            // char8_t 는 이름 그대로 UTF-8 이라는 약속이므로 검사한다. char 는 통과.
            if constexpr (!std::is_same_v<Char, char>)
            {
                validate_utf8(text);
            }
            result.assign(reinterpret_cast<const Char*>(text.data()), text.size());
        }
        else
        {
            result.reserve(text.size());
            std::size_t position = 0;
            while (position < text.size())
            {
                const char32_t code_point = decode_utf8(text, position);
                if constexpr (sizeof(Char) == 2)
                {
                    if (code_point >= 0x10000)
                    {
                        const char32_t offset = code_point - 0x10000;
                        result.push_back(static_cast<Char>(0xD800 + (offset >> 10)));
                        result.push_back(static_cast<Char>(0xDC00 + (offset & 0x3FF)));
                    }
                    else
                    {
                        result.push_back(static_cast<Char>(code_point));
                    }
                }
                else
                {
                    result.push_back(static_cast<Char>(code_point));
                }
            }
        }
        return result;
    }
} // namespace reflgen::detail

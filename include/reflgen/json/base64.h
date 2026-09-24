#pragma once
// base64 (RFC 4648, 패딩 있음) — JSON 에 바이트열을 담는 데 쓴다.
#include "reflgen/serial/error.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reflgen::json::detail
{
    inline constexpr std::string_view base64_alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline std::string base64_encode(std::span<const std::byte> bytes)
    {
        std::string result;
        result.reserve((bytes.size() + 2) / 3 * 4);
        std::size_t i = 0;
        for (; i + 3 <= bytes.size(); i += 3)
        {
            const std::uint32_t chunk = (std::to_integer<std::uint32_t>(bytes[i]) << 16) |
                                        (std::to_integer<std::uint32_t>(bytes[i + 1]) << 8) |
                                        std::to_integer<std::uint32_t>(bytes[i + 2]);
            result += base64_alphabet[(chunk >> 18) & 0x3F];
            result += base64_alphabet[(chunk >> 12) & 0x3F];
            result += base64_alphabet[(chunk >> 6) & 0x3F];
            result += base64_alphabet[chunk & 0x3F];
        }
        const std::size_t rest = bytes.size() - i;
        if (rest > 0)
        {
            std::uint32_t chunk = std::to_integer<std::uint32_t>(bytes[i]) << 16;
            if (rest == 2)
            {
                chunk |= std::to_integer<std::uint32_t>(bytes[i + 1]) << 8;
            }
            result += base64_alphabet[(chunk >> 18) & 0x3F];
            result += base64_alphabet[(chunk >> 12) & 0x3F];
            result += rest == 2 ? base64_alphabet[(chunk >> 6) & 0x3F] : '=';
            result += '=';
        }
        return result;
    }

    constexpr int base64_value(char c) noexcept
    {
        if (c >= 'A' && c <= 'Z')
        {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z')
        {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9')
        {
            return c - '0' + 52;
        }
        if (c == '+')
        {
            return 62;
        }
        if (c == '/')
        {
            return 63;
        }
        return -1;
    }

    // 엄격하게 푼다 — 길이가 4의 배수가 아니거나 패딩 뒤에 글자가 있으면 실패.
    inline std::vector<std::byte> base64_decode(std::string_view text)
    {
        if (text.size() % 4 != 0)
        {
            throw serialization_error("invalid base64 length");
        }
        std::vector<std::byte> result;
        result.reserve(text.size() / 4 * 3);
        for (std::size_t i = 0; i < text.size(); i += 4)
        {
            const bool last = i + 4 == text.size();
            const std::size_t padding = last ? (text[i + 3] == '=' ? (text[i + 2] == '=' ? 2 : 1) : 0) : 0;
            std::uint32_t chunk = 0;
            for (std::size_t j = 0; j < 4; ++j)
            {
                const char c = text[i + j];
                if (j >= 4 - padding)
                {
                    chunk <<= 6;
                    continue;
                }
                const int value = base64_value(c);
                if (value < 0)
                {
                    throw serialization_error("invalid base64 character");
                }
                chunk = (chunk << 6) | static_cast<std::uint32_t>(value);
            }
            result.push_back(static_cast<std::byte>((chunk >> 16) & 0xFF));
            if (padding < 2)
            {
                result.push_back(static_cast<std::byte>((chunk >> 8) & 0xFF));
            }
            if (padding < 1)
            {
                result.push_back(static_cast<std::byte>(chunk & 0xFF));
            }
        }
        return result;
    }
} // namespace reflgen::json::detail

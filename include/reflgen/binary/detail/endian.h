#pragma once
// 실수 wire 형식은 언제나 little-endian. 표준 byteswap이 있으면 고정 폭 복사로 처리한다.
#include "reflgen/core/features.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace reflgen::binary::detail
{
    constexpr std::array<std::byte, 8> little_bytes(std::uint64_t value) noexcept
    {
#if REFLGEN_HAS_BYTESWAP
        if constexpr (std::endian::native == std::endian::little || std::endian::native == std::endian::big)
        {
            if constexpr (std::endian::native == std::endian::big)
                value = std::byteswap(value);
            return std::bit_cast<std::array<std::byte, 8>>(value);
        }
        else
#endif
        {
            std::array<std::byte, 8> bytes{};
            for (unsigned i = 0; i < 8; ++i)
                bytes[i] = static_cast<std::byte>((value >> (i * 8)) & 0xff);
            return bytes;
        }
    }

    inline std::uint64_t little_value(const std::byte* bytes) noexcept
    {
#if REFLGEN_HAS_BYTESWAP
        if constexpr (std::endian::native == std::endian::little || std::endian::native == std::endian::big)
        {
            std::uint64_t value;
            std::memcpy(&value, bytes, sizeof(value)); // 정렬되지 않은 입력도 허용한다.
            if constexpr (std::endian::native == std::endian::big)
                value = std::byteswap(value);
            return value;
        }
        else
#endif
        {
            std::uint64_t value = 0;
            for (unsigned i = 0; i < 8; ++i)
                value |= std::to_integer<std::uint64_t>(bytes[i]) << (i * 8);
            return value;
        }
    }
} // namespace reflgen::binary::detail

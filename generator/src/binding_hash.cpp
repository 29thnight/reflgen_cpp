#include "binding_hash.h"
#include <array>
#include <bit>
#include <cstdint>
#include <vector>

namespace reflgen::generator
{
    std::string binding_sha256(std::string_view bytes)
    {
        constexpr std::array<std::uint32_t, 64> constants{
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
        const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
        data.push_back(0x80);
        while (data.size() % 64 != 56)
            data.push_back(0);
        for (int shift = 56; shift >= 0; shift -= 8)
            data.push_back(static_cast<std::uint8_t>(bit_length >> shift));
        for (std::size_t offset = 0; offset < data.size(); offset += 64)
        {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t i = 0; i < 16; ++i)
                for (std::size_t j = 0; j < 4; ++j)
                    words[i] = (words[i] << 8) | data[offset + i * 4 + j];
            for (std::size_t i = 16; i < 64; ++i)
            {
                const auto x = words[i - 15];
                const auto y = words[i - 2];
                words[i] = words[i - 16] + (std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3)) + words[i - 7] +
                           (std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10));
            }
            auto work = state;
            for (std::size_t i = 0; i < 64; ++i)
            {
                const auto a = work[0], b = work[1], c = work[2], d = work[3];
                const auto e = work[4], f = work[5], g = work[6], h = work[7];
                const auto t1 = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                                constants[i] + words[i];
                const auto t2 = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                work = {t1 + t2, a, b, c, d + t1, e, f, g};
            }
            for (std::size_t i = 0; i < state.size(); ++i)
                state[i] += work[i];
        }
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        for (const auto word : state)
            for (int shift = 28; shift >= 0; shift -= 4)
                result += hex[(word >> shift) & 15];
        return result;
    }
} // namespace reflgen::generator

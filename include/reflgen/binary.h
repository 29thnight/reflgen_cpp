#pragma once
// 바이너리 백엔드의 편의 함수.
//
//   std::vector<std::byte> bytes = reflgen::binary::to_bytes(player);
//   auto loaded = reflgen::binary::from_bytes<game::player>(bytes);
#include "reflgen/binary/reader.h"
#include "reflgen/binary/writer.h"
#include "reflgen/serial/serializer.h"
#include <concepts>
#include <cstddef>
#include <span>
#include <vector>

namespace reflgen::binary
{
template<class T>
std::vector<std::byte> to_bytes(const T& value)
{
    std::vector<std::byte> bytes;
    writer out(bytes);
    serialize(out, value);
    return bytes;
}

template<class T>
void from_bytes(std::span<const std::byte> bytes, T& value, std::size_t max_depth = reader::default_max_depth)
{
    reader in(bytes, max_depth);
    deserialize(in, value);
    in.finish();
}

template<class T>
    requires std::default_initializable<T>
T from_bytes(std::span<const std::byte> bytes, std::size_t max_depth = reader::default_max_depth)
{
    T value{};
    from_bytes(bytes, value, max_depth);
    return value;
}
} // namespace reflgen::binary

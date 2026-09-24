#pragma once
// JSON 백엔드의 편의 함수.
//
//   std::string text = reflgen::json::to_string(player, 4);
//   auto loaded = reflgen::json::from_string<game::player>(text);
#include "reflgen/json/reader.h"
#include "reflgen/json/writer.h"
#include "reflgen/serial/serializer.h"
#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>

namespace reflgen::json
{
template<class T>
std::string to_string(const T& value, std::size_t indent = 0, std::size_t max_depth = writer::default_max_depth)
{
    std::string text;
    writer out(text, indent, max_depth);
    serialize(out, value);
    return text;
}

// 제자리 읽기 — 입력에 없는 필드는 value 의 기존 값을 유지한다.
template<class T>
void from_string(std::string_view text, T& value, std::size_t max_depth = reader::default_max_depth)
{
    reader in(text, max_depth);
    deserialize(in, value);
    in.finish();
}

template<class T>
    requires std::default_initializable<T>
T from_string(std::string_view text, std::size_t max_depth = reader::default_max_depth)
{
    T value{};
    from_string(text, value, max_depth);
    return value;
}
} // namespace reflgen::json

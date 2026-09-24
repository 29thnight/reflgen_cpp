#pragma once
// 실패 경로 채우기 — 한 단계를 감싸 실패가 지나갈 때 자기 조각을 앞에 붙인다.
// try 블록은 예외가 없으면 비용이 없다(x64 테이블 기반 예외 처리).
#include "reflgen/serial/error.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace reflgen::detail
{
template<class F>
void with_path(std::string_view segment, F&& action)
{
    try
    {
        std::forward<F>(action)();
    }
    catch (const serialization_error& error)
    {
        throw error.with_parent(segment);
    }
}

template<class F>
void with_index(std::size_t index, F&& action)
{
    try
    {
        std::forward<F>(action)();
    }
    catch (const serialization_error& error)
    {
        throw error.with_parent(std::to_string(index));
    }
}
} // namespace reflgen::detail

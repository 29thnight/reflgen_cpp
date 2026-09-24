#pragma once
// 실패 경로 쌓기 — 한 단계에 들어갈 때 조각을 올리고 나올 때 내린다(RAII). 실패하면
// serialization_error 가 생성되는 순간 이 stack 을 복사해 path() 를 만든다(error.h).
// catch 도 재던지기도 없다 — 이유는 error.h 머리말.
#include "reflgen/serial/error.h"
#include <cstddef>
#include <string_view>
#include <utility>

namespace reflgen::detail
{
class path_scope
{
  public:
    explicit path_scope(std::string_view key) { active_path.push_back({key, 0, false}); }
    explicit path_scope(std::size_t index) { active_path.push_back({{}, index, true}); }
    ~path_scope() { active_path.pop_back(); }

    path_scope(const path_scope&) = delete;
    path_scope& operator=(const path_scope&) = delete;
};

template<class F>
void with_path(std::string_view segment, F&& action)
{
    const path_scope scope(segment);
    std::forward<F>(action)();
}

template<class F>
void with_index(std::size_t index, F&& action)
{
    const path_scope scope(index);
    std::forward<F>(action)();
}
} // namespace reflgen::detail

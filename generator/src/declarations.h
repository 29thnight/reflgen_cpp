#pragma once
// 반영 선언을 다른 도구에 전달하는 선택적 JSON 형식. ABI·핸들·대상 언어 정책은 소비자가 결정한다.
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    inline constexpr int declarations_format_version = 1;

    std::string emit_declarations_json(const std::string& module_name, const std::vector<header_model>& headers,
                                       const target_model& target);
} // namespace reflgen::generator

#pragma once
// 출력 — 모델을 C++ 소스 텍스트로 만든다. 파일 쓰기는 하지 않는다(시험하기 쉽게).
//
//   <stem>.reflgen.h       header 하나에 대응. reflection<T> 특수화를 담는다.
//                          원본 header 끝에서 #include 한다.
//   reflgen_<module>.h/.cpp  module 등록 함수 — reflgen::generated::register_<module>()
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    std::string emit_header(const header_model& header, const std::string& generated_path);

    std::string emit_module_header(const std::string& module_name);

    std::string emit_module_source(const std::string& module_name, const std::vector<header_model>& headers,
                                   const std::vector<std::string>& generated_names);
} // namespace reflgen::generator

#pragma once
// 출력 — 모델을 C++ 소스 텍스트로 만든다. 파일 쓰기는 하지 않는다(시험하기 쉽게).
//
//   <stem>.reflgen.h         header 하나에 대응. 원본 header 를 include 하고 reflection<T> 특수화를 담는다.
//   reflgen_<module>.h       주입 header — 생성 파일을 모두 include 하고 등록 함수를 선언한다. 빌드 시스템이
//                            모든 번역 단위에 강제 include 한다(원본 header 는 아무것도 include 하지 않는다).
//   reflgen_<module>.cpp     등록 함수 reflgen::generated::register_<module>() 의 정의.
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    std::string emit_header(const header_model& header, const std::string& generated_path);

    std::string emit_module_header(const std::string& module_name, const std::vector<std::string>& generated_paths);

    std::string emit_module_source(const std::string& module_name, const std::vector<header_model>& headers);
} // namespace reflgen::generator

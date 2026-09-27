#pragma once
// 출력 — 모델을 C++ 소스 텍스트로 만든다. 파일 쓰기는 하지 않는다(시험하기 쉽게).
//
//   <stem>.reflgen.h         header 하나에 대응. 타입을 전방 선언하고 reflection<T> 부분 특수화를 담는다 — 원본
//                            header 는 전방 선언으로 설 수 없을 때(중첩 타입 등)만 include 한다.
//   reflgen_<module>.h       주입 header — 생성 파일을 모두 include 하고 등록 함수를 선언한다. 빌드 시스템이
//                            모든 번역 단위에 강제 include 한다(원본 header 는 아무것도 include 하지 않는다).
//   reflgen_<module>.cpp     등록 함수 reflgen::generated::register_<module>() 의 정의.
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    std::string emit_header(const header_model& header, const std::string& generated_path);

    // attribute_headers: 사용자 이름공간의 attribute 타입을 정의한 header(절대 경로) — 생성 코드가 그 타입을 쓴다.
    std::string emit_module_header(const std::string& module_name, const std::vector<std::string>& generated_paths,
                                   const std::vector<std::string>& attribute_headers);

    std::string emit_module_source(const std::string& module_name, const std::vector<header_model>& headers);
} // namespace reflgen::generator

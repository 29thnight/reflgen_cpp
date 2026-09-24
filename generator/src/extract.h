#pragma once
// 추출 — header 들을 한 번에 파싱해 [[reflgen::reflect]] 타입을 모델로 옮긴다.
#include "diagnostics.h"
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    struct extract_options
    {
        std::vector<std::string> headers;          // 정규화한 절대 경로
        std::vector<std::string> clang_arguments;  // -I, -D, -std 등(빌드 시스템이 준다)
        std::vector<std::string> attribute_scopes; // reflgen 외에 옮길 attribute 이름공간
        std::string output_directory;              // 정규화한 절대 경로
        std::string module_name;
    };

    struct extract_result
    {
        std::vector<header_model> headers;      // header 마다 하나씩, 입력 순서대로(반영할 것이 없어도 있다)
        std::vector<std::string> dependencies;  // 파싱에 읽힌 파일 전부 — 빌드 시스템의 depfile 용
        std::vector<attribute_info> attributes; // 편집기 자동완성용 attribute 카탈로그
    };

    // 오류가 보고되면 결과를 쓰지 않는다.
    extract_result extract(const extract_options& options, diagnostics& report);

    // 생성 파일 이름 규칙: player.h → player.reflgen.h
    std::string generated_header_name(const std::string& header_path);
} // namespace reflgen::generator

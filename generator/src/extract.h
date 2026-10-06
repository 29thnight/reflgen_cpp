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
        // 사용자 이름공간의 attribute 타입을 정의한 header — 주입 header 가 include 한다. 없으면 사용자 attribute 를
        // 쓰는 타입은 원본 header 가 필요하다(생성 코드가 attribute 타입을 볼 길이 그것뿐이다).
        std::vector<std::string> attribute_headers;
        // 등록 함수의 번역 단위(reflgen_<module>.cpp)가 맨 앞에서 include 하는 header — 사용자 serializer 특수화처럼
        // 등록소의 서술자가 보아야 하는데 반영 타입의 header 가 include 하지 않는 것. 생성에는 쓰지 않는다.
        std::vector<std::string> registration_headers;
        std::string output_directory; // 정규화한 절대 경로
        std::string module_name;
        // clang 내장 header 를 담은 resource 디렉터리(그 아래 include/). 비면 libclang 이 스스로 찾는다.
        std::string resource_directory;
        bool collect_interop = false; // 별도로 opt-in 한 내보내기 선언도 읽는다. reflection 선택에는 영향이 없다.
    };

    struct extract_result
    {
        std::vector<header_model> headers;      // header 마다 하나씩, 입력 순서대로(반영할 것이 없어도 있다)
        std::vector<std::string> dependencies;  // 파싱에 읽힌 파일 전부 — 빌드 시스템의 depfile 용
        std::vector<attribute_info> attributes; // 편집기 자동완성용 attribute 카탈로그
        target_model target;
        std::vector<class_model> exports;
    };

    // 오류가 보고되면 결과를 쓰지 않는다.
    extract_result extract(const extract_options& options, diagnostics& report);

    // 생성 파일 이름 규칙: player.h → player.reflgen.h
    std::string generated_header_name(const std::string& header_path);
} // namespace reflgen::generator

#pragma once
// reflgen 의 판. 판은 여기 한 곳에서만 정한다 — CMake project(VERSION) 는 이 파일을 읽고, 생성기 --version 은 이
// 값을 찍으며, VSIX 매니페스트는 같은 값이어야 한다(시험 reflgen_version_consistency 가 맞춰 본다).
#include <string_view>

namespace reflgen
{
    inline constexpr int version_major = 1;
    inline constexpr int version_minor = 0;
    inline constexpr int version_patch = 0;
    inline constexpr std::string_view version_string = "1.0.0";

    // 생성 코드의 형식. 생성기가 만드는 코드가 기대는 header 의 모양이 바뀌면 올린다. 생성기는 자기 형식을 주입
    // header 의 static_assert 에 적으므로, 다른 형식의 생성기로 만든 코드는 컴파일에서 멈춘다 — 생성기와 header 의
    // 판이 어긋난 설치(옛 생성기 + 새 header 등)를 조용히 넘기지 않는다.
    //   2: 생성된 reflection<T> 가 detail::complete_type·generated_description 을 쓴다(불완전 타입 진단).
    inline constexpr int generated_code_format = 2;
} // namespace reflgen

#pragma once
// MSBuild 연동 시험의 라이브러리 쪽 — 이 프로젝트를 참조하는 실행 파일이 이 header 로 reflection 을 쓴다.
// 생성 코드는 라이브러리의 IntDir 에 있으므로 참조하는 쪽에 주입이 전해져야 한다.
#include "reflgen/reflgen.h"

namespace msbuild_lib
{
    struct [[reflgen::reflect("lib.point")]] point
    {
        int x = 1;
        int y = 2;
    };

    int length_squared(const point& value);
} // namespace msbuild_lib

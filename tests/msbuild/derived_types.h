#pragma once
// 라이브러리 타입을 부모로 둔 반영 타입 — 부모의 서술(라이브러리의 주입)이 이 프로젝트의 주입보다 먼저 와야 한다.
#include "lib_types.h"

namespace msbuild_app
{
    struct [[reflgen::reflect("app.tagged_point")]] tagged_point : msbuild_lib::point
    {
        int tag = 7;
    };
} // namespace msbuild_app

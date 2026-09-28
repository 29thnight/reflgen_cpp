#pragma once
// NO_REGISTRATION 시험 — 서술만 쓰는 target(자기 직렬화·등록을 가진 엔진처럼). 반영 타입이 reflgen 의 직렬화기가
// 없는 타입을 품는다(게임 엔진의 Material → MaterialInfomation → 수학 벡터 모양) — 그런 코드베이스의 흔한 모양이다.
// 등록 함수가 target 에서 빠졌는지는 check_no_registration.cmake 가 본다.
#include "reflgen/reflgen.h"

namespace description_only
{
    struct opaque_handle
    {
        void* native = nullptr;
    };

    struct [[reflgen::reflect]] native_handle
    {
        opaque_handle raw;
    };

    struct [[reflgen::reflect]] handle_holder
    {
        native_handle handle;
        int generation = 1;
    };
} // namespace description_only

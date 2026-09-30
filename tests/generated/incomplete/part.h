#pragma once
// 불완전 타입 진단 시험 — 다른 모듈(target)의 반영 타입. holder.h 는 이 타입을 전방 선언만 한다(엔진의 Material).

namespace incomplete_tests
{
    struct [[reflgen::reflect]] part
    {
        int power = 1;
    };
} // namespace incomplete_tests

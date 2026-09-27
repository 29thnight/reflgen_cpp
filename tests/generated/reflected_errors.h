#pragma once
// 생성기 시험(check_tolerance.cmake) — 반영 선언 안의 오류. clang 이 이 클래스를 잘못 읽었을 수 있으므로(멤버가
// 빠지거나 타입이 바뀐다) 생성하지 않고 실패해야 한다. 이 header 는 컴파일하지 않는다.
#include "reflgen/reflgen.h"

namespace reflected_errors
{
    struct [[reflgen::reflect]] broken
    {
        int value = 1;
        undeclared_type member;
    };

    // 오류는 반영 선언 밖(부모)에 있지만 clang 은 그 때문에 파생 클래스도 무효로 본다 — 부모를 건너뛰고 읽었을 수 있다.
    struct broken_base
    {
        another_undeclared_type member;
    };

    struct [[reflgen::reflect]] derived : broken_base
    {
        int value = 1;
    };
} // namespace reflected_errors

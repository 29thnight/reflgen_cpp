#pragma once
// 생성기 시험(check_tolerance.cmake) — clang 이 오류를 내지만 반영 선언에는 닿지 않는 header. MSVC 로만 빌드하는
// 코드베이스(게임 엔진)에 흔하다: __FUNCSIG__ 표기를 못 박은 static_assert, clang 이 상수 식으로 받지 않는 enum
// 캐스트 같은 것. 생성기는 이 오류들을 넘기고 반영 선언을 생성해야 한다. 이 header 는 컴파일하지 않는다.
#include "reflgen/reflgen.h"
#include <cstddef>
#include <immintrin.h>
#include <utility>

namespace tolerated_errors
{
    // clang 의 내장 header 를 보는가 — 생성기 옆의 clang/include 를 -resource-dir 로 준다. 못 찾으면 MSVC 의
    // xmmintrin.h 가 쓰여 __m128 이 union 이 되고(첨자를 못 쓴다) 아래 단정이 넘긴 오류 수에 하나 더해진다.
    template<class V>
    concept subscriptable = requires(V value) { value[0]; };
    static_assert(subscriptable<__m128>, "clang's builtin headers were not used");

    // clang 의 식 중첩 한도(-fbracket-depth)를 넘는 fold — MSVC 에는 없는 한도라 MSVC 로만 빌드하는 코드에 나온다
    // (CreatorEngine 의 enum 스캔은 값 257 개를 한 fold 로 편다). 기본 한도가 clang 판마다 다르고(20 은 256, 22 는
    // 2048) 넘으면 치명 오류로 파싱이 멈추므로, 생성기가 판과 무관하게 넉넉히(4096) 준다. 3000 은 22 의 기본도 넘는다.
    template<std::size_t... I>
    constexpr std::size_t count_all(std::index_sequence<I...>)
    {
        return (std::size_t(I < 100000) + ... + std::size_t(0));
    }
    static_assert(count_all(std::make_index_sequence<3000>{}) == 3000);

    // 반영 선언 밖의 오류 21 개 — clang 의 기본 한도(20)를 넘는다. 한도에서 파싱을 멈추면 뒤의 survivor 를 놓친다.
    constexpr int clang_only = 0;
    static_assert(clang_only == 1, "1");
    static_assert(clang_only == 2, "2");
    static_assert(clang_only == 3, "3");
    static_assert(clang_only == 4, "4");
    static_assert(clang_only == 5, "5");
    static_assert(clang_only == 6, "6");
    static_assert(clang_only == 7, "7");
    static_assert(clang_only == 8, "8");
    static_assert(clang_only == 9, "9");
    static_assert(clang_only == 10, "10");
    static_assert(clang_only == 11, "11");
    static_assert(clang_only == 12, "12");
    static_assert(clang_only == 13, "13");
    static_assert(clang_only == 14, "14");
    static_assert(clang_only == 15, "15");
    static_assert(clang_only == 16, "16");
    static_assert(clang_only == 17, "17");
    static_assert(clang_only == 18, "18");
    static_assert(clang_only == 19, "19");
    static_assert(clang_only == 20, "20");
    static_assert(clang_only == 21, "21");

    struct [[reflgen::reflect]] survivor
    {
        int value = 1;
        __m128 lanes;
    };
} // namespace tolerated_errors

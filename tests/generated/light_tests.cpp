// 가벼운 주입 — 이 번역 단위는 game_types.h 를 include 하지 않는다. 주입 header 는 반영하는 타입을 전방 선언만
// 하므로 그 타입들은 여기서 불완전해야 한다(원본 header 를 모든 번역 단위에 끌고 다니지 않는다). 전방 선언할 수
// 없는 타입이 든 fallback_types.h 만 주입이 include 한다.
#include "harness.h"

namespace
{
    template<class T>
    concept complete = requires { sizeof(T); };

    // game_types.h 의 타입 — 이름은 보이지만(전방 선언) 정의는 없다.
    static_assert(!complete<generated_tests::entity>, "the injection must not include game_types.h");
    static_assert(!complete<generated_tests::stats>, "the injection must not include game_types.h");
    static_assert(!complete<generated_tests::stamped_hero>, "the injection must not include game_types.h");

    // fallback_types.h 는 주입이 include 한다 — 그 타입은 완전하다.
    static_assert(complete<generated_tests::crate>, "fallback_types.h is included by the injection");
    static_assert(complete<generated_tests::limits::slot>, "fallback_types.h is included by the injection");

    const reflgen_test::test light_injection("generated: the injection forward-declares instead of including", [] {
        reflgen_test::check(true); // 위의 static_assert 가 시험이다 — 이 번역 단위가 컴파일되면 통과
    });
} // namespace

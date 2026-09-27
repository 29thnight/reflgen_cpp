#pragma once
// 전방 선언만으로는 서술을 세울 수 없는 타입 — 생성 header 가 이 파일을 include 한다(가벼운 주입의 예외).
//   · 클래스 안에 선언한 타입(limits::slot) — 전방 선언할 수 없다
//   · attribute 인자가 이름공간 상수를 쓴다(crate) — 생성 코드가 원본 header 없이는 그 이름을 찾지 못한다
#include "reflgen/reflgen.h"
#include <string>

namespace generated_tests
{
    constexpr int crate_capacity = 12;

    struct [[reflgen::reflect]] crate
    {
        [[reflgen::range(0, crate_capacity)]] int count = 3;
    };

    struct limits
    {
        static constexpr int cap = 64;

        // attribute 인자는 자기 클래스와 감싸는 클래스의 이름을 한정 없이 쓴다 — 생성 코드는 클래스
        // 밖에 있으므로 생성기가 한정해 준다.
        struct [[reflgen::reflect]] slot
        {
            static constexpr int floor = 1;

            [[reflgen::range(floor, cap)]] int count = 1;
            // 이름 뒤 attribute 는 그 declarator 에만 붙는다 — scratch 만 빠지고 weight 는 남는다.
            int scratch [[reflgen::ignore]] = 0, weight = 2;
            [[reflgen::display_name("Tag" // 인자 안의 주석은 생성 코드로 옮겨지지 않는다
                                    )]] std::string tag;

            [[reflgen::reflect]] explicit operator bool() const { return count != 0; }
        };
    };
} // namespace generated_tests

#pragma once
// 생성기 진단 시험용 — 일부러 잘못 쓴 선언들. 컴파일하지 않고 생성기에만 준다.
// 생성 파일을 include 하지 않은 것도 의도다(RG0003).
#include "reflgen/reflgen.h"

namespace bad
{
    // RG0002: 반영할 private 멤버가 있는데 friend struct reflgen::access; 가 없다.
    class [[reflgen::reflect]] hidden_without_friend
    {
        int secret_ = 0;
    };

    struct [[reflgen::reflect]] odd_members
    {
        int bits : 3;   // RG0004: bit-field
        int& reference; // RG0004: 참조 멤버
        [[reflgen::ignore]] int ignored_bits : 2;

        [[reflgen::reflect]] void fire(int shots); // RG0006: 오버로드
        void fire(double power);
    };

    // RG0005: 클래스 template
    template<class T>
    struct [[reflgen::reflect]] box
    {
        T value;
    };

    namespace
    {
        // RG0005: 익명 이름공간
        struct [[reflgen::reflect]] local
        {
            int value = 0;
        };
    } // namespace

    // RG0002: 중첩 클래스의 friend 는 바깥 클래스의 것이 아니다.
    struct [[reflgen::reflect]] leaky_outer
    {
        struct inner
        {
            friend struct reflgen::access;
        };

      private:
        int outer_secret = 0;
    };

    // RG0005: union — 어느 멤버가 살아 있는지 직렬화기가 알 수 없다.
    union [[reflgen::reflect]] either {
        int i;
        float f;
    };

    struct [[reflgen::reflect]] odd_functions
    {
        [[reflgen::reflect]] odd_functions() = default; // RG0004: 생성자

        template<class T>
        [[reflgen::reflect]] void accept(T value); // RG0005: 멤버 함수 template

        [[reflgen::reflect]] static constexpr int limit = 3; // RG0004: static data member
    };
} // namespace bad

#pragma once
// 불완전 타입 진단 시험 — 다른 모듈의 반영 타입을 전방 선언만 하고 필드로 든다(엔진의 MeshRenderer 가
// std::shared_ptr<Material> 을 드는 모양). 이 모듈의 등록 함수는 자기 header 만 include 하므로 part 가 불완전하다 —
// REGISTRATION_HEADERS 로 part.h 를 넘기지 않으면 컴파일이 reflgen 의 메시지로 멈춰야 한다.
#include <memory>

namespace incomplete_tests
{
    struct part;

    struct [[reflgen::reflect]] holder
    {
        std::shared_ptr<part> item;
    };
} // namespace incomplete_tests

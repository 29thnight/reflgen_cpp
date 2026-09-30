#pragma once
// 불완전 타입 진단 시험 — 서술하지 않은 불완전 타입(pimpl)을 가리키는 스마트 포인터 필드. 서술을 쓰는 자리(등록 함수)가
// 직렬화 가능성을 판정할 수 없으므로 reflgen 의 메시지로 멈춰야 한다 — 고칠 곳은 정의를 보이거나 필드를
// [[reflgen::ignore]] 로 빼는 것이다.
#include <memory>

namespace incomplete_tests
{
    struct opaque;

    struct [[reflgen::reflect]] opaque_holder
    {
        std::shared_ptr<opaque> handle;
    };
} // namespace incomplete_tests

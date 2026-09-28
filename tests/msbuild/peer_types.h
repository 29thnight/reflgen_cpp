#pragma once
// ReflgenReference 시험 — 이 라이브러리(reflgen_msbuild_peer)와 reflgen_msbuild_lib 는 서로의 header 를 include 한다.
// ProjectReference 로는 이을 수 없는 순환이라 둘은 ReflgenReference 로 서로의 서술만 받는다(빌드 순서는 걸지 않는다).
#include "lib_types.h"
#include <string>

namespace msbuild_peer
{
    struct [[reflgen::reflect("peer.badge")]] badge
    {
        msbuild_lib::point where;
        int rank = 3;
    };

    // lib 의 타입을 lib 가 생성한 서술로 직렬화한다 — 이 라이브러리의 번역 단위에 lib 의 주입이 와야 한다.
    std::string point_text();

    // reflgen 의 직렬화기가 없는 타입을 반영 타입이 품는다(엔진의 Material → MaterialInfomation → math::vector4 모양).
    // 이 라이브러리는 서술만 쓰고 reflgen 의 등록소·직렬화는 쓰지 않는다(ReflgenRegistration=false) — 등록 함수가
    // 컴파일되지 않는지는 run_msbuild_test.cmake 가 본다.
    struct opaque_handle
    {
        void* native = nullptr;
    };

    struct [[reflgen::reflect("peer.native_handle")]] native_handle
    {
        opaque_handle raw;
    };

    struct [[reflgen::reflect("peer.handle_holder")]] handle_holder
    {
        native_handle handle;
        int generation = 1;
    };
} // namespace msbuild_peer

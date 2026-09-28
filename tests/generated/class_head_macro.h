#pragma once
// 클래스 머리를 매크로로 적는 코드의 시험(game_types.h 의 aligned_block) — 매크로가 다른 header 에 있어야 한다.
// 그래야 선언 시작의 spelling 위치가 이 파일을 가리켜, 사용 자리(game_types.h)와 갈린다(엔진의 cbuffer 가 그렇다).
#define GENERATED_TESTS_ALIGNED_STRUCT struct alignas(4)

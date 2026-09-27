#pragma once
// 패키지 소비 시험의 타입 — attribute 만 달았다.
#include "reflgen/reflgen.h"
#include <string>

namespace consumer
{
    struct [[reflgen::reflect("consumer.player")]] player
    {
        std::string name = "ada";
        [[reflgen::range(0, 100)]] int level = 7;
    };
} // namespace consumer

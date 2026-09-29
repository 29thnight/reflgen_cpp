#pragma once
// 등록 header 시험 — 사용자 serializer 특수화를 타입의 header 가 아닌 곳에 두는 코드(게임 엔진의 직렬화 규칙 모음).
// reflgen_generate(… REGISTRATION_HEADERS 이 파일) 이 등록 함수의 번역 단위 맨 앞에서 include 하게 한다 — 그래야
// 등록소의 서술자가 이 특수화로 필드를 쓰고 읽는다.
#include "game_types.h"
#include "reflgen/reflgen.h"
#include <string>

// tint 는 "#r,g,b" 문자열 하나로 적는다.
template<>
struct reflgen::serializer<generated_tests::tint>
{
    static void write(reflgen::writer& out, const generated_tests::tint& value)
    {
        out.write_string("#" + std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b));
    }

    static void read(reflgen::reader& in, generated_tests::tint& value)
    {
        const std::string text = in.read_string();
        if (text.empty() || text.front() != '#')
        {
            throw reflgen::serialization_error("a tint starts with '#'");
        }
        const std::size_t first = text.find(',');
        const std::size_t second = text.find(',', first + 1);
        value.r = std::stof(text.substr(1, first - 1));
        value.g = std::stof(text.substr(first + 1, second - first - 1));
        value.b = std::stof(text.substr(second + 1));
    }
};

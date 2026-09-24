// MSBuild 연동 시험 — reflgen.targets 가 생성한 서술로 직렬화와 등록이 되는가. game_types.h 도 이 파일도 생성
// 파일을 include 하지 않는다 — 주입 header 가 강제 include 로 들어온다(등록 함수 선언도 거기 있다).
#include "pch.h"
#include "game_types.h"
#include "reflgen/json.h"

int main()
{
    static_assert(reflgen::reflectable<generated_tests::stats>);
    static_assert(reflgen::reflectable<generated_tests::limits::slot>);

    reflgen::generated::register_msbuild_test();
    const bool registered = reflgen::default_registry().find("tests.hero") != nullptr;

    generated_tests::stats stats;
    stats.level = 3;
    const std::string text = reflgen::json::to_string(stats);
    const bool serialized = text == R"({"level":3,"health":100.0,"secret":7})";

    std::printf("%s\nmsbuild integration: %s\n", text.c_str(), registered && serialized ? "ok" : "FAILED");
    return registered && serialized ? 0 : 1;
}

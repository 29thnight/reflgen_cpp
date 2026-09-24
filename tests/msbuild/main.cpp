// MSBuild 연동 시험 — reflgen.targets 가 생성한 서술로 직렬화와 등록이 되는가.
#include "game_types.h"
#include "reflgen/json.h"
#include "reflgen_msbuild_test.h"
#include <cstdio>
#include <string>

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

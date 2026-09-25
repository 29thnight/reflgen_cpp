// MSBuild 연동 시험 — reflgen.targets 가 생성한 서술로 직렬화와 등록이 되는가. game_types.h 도 이 파일도 생성
// 파일을 include 하지 않는다 — 주입 header 가 강제 include 로 들어온다(등록 함수 선언도 거기 있다).
// lib_types.h 는 참조하는 라이브러리의 header 다 — 그 서술은 라이브러리의 주입이 전파되어 온다.
#include "pch.h"
#include "derived_types.h"
#include "game_types.h"
#include "lib_types.h"
#include "reflgen/json.h"

int main()
{
    static_assert(reflgen::reflectable<generated_tests::stats>);
    static_assert(reflgen::reflectable<generated_tests::limits::slot>);
    static_assert(reflgen::reflectable<msbuild_lib::point>);

    reflgen::generated::register_msbuild_test();
    const bool registered = reflgen::default_registry().find("tests.hero") != nullptr;

    generated_tests::stats stats;
    stats.level = 3;
    const std::string text = reflgen::json::to_string(stats);
    const bool serialized = text == R"({"level":3,"health":100.0,"secret":7})";

    // 다른 프로젝트의 타입과, 그것을 부모로 둔 이 프로젝트의 타입(부모 필드가 먼저).
    const msbuild_lib::point point;
    const std::string point_text = reflgen::json::to_string(point);
    const std::string tagged_text = reflgen::json::to_string(msbuild_app::tagged_point{});
    const bool propagated = point_text == R"({"x":1,"y":2})" && tagged_text == R"({"x":1,"y":2,"tag":7})" &&
                            msbuild_lib::length_squared(point) == 5;

    const bool ok = registered && serialized && propagated;
    std::printf("%s\n%s\n%s\nmsbuild integration: %s\n", text.c_str(), point_text.c_str(), tagged_text.c_str(),
                ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}

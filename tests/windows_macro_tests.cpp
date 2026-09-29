// windows.h 를 NOMINMAX 없이 include 한 번역 단위 — 함수형 매크로 min·max 가 먼저 정의돼 있어도 reflgen header 가
// 컴파일되고 동작하는가. Windows 코드베이스는 흔히 이 상태다(엔진 header 가 windows.h 를 끌고 다닌다).
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

#include "harness.h"
#include "reflgen/json.h"
#include "reflgen/reflgen.h"
#include "reflgen/runtime/registry.h"
#include <string>

namespace windows_macros
{
    struct gauge
    {
        float level = 0.25f;
        short raw = 7;

        static consteval auto reflect()
        {
            return reflgen::schema<gauge>(reflgen::field<&gauge::level>.with(reflgen::range<float>(0.0f, 1.0f)),
                                          reflgen::field<&gauge::raw>);
        }
    };
} // namespace windows_macros

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::test;

    const test survives_min_max_macros("windows macros: headers compile under function-like min and max", [] {
        const auto& level = std::get<0>(reflgen::schema_of<windows_macros::gauge>.fields);
        check_equal(level.attribute<reflgen::range<float>>().max, 1.0f);
        check_equal(reflgen::json::to_string(windows_macros::gauge{}), std::string(R"({"level":0.25,"raw":7})"));
        check(reflgen::type_descriptor_of<windows_macros::gauge>().find_field("raw") != nullptr);
    });
} // namespace

#undef min
#undef max

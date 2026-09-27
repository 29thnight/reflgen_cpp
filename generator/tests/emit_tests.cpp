// 생성 코드의 모양 가운데 빌드가 기대는 것 — 주입 header 의 형식 검사.
#include "emit.h"
#include "harness.h"
#include "reflgen/core/version.h"
#include <string>

namespace
{
    using reflgen::generator::emit_module_header;
    using reflgen_test::check;
    using reflgen_test::test;

    // 주입 header 는 모든 번역 단위에 들어가므로 여기서 한 번 검사하면 모듈 전체가 검사된다. 생성기와 header 의
    // 판이 어긋난 설치(옛 생성기 + 새 header 등)를 컴파일 오류로 멈춘다.
    const test module_header_checks_format("emit: the injection header checks the generated code format", [] {
        const std::string text = emit_module_header("game", {});
        check(text.find("#include \"reflgen/core/version.h\"") != std::string::npos);
        const std::string check_line =
            "static_assert(::reflgen::generated_code_format == " + std::to_string(reflgen::generated_code_format) + ',';
        check(text.find(check_line) != std::string::npos);
    });

} // namespace

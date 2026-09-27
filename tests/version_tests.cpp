// 판 — CMake project(VERSION)·생성기 --version·VSIX 매니페스트가 따르는 단일 원천(reflgen/core/version.h).
#include "harness.h"
#include "reflgen/core/version.h"
#include <string>

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::test;

    const test version_string_spells_numbers("version: version_string spells major.minor.patch", [] {
        const std::string expected = std::to_string(reflgen::version_major) + '.' +
                                     std::to_string(reflgen::version_minor) + '.' +
                                     std::to_string(reflgen::version_patch);
        check_equal(std::string(reflgen::version_string), expected);
    });

    const test generated_code_format_is_positive("version: the generated code format starts at 1",
                                                 [] { check(reflgen::generated_code_format >= 1); });
} // namespace

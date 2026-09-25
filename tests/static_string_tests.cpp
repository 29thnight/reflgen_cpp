// static_string — 속성이 담는 컴파일 타임 문자열. std::string_view 처럼 읽히고, 구조적 타입이라 C++26 주석
// 값(과 C++20 클래스 NTTP)이 될 수 있다.
#include "harness.h"
#include "reflgen/core/static_string.h"
#include <cstddef>
#include <string>
#include <string_view>

namespace
{
    using reflgen::static_string;
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::test;

    template<auto Value>
    struct probe
    {
        static constexpr auto value = Value;
    };

    // 템플릿 인자의 포인터는 문자열 리터럴을 가리킬 수 없다 — 정적 배열을 가리킨다.
    inline constexpr char health_text[] = "Health";

    const test reads_like_a_view("static_string: reads like std::string_view", [] {
        constexpr static_string text{std::string_view("Health")};
        constexpr std::string_view view = text;
        static_assert(view == "Health");
        check_equal(text.view(), std::string_view("Health"));
        check_equal(text.size(), std::size_t{6});
        check(!text.empty());
        check_equal(std::string(text.begin(), text.end()), std::string("Health"));
        check_equal(std::string_view(text.data(), text.size()), std::string_view("Health"));
    });

    const test compares("static_string: compares with itself and with std::string_view", [] {
        constexpr static_string health{std::string_view("Health")};
        constexpr static_string mana{std::string_view("Mana")};
        static_assert(health == std::string_view("Health"));
        static_assert(std::string_view("Health") == health);
        static_assert(health != mana);
        static_assert(health < mana);
        check(health == static_string{std::string_view("Health")});
    });

    const test empty_by_default("static_string: is empty by default", [] {
        constexpr static_string none{};
        static_assert(none.empty());
        check_equal(std::string_view(none), std::string_view());
    });

    const test structural("static_string: is a structural type", [] {
        constexpr static_string from_array{std::string_view(health_text)};
        check_equal(probe<from_array>::value.view(), std::string_view("Health"));
    });
} // namespace

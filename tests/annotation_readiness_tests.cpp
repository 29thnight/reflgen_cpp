// C++26 이행 준비 — reflgen 의 속성·지시어 타입은 모두 구조적 타입이어야 [[=reflgen::…]] 주석 값이 된다. 그 조건이
// C++20 클래스 NTTP 와 같으므로 지금 템플릿 인자로 써 보아 확인한다(구조적이 아니면 이 파일이 컴파일되지 않는다).
#include "harness.h"
#include "reflgen/reflgen.h"
#include <string_view>

namespace annotation_readiness
{
    // 이름 있는 이름공간에 둔다 — clang 은 익명 이름공간 타입의 멤버 이름을 자동으로 뽑지 못한다(README 알려진 제약).
    struct rigidbody
    {
        float mass = 1.0f;
        bool use_gravity = true;

        static consteval auto reflect()
        {
            // clang-format off
            return reflgen::schema<rigidbody>(
                reflgen::field<&rigidbody::mass>.with(reflgen::category("Body"), reflgen::range(0.0f, 10000.0f)),
                reflgen::field<&rigidbody::use_gravity>.with(reflgen::category("Body")));
            // clang-format on
        }
    };
} // namespace annotation_readiness

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::require;
    using reflgen_test::test;

    template<auto... Values>
    inline constexpr bool annotation_values = true;

    // 주석 값의 포인터는 문자열 리터럴을 가리킬 수 없다(C++26 에서는 define_static_string 이 배열을 만든다) —
    // 정적 배열로 대신한다.
    inline constexpr char body_text[] = "Body";
    constexpr std::string_view body{body_text};

    static_assert(annotation_values<reflgen::display_name(body), reflgen::description(body),
                                    reflgen::serialized_name(body), reflgen::category(body)>,
                  "string attributes must be structural");
    static_assert(annotation_values<reflgen::range(0, 1), reflgen::range(0.0f, 1.0f)>, "range must be structural");
    static_assert(annotation_values<reflgen::transient{}, reflgen::required{}, reflgen::hidden{}, reflgen::readonly{}>,
                  "marker attributes must be structural");
    static_assert(
        annotation_values<reflgen::reflect{}, reflgen::reflect(body), reflgen::ignore{}, reflgen::attribute{}>,
        "directives must be structural");

    const test category("attributes: category is read at compile time and at run time", [] {
        static_assert(std::get<0>(reflgen::schema_of<annotation_readiness::rigidbody>.fields)
                          .attribute<reflgen::category>()
                          .value == std::string_view("Body"));
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<annotation_readiness::rigidbody>();
        const reflgen::category* found = type.fields()[1].attributes().find<reflgen::category>();
        require(found != nullptr);
        check_equal(found->value, std::string_view("Body"));
    });

    const test directives("directives: are types but not attributes", [] {
        static_assert(reflgen::is_directive_v<reflgen::reflect>);
        static_assert(reflgen::is_directive_v<reflgen::ignore>);
        static_assert(reflgen::is_directive_v<reflgen::attribute>);
        static_assert(!reflgen::is_directive_v<reflgen::transient>);
        static_assert(!reflgen::is_directive_v<reflgen::category>);
        check(reflgen::reflect{}.name.empty());
        check_equal(reflgen::reflect(body).name, std::string_view("Body"));
    });
} // namespace

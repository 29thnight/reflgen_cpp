#include "support.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>

namespace scalars
{
    enum class mode
    {
        idle,
        running
    };

    enum class flags : unsigned
    {
        a = 1,
        b = 2
    };
} // namespace scalars

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::check_round_trip;
    using reflgen_test::check_throws;
    using reflgen_test::test;

    const test booleans("scalar: bool", [] {
        check_round_trip(true);
        check_round_trip(false);
        check_equal(reflgen::json::to_string(true), std::string("true"));
    });

    const test integer_limits("scalar: integer limits of every width", [] {
        check_round_trip((std::numeric_limits<std::int8_t>::min)());
        check_round_trip((std::numeric_limits<std::int8_t>::max)());
        check_round_trip((std::numeric_limits<std::uint8_t>::max)());
        check_round_trip((std::numeric_limits<std::int16_t>::min)());
        check_round_trip((std::numeric_limits<std::uint16_t>::max)());
        check_round_trip((std::numeric_limits<std::int32_t>::min)());
        check_round_trip((std::numeric_limits<std::uint32_t>::max)());
        check_round_trip((std::numeric_limits<std::int64_t>::min)());
        check_round_trip((std::numeric_limits<std::int64_t>::max)());
        check_round_trip((std::numeric_limits<std::uint64_t>::max)());
        check_round_trip(static_cast<signed char>(-5));
        check_round_trip(static_cast<unsigned char>(250));
        check_equal(reflgen::json::to_string(std::uint64_t{18446744073709551615ull}),
                    std::string("18446744073709551615"));
    });

    const test integer_narrowing("scalar: narrowing reads are range checked", [] {
        check_throws([] { reflgen::json::from_string<std::uint8_t>("300"); }, "out of range");
        check_throws([] { reflgen::json::from_string<std::int8_t>("-129"); }, "out of range");
        check_throws([] { reflgen::json::from_string<unsigned>("-1"); }, "non-negative");
        check_throws([] { reflgen::json::from_string<int>("1.5"); }, "expected an integer");
        check_throws([] { reflgen::json::from_string<std::int64_t>("9223372036854775808"); }, "out of range");
        check_throws([] { reflgen::binary::from_bytes<std::uint8_t>(reflgen::binary::to_bytes(1000)); },
                     "out of range");
    });

    const test floating_point("scalar: floating point values", [] {
        check_round_trip(0.1);
        check_round_trip(-2.5f);
        check_round_trip(1e300);
        check_round_trip(static_cast<long double>(0.25));
        check_round_trip((std::numeric_limits<double>::min)());
        check_equal(reflgen::json::to_string(1.0), std::string("1.0"));
        check_equal(reflgen::json::to_string(0.1), std::string("0.1"));
        check_equal(reflgen::json::from_string<double>("3"), 3.0);
        check_throws([] { reflgen::json::from_string<float>("1e39"); }, "out of range");
        check_throws([] { reflgen::json::to_string(std::numeric_limits<double>::quiet_NaN()); }, "NaN");
        // 바이너리 포맷은 무한대를 담는다.
        const double infinity = std::numeric_limits<double>::infinity();
        check_equal(reflgen_test::binary_round_trip(infinity), infinity);
    });

    const test characters("scalar: characters are one-character strings", [] {
        check_round_trip('A');
        check_round_trip(u8'z');
        check_round_trip(u'한');
        check_round_trip(U'\U0001F600');
        check_round_trip(L'W');
        check_equal(reflgen::json::to_string('A'), std::string("\"A\""));
        check_equal(reflgen::json::to_string(U'é'), std::string("\"\xC3\xA9\""));
        check_throws([] { reflgen::json::from_string<char>("\"ab\""); }, "single character");
        check_throws([] { reflgen::json::from_string<char16_t>("\"\\ud83d\\ude00\""); }, "single character");
        check_throws([] { reflgen::json::to_string(static_cast<char16_t>(0xD800)); }, "invalid Unicode");
    });

    const test bytes_and_null("scalar: std::byte, nullptr and monostate", [] {
        check_round_trip(std::byte{0xAB});
        check_equal(reflgen::json::to_string(std::byte{7}), std::string("7"));
        check_throws([] { reflgen::json::from_string<std::byte>("256"); }, "out of range");
        check_equal(reflgen::json::to_string(nullptr), std::string("null"));
        check_equal(reflgen::json::to_string(std::monostate{}), std::string("null"));
        check_round_trip(std::monostate{});
    });

    const test enums("scalar: enums use names and fall back to integers", [] {
        check_round_trip(scalars::mode::running);
        check_equal(reflgen::json::to_string(scalars::mode::running), std::string("\"running\""));
        // 이름 없는 값(플래그 조합)은 정수다.
        const auto combined = static_cast<scalars::flags>(3);
        check_equal(reflgen::json::to_string(combined), std::string("3"));
        check_round_trip(combined);
        check(reflgen::json::from_string<scalars::mode>("1") == scalars::mode::running);
        check_throws([] { reflgen::json::from_string<scalars::mode>("\"flying\""); }, "unknown enumerator 'flying'");
    });
} // namespace

#include "harness.h"
#include "reflgen/core/enum.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace enums
{
    enum class scoped
    {
        off,
        on,
        automatic
    };

    // 고정 기저 타입이 없는 비스코프드 열거형 — 스캔이 값 범위 밖을 두드리는 경로.
    enum unscoped
    {
        uc_zero,
        uc_one,
        uc_two
    };

    // 고정 기저 타입이 없고 음수를 가진 비스코프드 열거형 — bit_cast 우회로가 필요한 경우.
    enum signed_unscoped
    {
        su_minus = -3,
        su_plus = 3
    };

    enum class small : std::uint8_t
    {
        a,
        b
    };

    enum class gaps
    {
        negative = -2,
        zero = 0,
        four = 4
    };

    enum class wide_flags : unsigned
    {
        low = 1,
        high = 1u << 10
    };

    enum class described
    {
        alpha = 1000,
        beta = 2000
    };
} // namespace enums

template<>
struct reflgen::enum_range<enums::wide_flags>
{
    static constexpr long long min = 0;
    static constexpr long long max = 1 << 10;
};

template<>
struct reflgen::reflection<enums::described>
{
    static constexpr std::array value = {reflgen::enum_entry<enums::described>{"alpha", enums::described::alpha},
                                         reflgen::enum_entry<enums::described>{"beta", enums::described::beta}};
};

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::test;

    static_assert(reflgen::enum_entries<enums::scoped>.size() == 3);
    static_assert(reflgen::enum_entries<enums::scoped>[2].name == "automatic");
    static_assert(reflgen::enum_name(enums::unscoped::uc_one) == "uc_one");

    const test scoped_entries("enum: scoped entries in value order", [] {
        const auto& entries = reflgen::enum_entries<enums::scoped>;
        reflgen_test::require(entries.size() == 3, "expected 3 entries");
        check_equal(entries[0].name, std::string_view("off"));
        check_equal(entries[1].name, std::string_view("on"));
        check(entries[2].value == enums::scoped::automatic);
    });

    const test unscoped_entries("enum: unscoped enum without fixed type", [] {
        check_equal(reflgen::enum_entries<enums::unscoped>.size(), std::size_t{3});
        check_equal(reflgen::enum_name(enums::uc_two), std::string_view("uc_two"));
    });

    const test signed_unscoped_entries("enum: unscoped enum with negative values", [] {
        const auto& entries = reflgen::enum_entries<enums::signed_unscoped>;
        reflgen_test::require(entries.size() == 2, "expected 2 entries");
        check_equal(entries[0].name, std::string_view("su_minus"));
        check_equal(reflgen::enum_name(enums::su_plus), std::string_view("su_plus"));
    });

    const test small_entries("enum: uint8_t based enum", [] {
        check_equal(reflgen::enum_entries<enums::small>.size(), std::size_t{2});
        check_equal(reflgen::enum_name(enums::small::b), std::string_view("b"));
    });

    const test gap_entries("enum: gaps and negatives", [] {
        const auto& entries = reflgen::enum_entries<enums::gaps>;
        reflgen_test::require(entries.size() == 3, "expected 3 entries");
        check_equal(entries[0].name, std::string_view("negative"));
        check_equal(entries[2].name, std::string_view("four"));
    });

    const test unnamed_values("enum: unnamed values have empty names", [] {
        check(reflgen::enum_name(static_cast<enums::scoped>(99)).empty());
        check(!reflgen::enum_cast<enums::scoped>("missing").has_value());
    });

    const test enum_cast("enum: enum_cast finds values by name", [] {
        const auto value = reflgen::enum_cast<enums::scoped>("on");
        reflgen_test::require(value.has_value());
        check(*value == enums::scoped::on);
    });

    const test custom_range("enum: enum_range widens the scan", [] {
        check_equal(reflgen::enum_entries<enums::wide_flags>.size(), std::size_t{2});
        check_equal(reflgen::enum_name(enums::wide_flags::high), std::string_view("high"));
    });

    const test described_enum("enum: reflection<E> supplies an exact table", [] {
        const auto& entries = reflgen::enum_entries<enums::described>;
        reflgen_test::require(entries.size() == 2, "expected 2 entries");
        check_equal(reflgen::enum_name(enums::described::beta), std::string_view("beta"));
        check(reflgen::enum_cast<enums::described>("alpha") == enums::described::alpha);
    });

    const test names_are_c_strings("enum: names are NUL terminated", [] {
        const std::string_view name = reflgen::enum_name(enums::scoped::on);
        check_equal(name.data()[name.size()], '\0');
    });
} // namespace

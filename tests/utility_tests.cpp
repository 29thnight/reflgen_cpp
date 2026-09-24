#include "support.h"
#include <atomic>
#include <bitset>
#include <chrono>
#include <complex>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace utility
{
struct settings
{
    int volume = 5;
    bool muted = false;

    bool operator==(const settings&) const = default;

    static consteval auto reflect()
    {
        return reflgen::schema<settings>(reflgen::field<&settings::volume>, reflgen::field<&settings::muted>);
    }
};

// 튜플 모양 사용자 타입 — tuple_size/tuple_element/get 을 갖추면 배열로 적힌다.
struct vec2
{
    float x = 0;
    float y = 0;

    bool operator==(const vec2&) const = default;
};

template<std::size_t I>
float& get(vec2& value)
{
    return I == 0 ? value.x : value.y;
}

template<std::size_t I>
const float& get(const vec2& value)
{
    return I == 0 ? value.x : value.y;
}
} // namespace utility

template<>
struct std::tuple_size<utility::vec2> : std::integral_constant<std::size_t, 2>
{
};

template<std::size_t I>
struct std::tuple_element<I, utility::vec2>
{
    using type = float;
};

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::check_round_trip;
using reflgen_test::check_throws;
using reflgen_test::test;

const test optionals("utility: optional is null or the value", [] {
    check_round_trip(std::optional<int>{});
    check_round_trip(std::optional<std::string>{"x"});
    check_equal(reflgen::json::to_string(std::optional<int>{}), std::string("null"));
    check_equal(reflgen::json::to_string(std::optional<int>{3}), std::string("3"));

    // 값이 이미 있으면 제자리에 읽는다 — 입력에 없는 필드는 유지된다.
    std::optional<utility::settings> existing = utility::settings{9, true};
    reflgen::json::from_string(R"({"volume":1})", existing);
    check_equal(existing->volume, 1);
    check_equal(existing->muted, true);
    reflgen::json::from_string("null", existing);
    check(!existing.has_value());
});

const test variants("utility: variant is [index, value]", [] {
    using value = std::variant<int, std::string, utility::settings>;
    check_round_trip(value{5});
    check_round_trip(value{std::string("text")});
    check_round_trip(value{utility::settings{}});
    check_equal(reflgen::json::to_string(value{std::string("s")}), std::string(R"([1,"s"])"));
    check_throws([] { reflgen::json::from_string<value>("[3,1]"); }, "out of range");
    check_throws([] { reflgen::json::from_string<value>("[0]"); }, "[index, value]");
    check_throws([] { reflgen::json::from_string<value>("[]"); }, "[index, value]");
    check_throws([] { reflgen::json::from_string<value>("[0,1,2]"); }, "[index, value]");
    check_throws([] { reflgen::json::from_string<value>("[0,\"x\"]"); }, "(at /1)");
});

const test tuples("utility: pair, tuple and tuple-like types are arrays", [] {
    check_round_trip(std::pair<int, std::string>{1, "one"});
    check_round_trip(std::tuple<int, double, bool>{1, 2.5, true});
    check_round_trip(std::tuple<>{});
    check_round_trip(utility::vec2{1.5f, -2.0f});
    check_equal(reflgen::json::to_string(std::pair<int, bool>{1, false}), std::string("[1,false]"));
    check_equal(reflgen::json::to_string(utility::vec2{1.0f, 2.0f}), std::string("[1.0,2.0]"));
    check_throws([] { reflgen::json::from_string<std::pair<int, int>>("[1]"); }, "expected 2 elements, got 1");
    check_throws([] { reflgen::json::from_string<std::pair<int, int>>("[1,2,3]"); }, "got more");
});

const test numeric_wrappers("utility: complex and bitset", [] {
    check_round_trip(std::complex<double>{1.5, -2.0});
    check_round_trip(std::bitset<10>{0b1011001110});
    check_equal(reflgen::json::to_string(std::bitset<4>{0b0101}), std::string("\"0101\""));
    check_throws([] { reflgen::json::from_string<std::bitset<4>>("\"012\""); }, "binary digits");
    check_throws([] { reflgen::json::from_string<std::bitset<4>>("\"01x0\""); }, "binary digits");
});

const test chrono_values("utility: chrono durations and time points are tick counts", [] {
    check_round_trip(std::chrono::milliseconds{1500});
    check_round_trip(std::chrono::duration<double>{0.25});
    check_equal(reflgen::json::to_string(std::chrono::seconds{3}), std::string("3"));
    const auto now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
    check_round_trip(now);
});

const test paths("utility: filesystem paths use generic UTF-8 form", [] {
    const std::filesystem::path path = std::filesystem::path(u8"assets/한글/file.png");
    check_round_trip(path);
    check_equal(reflgen::json::to_string(std::filesystem::path("a/b")), std::string("\"a/b\""));
});

const test atomics_and_references("utility: atomic and reference_wrapper pass through", [] {
    const std::atomic<int> counter{7};
    check_equal(reflgen::json::to_string(counter), std::string("7"));
    std::atomic<int> target{0};
    reflgen::json::from_string("11", target);
    check_equal(target.load(), 11);

    int value = 3;
    std::reference_wrapper<int> reference = value;
    check_equal(reflgen::json::to_string(reference), std::string("3"));
    reflgen::json::from_string("8", reference);
    check_equal(value, 8);
});
} // namespace

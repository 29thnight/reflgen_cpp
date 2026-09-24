#include "support.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::check_throws;
using reflgen_test::require;
using reflgen_test::test;

std::vector<std::byte> bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> result;
    for (const int value : values)
    {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

const test encodings("binary: exact encodings", [] {
    check_equal(reflgen::binary::to_bytes(nullptr), bytes({0x00}));
    check_equal(reflgen::binary::to_bytes(false), bytes({0x01}));
    check_equal(reflgen::binary::to_bytes(true), bytes({0x02}));
    check_equal(reflgen::binary::to_bytes(-1), bytes({0x03, 0x01}));
    check_equal(reflgen::binary::to_bytes(1), bytes({0x03, 0x02}));
    check_equal(reflgen::binary::to_bytes(300u), bytes({0x04, 0xAC, 0x02}));
    check_equal(reflgen::binary::to_bytes(std::string("hi")), bytes({0x06, 0x02, 'h', 'i'}));
    check_equal(reflgen::binary::to_bytes(std::vector<int>{1}), bytes({0x08, 0x01, 0x03, 0x02}));
    check_equal(reflgen::binary::to_bytes(1.0), bytes({0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
    check_equal(reflgen::binary::to_bytes(std::vector<std::byte>{std::byte{9}}), bytes({0x07, 0x01, 0x09}));
});

const test extremes("binary: 64-bit extremes", [] {
    check_equal(reflgen_test::binary_round_trip((std::numeric_limits<std::int64_t>::min)()),
                (std::numeric_limits<std::int64_t>::min)());
    check_equal(reflgen_test::binary_round_trip((std::numeric_limits<std::uint64_t>::max)()),
                (std::numeric_limits<std::uint64_t>::max)());
});

const test cross_reads("binary: signed and unsigned tags read into either", [] {
    check_equal(reflgen::binary::from_bytes<std::uint32_t>(reflgen::binary::to_bytes(5)), 5u);
    check_equal(reflgen::binary::from_bytes<int>(reflgen::binary::to_bytes(5u)), 5);
    check_equal(reflgen::binary::from_bytes<double>(reflgen::binary::to_bytes(-3)), -3.0);
    check_equal(reflgen::binary::from_bytes<double>(reflgen::binary::to_bytes(3u)), 3.0);
    check_throws([] { reflgen::binary::from_bytes<unsigned>(reflgen::binary::to_bytes(-1)); }, "non-negative");
    check_throws(
        [] {
            reflgen::binary::from_bytes<std::int64_t>(
                reflgen::binary::to_bytes((std::numeric_limits<std::uint64_t>::max)()));
        },
        "out of range for int64");
    check_throws([] { reflgen::binary::from_bytes<int>(reflgen::binary::to_bytes(std::string("x"))); },
                 "expected an integer");
    check_throws([] { reflgen::binary::from_bytes<unsigned>(reflgen::binary::to_bytes(std::string("x"))); },
                 "non-negative");
    check_throws([] { reflgen::binary::from_bytes<double>(reflgen::binary::to_bytes(true)); }, "expected a number");
    check_throws([] { reflgen::binary::from_bytes<bool>(reflgen::binary::to_bytes(1)); }, "expected a boolean");
});

const test malformed("binary: malformed input is rejected with an offset", [] {
    check_throws([] { reflgen::binary::from_bytes<int>(bytes({})); }, "unexpected end of input at offset 0");
    check_throws([] { reflgen::binary::from_bytes<int>(bytes({0x7F})); }, "unknown tag");
    check_throws([] { reflgen::binary::from_bytes<std::string>(bytes({0x06, 0x05, 'a'})); },
                 "length exceeds the remaining input");
    check_throws([] { reflgen::binary::from_bytes<std::vector<int>>(bytes({0x08, 0xE8, 0x07, 0x03})); },
                 "length exceeds the remaining input");
    check_throws(
        [] {
            reflgen::binary::from_bytes<std::uint64_t>(
                bytes({0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}));
        },
        "varint overflows");
    check_throws([] { reflgen::binary::from_bytes<double>(bytes({0x05, 0x00})); }, "unexpected end of input");
    check_throws([] { reflgen::binary::from_bytes<int>(bytes({0x03, 0x02, 0x00})); }, "unexpected bytes");
    check_throws([] { reflgen::binary::from_bytes<std::nullptr_t>(bytes({0x01})); }, "expected null");
});

const test reader_protocol("binary: reader enforces container protocol", [] {
    std::string key;
    const auto array = reflgen::binary::to_bytes(std::vector<int>{1, 2});
    check_throws(
        [&] {
            reflgen::binary::reader in(array);
            in.begin_array();
            in.end_array();
        },
        "unread elements");
    check_throws([&] { reflgen::binary::reader(array).next_key(key); }, "outside of an object");
    check_throws([&] { reflgen::binary::reader(array).next_element(); }, "outside of an array");
    check_throws([&] { reflgen::binary::reader(array).end_object(); }, "outside of an object");
    check_throws([&] { reflgen::binary::reader(array).end_array(); }, "outside of an array");

    reflgen::binary::reader in(array);
    const auto hint = in.begin_array();
    require(hint.has_value());
    check_equal(*hint, std::size_t{2});
});

const test writer_protocol("binary: writer checks declared sizes", [] {
    std::vector<std::byte> output;
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.begin_array(2);
            out.write_int(1);
            out.end_array();
        },
        "fewer elements");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.begin_array(0);
            out.write_int(1);
        },
        "more elements");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.begin_object(0);
            out.write_key("k");
        },
        "more keys");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.begin_object(1);
            out.write_null();
        },
        "write_key must precede");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.write_null();
            out.write_null();
        },
        "exactly one root value");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.write_key("k");
        },
        "write_key");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.begin_array(0);
            out.end_object();
        },
        "end_object");
    check_throws(
        [&] {
            reflgen::binary::writer out(output);
            out.begin_object(0);
            out.end_array();
        },
        "end_array");
    reflgen::binary::writer complete(output);
    complete.begin_array(0);
    complete.end_array();
    check(complete.complete());
});

const test depth_and_skip("binary: depth limit and skipping", [] {
    std::vector<std::byte> deep;
    for (int i = 0; i < 600; ++i)
    {
        deep.push_back(std::byte{0x08});
        deep.push_back(std::byte{0x01});
    }
    deep.push_back(std::byte{0x00});
    check_throws(
        [&] {
            reflgen::binary::reader in(deep);
            in.skip_value();
        },
        "nesting is deeper than 512");

    const auto mixed = reflgen::binary::to_bytes(std::map<std::string, std::vector<std::byte>>{{"a", {std::byte{1}}}});
    reflgen::binary::reader in(mixed);
    in.skip_value();
    in.finish();

    const auto floats = reflgen::binary::to_bytes(std::vector<double>{1.5, -2.0});
    reflgen::binary::reader float_reader(floats);
    float_reader.skip_value();
    float_reader.finish();
});
} // namespace

#include "support.h"
#include <cstddef>
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

    const test compact_output("json: compact output", [] {
        std::string text;
        reflgen::json::writer out(text);
        out.begin_object(3);
        out.write_key("a");
        out.begin_array(0);
        out.end_array();
        out.write_key("b");
        out.begin_object(0);
        out.end_object();
        out.write_key("c");
        out.write_null();
        out.end_object();
        check(out.complete());
        check_equal(text, std::string(R"({"a":[],"b":{},"c":null})"));
    });

    const test indented_output("json: indented output", [] {
        const std::map<std::string, std::vector<int>> value{{"k", {1, 2}}, {"e", {}}};
        check_equal(reflgen::json::to_string(value, 2),
                    std::string("{\n  \"e\": [],\n  \"k\": [\n    1,\n    2\n  ]\n}"));
    });

    const test writer_protocol("json: writer rejects protocol misuse", [] {
        std::string text;
        check_throws(
            [&] {
                reflgen::json::writer out(text);
                out.write_key("x");
            },
            "write_key");
        check_throws(
            [&] {
                reflgen::json::writer out(text);
                out.begin_object(1);
                out.write_int(1);
            },
            "write_key must precede");
        check_throws(
            [&] {
                reflgen::json::writer out(text);
                out.write_int(1);
                out.write_int(2);
            },
            "exactly one root value");
        check_throws(
            [&] {
                reflgen::json::writer out(text);
                out.begin_array(0);
                out.end_object();
            },
            "end_object");
        check_throws(
            [&] {
                reflgen::json::writer out(text);
                out.begin_object(0);
                out.end_array();
            },
            "end_array");
        check_throws(
            [&] {
                reflgen::json::writer out(text);
                out.begin_object(1);
                out.write_key("k");
                out.end_object();
            },
            "end_object");
    });

    const test reader_basics("json: reader parses whitespace, literals and numbers", [] {
        reflgen::json::reader in(" \t\r\n{ \"n\" : null , \"t\":true,\"f\":false,\"i\":-12,\"u\":7,\"d\":2.5e1 } ");
        check(in.peek() == reflgen::value_kind::object);
        in.begin_object();
        std::string key;
        require(in.next_key(key));
        check(in.peek() == reflgen::value_kind::null);
        in.read_null();
        require(in.next_key(key));
        check(in.peek() == reflgen::value_kind::boolean);
        check(in.read_bool());
        require(in.next_key(key));
        check(!in.read_bool());
        require(in.next_key(key));
        check(in.peek() == reflgen::value_kind::integer);
        check_equal(in.read_int(), std::int64_t{-12});
        require(in.next_key(key));
        check_equal(in.read_uint(), std::uint64_t{7});
        require(in.next_key(key));
        check(in.peek() == reflgen::value_kind::floating);
        check_equal(in.read_float(), 25.0);
        check(!in.next_key(key));
        in.end_object();
        in.finish();
    });

    const test escapes("json: string escapes and unicode", [] {
        check_equal(reflgen::json::from_string<std::string>(R"("\"\\\/\b\f\n\r\tAé😀")"),
                    std::string("\"\\/\b\f\n\r\tA\xC3\xA9\xF0\x9F\x98\x80"));
        check_equal(reflgen::json::from_string<std::string>("\"\xED\x95\x9C\""), std::string("\xED\x95\x9C"));
        check_throws([] { reflgen::json::from_string<std::string>(R"("\x")"); }, "invalid escape");
        check_throws([] { reflgen::json::from_string<std::string>(R"("\uD83D")"); }, "unpaired");
        check_throws([] { reflgen::json::from_string<std::string>(R"("\uDE00")"); }, "unpaired");
        check_throws([] { reflgen::json::from_string<std::string>(R"("\uD83DA")"); }, "unpaired");
        check_throws([] { reflgen::json::from_string<std::string>(R"("\u00G1")"); }, "invalid \\u escape");
        check_throws([] { reflgen::json::from_string<std::string>("\"a\nb\""); }, "control character");
        check_throws([] { reflgen::json::from_string<std::string>("\"open"); }, "unterminated string");
        check_throws([] { reflgen::json::from_string<std::string>("\"\xC3\""); }, "UTF-8");
    });

    const test number_grammar("json: numbers follow RFC 8259", [] {
        for (const char* bad : {"01", "+1", "1.", ".5", "-", "1e", "1e+", "--1"})
        {
            check_throws([&] { reflgen::json::from_string<double>(bad); }, "json:");
        }
        check_equal(reflgen::json::from_string<double>("-0.5E-2"), -0.005);
        check_equal(reflgen::json::from_string<int>("0"), 0);
        check_throws([] { reflgen::json::from_string<double>("1e999"); }, "out of range");
        check_throws([] { reflgen::json::from_string<std::uint64_t>("18446744073709551616"); }, "out of range");
    });

    const test structure_errors("json: structural errors carry line and column", [] {
        check_throws([] { reflgen::json::from_string<std::vector<int>>("[1,2,]"); }, "trailing comma");
        check_throws([] { reflgen::json::from_string<std::vector<int>>("[1 2]"); }, "expected ',' or ']'");
        check_throws([] { reflgen::json::from_string<std::map<std::string, int>>(R"({"a" 1})"); }, "expected ':'");
        check_throws([] { reflgen::json::from_string<std::map<std::string, int>>("{1:1}"); }, "expected a string key");
        check_throws([] { reflgen::json::from_string<std::vector<int>>("[1]\n  x"); }, "line 2, column 3");
        check_throws([] { reflgen::json::from_string<std::vector<int>>(""); }, "unexpected end of input");
        check_throws([] { reflgen::json::from_string<std::vector<int>>("{}"); }, "expected an array");
        check_throws([] { reflgen::json::from_string<std::map<std::string, int>>("[]"); }, "expected an object");
        check_throws([] { reflgen::json::from_string<bool>("nul"); }, "expected a boolean");
        check_throws([] { reflgen::json::from_string<std::nullptr_t>("nil"); }, "expected 'null'");
        check_throws([] { reflgen::json::from_string<int>("@"); }, "json:");
        check_throws([] { reflgen::json::reader("@").peek(); }, "unexpected character");
    });

    const test depth_limit("json: nesting depth is limited", [] {
        const std::string deep = std::string(600, '[') + std::string(600, ']');
        check_throws(
            [&] {
                reflgen::json::reader in(deep);
                in.skip_value();
            },
            "nesting is deeper than 512");
        const std::string shallow = std::string(10, '[') + std::string(10, ']');
        check_throws(
            [&] {
                reflgen::json::reader in(shallow, 5);
                in.skip_value();
            },
            "deeper than 5");
        reflgen::json::reader ok(shallow, 10);
        ok.skip_value();
        ok.finish();
    });

    const test bytes("json: bytes are base64", [] {
        const std::vector<std::byte> empty;
        check_equal(reflgen::json::to_string(empty), std::string("\"\""));
        for (std::size_t size = 0; size < 7; ++size)
        {
            std::vector<std::byte> blob(size);
            for (std::size_t i = 0; i < size; ++i)
            {
                blob[i] = static_cast<std::byte>(i * 37 + 1);
            }
            check_equal(reflgen_test::json_round_trip(blob), blob);
        }
        check_throws([] { reflgen::json::from_string<std::vector<std::byte>>("\"abc\""); }, "invalid base64 length");
        check_throws([] { reflgen::json::from_string<std::vector<std::byte>>("\"ab!=\""); },
                     "invalid base64 character");
        check_throws([] { reflgen::json::from_string<std::vector<std::byte>>("\"a=bc\""); },
                     "invalid base64 character");
    });

    const test skipping("json: skip_value walks any value", [] {
        reflgen::json::reader in(R"([{"a":[1,2.5,"s",null,true,{"b":{}}]},"x",-3])");
        in.skip_value();
        in.finish();
    });

    const test reader_protocol("json: reader rejects protocol misuse", [] {
        std::string key;
        check_throws([&] { reflgen::json::reader("[]").next_element(); }, "outside of an array");
        check_throws([&] { reflgen::json::reader("{}").next_key(key); }, "outside of an object");
        check_throws([&] { reflgen::json::reader("[]").end_array(); }, "outside of an array");
        check_throws([&] { reflgen::json::reader("{}").end_object(); }, "outside of an object");
        check_throws(
            [&] {
                reflgen::json::reader in("[1]");
                in.begin_array();
                in.end_array();
            },
            "expected ']'");
        check_throws(
            [&] {
                reflgen::json::reader in("{\"a\":1}");
                in.begin_object();
                in.end_object();
            },
            "expected '}'");
    });
} // namespace

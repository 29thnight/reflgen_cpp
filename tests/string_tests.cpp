#include "support.h"
#include <string>
#include <string_view>

namespace
{
using reflgen_test::check_equal;
using reflgen_test::check_round_trip;
using reflgen_test::check_throws;
using reflgen_test::test;

const test narrow_strings("string: std::string with escapes", [] {
    const std::string text = "quote\" backslash\\ newline\n tab\t bell\x07 \xED\x95\x9C\xEA\xB8\x80";
    check_round_trip(text);
    check_round_trip(std::string());
    check_equal(reflgen::json::to_string(std::string("a\"b\n\x01")), std::string(R"("a\"b\n\u0001")"));
});

const test wide_strings("string: every character type converts through UTF-8", [] {
    check_round_trip(std::u8string(u8"한글 text"));
    check_round_trip(std::u16string(u"surrogate \U0001F600 pair"));
    check_round_trip(std::u32string(U"\U0001F600é"));
    check_round_trip(std::wstring(L"wide é"));
    check_equal(reflgen::json::to_string(std::u16string(u"é")), std::string("\"\xC3\xA9\""));
    check_throws([] { reflgen::json::to_string(std::u16string(1, static_cast<char16_t>(0xDC00))); }, "invalid Unicode");
    check_throws([] { reflgen::json::to_string(std::u16string(1, static_cast<char16_t>(0xD800))); },
                 "unpaired UTF-16 surrogate");
    check_throws([] { reflgen::binary::from_bytes<std::u8string>(reflgen::binary::to_bytes(std::string("\xFF"))); },
                 "invalid UTF-8");
});

const test views_and_pointers("string: string_view and C strings are written", [] {
    check_equal(reflgen::json::to_string(std::string_view("view")), std::string("\"view\""));
    const char* text = "pointer";
    check_equal(reflgen::json::to_string(text), std::string("\"pointer\""));
    const char* null_text = nullptr;
    check_equal(reflgen::json::to_string(null_text), std::string("null"));
    check_equal(reflgen::json::to_string(std::u16string_view(u"w")), std::string("\"w\""));
    check_equal(reflgen::json::to_string("literal"), std::string("\"literal\""));
});

const test char_arrays("string: character arrays stop at NUL", [] {
    char buffer[8] = "abc";
    check_equal(reflgen::json::to_string(buffer), std::string("\"abc\""));

    char target[8] = "zzzzzzz";
    reflgen::json::from_string("\"hi\"", target);
    check_equal(std::string(target), std::string("hi"));
    check_equal(target[7], '\0');

    char small[3] = {};
    check_throws([&] { reflgen::json::from_string("\"abc\"", small); }, "does not fit");
});

const test invalid_utf8("string: JSON output must be valid UTF-8", [] {
    check_throws([] { reflgen::json::to_string(std::string("\xC3\x28")); }, "UTF-8");
    check_throws([] { reflgen::json::to_string(std::string("\xE0\x80\x80")); }, "invalid UTF-8");
    check_throws([] { reflgen::json::to_string(std::string("\xF0\x9F")); }, "truncated");
    check_throws([] { reflgen::json::to_string(std::string("\xFF")); }, "lead byte");
    // 바이너리 포맷은 바이트를 그대로 담는다.
    const std::string raw("\xFF\x00\x01", 3);
    check_equal(reflgen_test::binary_round_trip(raw), raw);
});
} // namespace

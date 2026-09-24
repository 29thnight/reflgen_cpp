#pragma once
// JSON 입력 백엔드 (RFC 8259) — 입력 전체를 메모리에 둔 당겨 읽기 파서.
//
// 신뢰할 수 없는 입력을 전제로 한다:
//   - 중첩 깊이 상한(max_depth) — 깊은 입력으로 스택을 넘치게 하는 것을 막는다.
//   - 숫자 문법을 RFC 대로 검사한다(선행 0, '+', 끝 소수점 거부).
//   - 문자열의 UTF-8 과 \u 서로게이트 짝을 검사한다.
// 오류 메시지에 줄·열을 담는다(1부터 센다).
#include "reflgen/json/base64.h"
#include "reflgen/serial/detail/utf.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace reflgen::json
{
class reader final : public reflgen::reader
{
  public:
    static constexpr std::size_t default_max_depth = 512;

    explicit reader(std::string_view input, std::size_t max_depth = default_max_depth)
        : input_(input), max_depth_(max_depth)
    {
    }

    value_kind peek() override
    {
        skip_whitespace();
        switch (current())
        {
        case 'n':
            return value_kind::null;
        case 't':
        case 'f':
            return value_kind::boolean;
        case '"':
            return value_kind::string;
        case '[':
            return value_kind::array;
        case '{':
            return value_kind::object;
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            const std::size_t saved = position_;
            const std::string_view token = scan_number();
            position_ = saved;
            return is_integer_token(token) ? value_kind::integer : value_kind::floating;
        }
        default:
            fail(position_ >= input_.size() ? "expected a value" : "unexpected character");
        }
    }

    void read_null() override
    {
        skip_whitespace();
        expect_literal("null");
    }

    bool read_bool() override
    {
        skip_whitespace();
        if (current() == 't')
        {
            expect_literal("true");
            return true;
        }
        if (current() == 'f')
        {
            expect_literal("false");
            return false;
        }
        fail("expected a boolean");
    }

    std::int64_t read_int() override
    {
        skip_whitespace();
        const std::size_t start = position_;
        const std::string_view token = scan_number();
        if (!is_integer_token(token))
        {
            fail_at(start, "expected an integer");
        }
        std::int64_t value = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{})
        {
            fail_at(start, "integer out of range for int64");
        }
        return value;
    }

    std::uint64_t read_uint() override
    {
        skip_whitespace();
        const std::size_t start = position_;
        const std::string_view token = scan_number();
        if (!is_integer_token(token) || token.front() == '-')
        {
            fail_at(start, "expected a non-negative integer");
        }
        std::uint64_t value = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{})
        {
            fail_at(start, "integer out of range for uint64");
        }
        return value;
    }

    double read_float() override
    {
        skip_whitespace();
        const std::size_t start = position_;
        const std::string_view token = scan_number();
        double value = 0.0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
        {
            fail_at(start, "number out of range for double");
        }
        return value;
    }

    std::string read_string() override
    {
        skip_whitespace();
        if (current() != '"')
        {
            fail("expected a string");
        }
        return parse_string();
    }

    std::vector<std::byte> read_bytes() override
    {
        skip_whitespace();
        const std::size_t start = position_;
        const std::string text = read_string();
        try
        {
            return detail::base64_decode(text);
        }
        catch (const serialization_error& error)
        {
            fail_at(start, error.message());
        }
    }

    std::optional<std::size_t> begin_array() override
    {
        skip_whitespace();
        if (current() != '[')
        {
            fail("expected an array");
        }
        enter(false);
        ++position_;
        return std::nullopt;
    }

    bool next_element() override
    {
        if (stack_.empty() || stack_.back().is_object)
        {
            fail("next_element outside of an array");
        }
        return advance(']');
    }

    void end_array() override
    {
        if (stack_.empty() || stack_.back().is_object)
        {
            fail("end_array outside of an array");
        }
        skip_whitespace();
        if (current() != ']')
        {
            fail("expected ']'");
        }
        ++position_;
        stack_.pop_back();
    }

    std::optional<std::size_t> begin_object() override
    {
        skip_whitespace();
        if (current() != '{')
        {
            fail("expected an object");
        }
        enter(true);
        ++position_;
        return std::nullopt;
    }

    bool next_key(std::string& key) override
    {
        if (stack_.empty() || !stack_.back().is_object)
        {
            fail("next_key outside of an object");
        }
        if (!advance('}'))
        {
            return false;
        }
        if (current() != '"')
        {
            fail("expected a string key");
        }
        key = parse_string();
        skip_whitespace();
        if (current() != ':')
        {
            fail("expected ':'");
        }
        ++position_;
        return true;
    }

    void end_object() override
    {
        if (stack_.empty() || !stack_.back().is_object)
        {
            fail("end_object outside of an object");
        }
        skip_whitespace();
        if (current() != '}')
        {
            fail("expected '}'");
        }
        ++position_;
        stack_.pop_back();
    }

    void skip_value() override
    {
        switch (peek())
        {
        case value_kind::null:
            read_null();
            break;
        case value_kind::boolean:
            read_bool();
            break;
        case value_kind::integer:
        case value_kind::floating:
            scan_number();
            break;
        case value_kind::string:
        case value_kind::bytes:
            parse_string();
            break;
        case value_kind::array: {
            begin_array();
            while (next_element())
            {
                skip_value();
            }
            end_array();
            break;
        }
        case value_kind::object: {
            begin_object();
            std::string key;
            while (next_key(key))
            {
                skip_value();
            }
            end_object();
            break;
        }
        }
    }

    // 루트 값 뒤에 공백만 남았는지 확인한다.
    void finish()
    {
        skip_whitespace();
        if (position_ != input_.size())
        {
            fail("unexpected characters after the root value");
        }
    }

  private:
    struct frame
    {
        bool is_object;
        bool first;
    };

    char current() const noexcept { return position_ < input_.size() ? input_[position_] : '\0'; }

    void skip_whitespace() noexcept
    {
        while (position_ < input_.size())
        {
            const char c = input_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            {
                break;
            }
            ++position_;
        }
    }

    void enter(bool is_object)
    {
        if (stack_.size() >= max_depth_)
        {
            fail("nesting is deeper than " + std::to_string(max_depth_));
        }
        stack_.push_back({is_object, true});
    }

    // 배열·객체의 다음 항목으로 간다. 닫는 괄호를 만나면 false — 괄호는 end_* 가 먹는다.
    bool advance(char closing)
    {
        skip_whitespace();
        frame& top = stack_.back();
        if (top.first)
        {
            top.first = false;
            return current() != closing;
        }
        if (current() == closing)
        {
            return false;
        }
        if (current() != ',')
        {
            fail(std::string("expected ',' or '") + closing + "'");
        }
        ++position_;
        skip_whitespace();
        if (current() == closing)
        {
            fail("trailing comma");
        }
        return true;
    }

    void expect_literal(std::string_view literal)
    {
        if (input_.substr(position_, literal.size()) != literal)
        {
            fail("expected '" + std::string(literal) + "'");
        }
        position_ += literal.size();
    }

    static bool is_integer_token(std::string_view token) noexcept
    {
        return token.find_first_of(".eE") == std::string_view::npos;
    }

    static bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    // -? (0 | [1-9][0-9]*) (\.[0-9]+)? ([eE][+-]?[0-9]+)?
    std::string_view scan_number()
    {
        const std::size_t start = position_;
        if (current() == '-')
        {
            ++position_;
        }
        if (current() == '0')
        {
            ++position_;
        }
        else if (is_digit(current()))
        {
            while (is_digit(current()))
            {
                ++position_;
            }
        }
        else
        {
            fail_at(start, "invalid number");
        }
        if (current() == '.')
        {
            ++position_;
            if (!is_digit(current()))
            {
                fail_at(start, "invalid number");
            }
            while (is_digit(current()))
            {
                ++position_;
            }
        }
        if (current() == 'e' || current() == 'E')
        {
            ++position_;
            if (current() == '+' || current() == '-')
            {
                ++position_;
            }
            if (!is_digit(current()))
            {
                fail_at(start, "invalid number");
            }
            while (is_digit(current()))
            {
                ++position_;
            }
        }
        return input_.substr(start, position_ - start);
    }

    unsigned read_hex4()
    {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = current();
            value <<= 4;
            if (c >= '0' && c <= '9')
            {
                value |= static_cast<unsigned>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                value |= static_cast<unsigned>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                value |= static_cast<unsigned>(c - 'A' + 10);
            }
            else
            {
                fail("invalid \\u escape");
            }
            ++position_;
        }
        return value;
    }

    // position_ 은 여는 따옴표에 있다.
    std::string parse_string()
    {
        const std::size_t start = position_;
        ++position_;
        std::string result;
        while (true)
        {
            if (position_ >= input_.size())
            {
                fail_at(start, "unterminated string");
            }
            const char c = input_[position_];
            if (c == '"')
            {
                ++position_;
                return result;
            }
            if (static_cast<unsigned char>(c) < 0x20)
            {
                fail("control character in string");
            }
            if (c == '\\')
            {
                ++position_;
                append_escape(result);
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x80)
            {
                result += c;
                ++position_;
                continue;
            }
            // 비 ASCII — 한 글자 단위로 검사하며 옮긴다.
            const std::size_t sequence_start = position_;
            try
            {
                reflgen::detail::decode_utf8(input_, position_);
            }
            catch (const serialization_error& error)
            {
                fail_at(sequence_start, error.message());
            }
            result.append(input_.substr(sequence_start, position_ - sequence_start));
        }
    }

    void append_escape(std::string& result)
    {
        const char c = current();
        ++position_;
        switch (c)
        {
        case '"':
            result += '"';
            return;
        case '\\':
            result += '\\';
            return;
        case '/':
            result += '/';
            return;
        case 'b':
            result += '\b';
            return;
        case 'f':
            result += '\f';
            return;
        case 'n':
            result += '\n';
            return;
        case 'r':
            result += '\r';
            return;
        case 't':
            result += '\t';
            return;
        case 'u': {
            char32_t code_point = read_hex4();
            if (code_point >= 0xD800 && code_point <= 0xDBFF)
            {
                if (current() != '\\' || position_ + 1 >= input_.size() || input_[position_ + 1] != 'u')
                {
                    fail("unpaired UTF-16 surrogate in \\u escape");
                }
                position_ += 2;
                const char32_t low = read_hex4();
                if (low < 0xDC00 || low > 0xDFFF)
                {
                    fail("unpaired UTF-16 surrogate in \\u escape");
                }
                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
            }
            else if (code_point >= 0xDC00 && code_point <= 0xDFFF)
            {
                fail("unpaired UTF-16 surrogate in \\u escape");
            }
            reflgen::detail::append_utf8(result, code_point);
            return;
        }
        default:
            fail("invalid escape sequence");
        }
    }

    // 입력이 끝나서 기대가 어긋난 것이면 그 사실을 앞세운다 — 잘린 파일이 가장 흔한 원인이다.
    [[noreturn]] void fail(const std::string& message) const
    {
        fail_at(position_, position_ >= input_.size() ? "unexpected end of input; " + message : message);
    }

    [[noreturn]] void fail_at(std::size_t offset, const std::string& message) const
    {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < offset && i < input_.size(); ++i)
        {
            if (input_[i] == '\n')
            {
                ++line;
                column = 1;
            }
            else
            {
                ++column;
            }
        }
        throw serialization_error("json: " + message + " at line " + std::to_string(line) + ", column " +
                                  std::to_string(column));
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::size_t max_depth_;
    std::vector<frame> stack_;
};
} // namespace reflgen::json

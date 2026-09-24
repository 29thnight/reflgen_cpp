#pragma once
// JSON 출력 백엔드 (RFC 8259).
//
// 들여쓰기 0 이면 한 줄, 그 밖이면 원소마다 줄을 바꾼다. 실수는 왕복이 보장되는 최단
// 표기(std::to_chars)로 적고, 정수와 구별되게 소수점을 붙인다("1.0") — 읽는 쪽이
// peek() 으로 실수임을 알 수 있어야 한다. NaN·무한대는 JSON 에 표기가 없어 실패한다.
#include "reflgen/json/base64.h"
#include "reflgen/serial/detail/utf.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/writer.h"
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reflgen::json
{
class writer final : public reflgen::writer
{
  public:
    // reader 와 같은 상한이다 — 그보다 깊게 쓰면 기본 설정의 reader 가 어차피 거부한다.
    static constexpr std::size_t default_max_depth = 512;

    explicit writer(std::string& output, std::size_t indent = 0, std::size_t max_depth = default_max_depth)
        : output_(output), indent_(indent), max_depth_(max_depth)
    {
    }

    void write_null() override
    {
        begin_value();
        output_ += "null";
    }

    void write_bool(bool value) override
    {
        begin_value();
        output_ += value ? "true" : "false";
    }

    void write_int(std::int64_t value) override
    {
        begin_value();
        append_number(value);
    }

    void write_uint(std::uint64_t value) override
    {
        begin_value();
        append_number(value);
    }

    void write_float(double value) override
    {
        if (!std::isfinite(value))
        {
            throw serialization_error("json: NaN and infinity have no JSON representation");
        }
        begin_value();
        const std::size_t start = output_.size();
        append_number(value);
        const std::string_view written = std::string_view(output_).substr(start);
        if (written.find_first_of(".eE") == std::string_view::npos)
        {
            output_ += ".0";
        }
    }

    void write_string(std::string_view value) override
    {
        begin_value();
        append_quoted(value);
    }

    void write_bytes(std::span<const std::byte> value) override
    {
        begin_value();
        output_ += '"';
        output_ += detail::base64_encode(value);
        output_ += '"';
    }

    void begin_array(std::size_t) override
    {
        check_depth();
        begin_value();
        output_ += '[';
        stack_.push_back({false, false, false});
    }

    void end_array() override
    {
        if (stack_.empty() || stack_.back().is_object)
        {
            throw serialization_error("json writer: end_array without a matching begin_array");
        }
        close(']');
    }

    void begin_object(std::size_t) override
    {
        check_depth();
        begin_value();
        output_ += '{';
        stack_.push_back({true, false, false});
    }

    void write_key(std::string_view key) override
    {
        if (stack_.empty() || !stack_.back().is_object || stack_.back().awaiting_value)
        {
            throw serialization_error("json writer: write_key is only valid inside an object, before a value");
        }
        frame& top = stack_.back();
        if (top.has_items)
        {
            output_ += ',';
        }
        newline(stack_.size());
        append_quoted(key);
        output_ += indent_ > 0 ? ": " : ":";
        top.has_items = true;
        top.awaiting_value = true;
    }

    void end_object() override
    {
        if (stack_.empty() || !stack_.back().is_object || stack_.back().awaiting_value)
        {
            throw serialization_error("json writer: end_object without a matching begin_object");
        }
        close('}');
    }

    // 루트 값 하나가 끝까지 쓰였는가.
    bool complete() const noexcept { return root_started_ && stack_.empty(); }

  private:
    struct frame
    {
        bool is_object;
        bool has_items;
        bool awaiting_value;
    };

    // 순환이 아니어도 아주 깊은 구조(수만 단계 연결 리스트)는 쓰는 재귀가 stack 을 넘친다.
    // 그 전에 오류로 끝낸다.
    void check_depth() const
    {
        if (stack_.size() >= max_depth_)
        {
            throw serialization_error("json writer: nesting is deeper than " + std::to_string(max_depth_));
        }
    }

    void begin_value()
    {
        if (stack_.empty())
        {
            if (root_started_)
            {
                throw serialization_error("json writer: a document holds exactly one root value");
            }
            root_started_ = true;
            return;
        }
        frame& top = stack_.back();
        if (top.is_object)
        {
            if (!top.awaiting_value)
            {
                throw serialization_error("json writer: write_key must precede each value inside an object");
            }
            top.awaiting_value = false;
            return;
        }
        if (top.has_items)
        {
            output_ += ',';
        }
        newline(stack_.size());
        top.has_items = true;
    }

    void close(char bracket)
    {
        const bool had_items = stack_.back().has_items;
        stack_.pop_back();
        if (had_items)
        {
            newline(stack_.size());
        }
        output_ += bracket;
    }

    void newline(std::size_t depth)
    {
        if (indent_ == 0)
        {
            return;
        }
        output_ += '\n';
        output_.append(depth * indent_, ' ');
    }

    template<class Number>
    void append_number(Number value)
    {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        output_.append(buffer, result.ptr);
    }

    void append_quoted(std::string_view text)
    {
        // JSON 문자열은 UTF-8 이어야 한다. 잘못된 바이트를 흘려 보내면 읽는 쪽 파서가
        // 거부하거나 조용히 대체 문자로 바꾼다 — 쓰는 시점에 막는다.
        reflgen::detail::validate_utf8(text);

        constexpr char hex[] = "0123456789abcdef";
        output_ += '"';
        for (const char c : text)
        {
            switch (c)
            {
            case '"':
                output_ += "\\\"";
                break;
            case '\\':
                output_ += "\\\\";
                break;
            case '\b':
                output_ += "\\b";
                break;
            case '\f':
                output_ += "\\f";
                break;
            case '\n':
                output_ += "\\n";
                break;
            case '\r':
                output_ += "\\r";
                break;
            case '\t':
                output_ += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    output_ += "\\u00";
                    output_ += hex[(c >> 4) & 0x0F];
                    output_ += hex[c & 0x0F];
                }
                else
                {
                    output_ += c;
                }
            }
        }
        output_ += '"';
    }

    std::string& output_;
    std::size_t indent_;
    std::size_t max_depth_;
    std::vector<frame> stack_;
    bool root_started_ = false;
};
} // namespace reflgen::json

#pragma once
// 바이너리 출력 백엔드 — 형식은 format.h.
//
// 배열·객체의 원소 수를 머리에 적으므로 begin_* 에 넘어온 크기와 실제로 쓴 수가
// 다르면 결과가 깨진다. 그래서 닫을 때 센 수를 대조해 어긋나면 실패한다.
#include "reflgen/binary/format.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/writer.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reflgen::binary
{
    class writer final : public reflgen::writer
    {
      public:
        // reader 와 같은 상한이다 — 그보다 깊게 쓰면 기본 설정의 reader 가 어차피 거부한다.
        static constexpr std::size_t default_max_depth = 512;

        explicit writer(std::vector<std::byte>& output, std::size_t max_depth = default_max_depth)
            : output_(output), max_depth_(max_depth)
        {
        }

        void write_null() override
        {
            begin_value();
            put_tag(tag::null);
        }

        void write_bool(bool value) override
        {
            begin_value();
            put_tag(value ? tag::true_value : tag::false_value);
        }

        void write_int(std::int64_t value) override
        {
            begin_value();
            put_tag(tag::signed_integer);
            // zigzag: 작은 음수도 짧게 적힌다.
            put_varint((static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63));
        }

        void write_uint(std::uint64_t value) override
        {
            begin_value();
            put_tag(tag::unsigned_integer);
            put_varint(value);
        }

        void write_float(double value) override
        {
            begin_value();
            put_tag(tag::floating);
            const auto bits = std::bit_cast<std::uint64_t>(value);
            for (int shift = 0; shift < 64; shift += 8)
            {
                output_.push_back(static_cast<std::byte>((bits >> shift) & 0xFF));
            }
        }

        void write_string(std::string_view value) override
        {
            begin_value();
            put_tag(tag::string);
            put_text(value);
        }

        void write_bytes(std::span<const std::byte> value) override
        {
            begin_value();
            put_tag(tag::bytes);
            put_varint(value.size());
            output_.insert(output_.end(), value.begin(), value.end());
        }

        void begin_array(std::size_t size) override
        {
            check_depth();
            begin_value();
            put_tag(tag::array);
            put_varint(size);
            stack_.push_back({false, size, false});
        }

        void end_array() override
        {
            if (stack_.empty() || stack_.back().is_object)
            {
                throw serialization_error("binary writer: end_array without a matching begin_array");
            }
            close();
        }

        void begin_object(std::size_t size) override
        {
            check_depth();
            begin_value();
            put_tag(tag::object);
            put_varint(size);
            stack_.push_back({true, size, false});
        }

        void write_key(std::string_view key) override
        {
            if (stack_.empty() || !stack_.back().is_object || stack_.back().awaiting_value)
            {
                throw serialization_error("binary writer: write_key is only valid inside an object, before a value");
            }
            frame& top = stack_.back();
            if (top.remaining == 0)
            {
                throw serialization_error("binary writer: more keys than declared in begin_object");
            }
            --top.remaining;
            top.awaiting_value = true;
            put_text(key);
        }

        void end_object() override
        {
            if (stack_.empty() || !stack_.back().is_object || stack_.back().awaiting_value)
            {
                throw serialization_error("binary writer: end_object without a matching begin_object");
            }
            close();
        }

        bool complete() const noexcept { return root_started_ && stack_.empty(); }

      private:
        struct frame
        {
            bool is_object;
            std::size_t remaining;
            bool awaiting_value;
        };

        // 순환이 아니어도 아주 깊은 구조(수만 단계 연결 리스트)는 쓰는 재귀가 stack 을 넘친다.
        // 그 전에 오류로 끝낸다.
        void check_depth() const
        {
            if (stack_.size() >= max_depth_)
            {
                throw serialization_error("binary writer: nesting is deeper than " + std::to_string(max_depth_));
            }
        }

        void begin_value()
        {
            if (stack_.empty())
            {
                if (root_started_)
                {
                    throw serialization_error("binary writer: a document holds exactly one root value");
                }
                root_started_ = true;
                return;
            }
            frame& top = stack_.back();
            if (top.is_object)
            {
                if (!top.awaiting_value)
                {
                    throw serialization_error("binary writer: write_key must precede each value inside an object");
                }
                top.awaiting_value = false;
                return;
            }
            if (top.remaining == 0)
            {
                throw serialization_error("binary writer: more elements than declared in begin_array");
            }
            --top.remaining;
        }

        void close()
        {
            if (stack_.back().remaining != 0)
            {
                throw serialization_error("binary writer: fewer elements than declared");
            }
            stack_.pop_back();
        }

        void put_tag(tag value) { output_.push_back(static_cast<std::byte>(value)); }

        void put_varint(std::uint64_t value)
        {
            while (value >= 0x80)
            {
                output_.push_back(static_cast<std::byte>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            output_.push_back(static_cast<std::byte>(value));
        }

        void put_text(std::string_view text)
        {
            put_varint(text.size());
            for (const char c : text)
            {
                output_.push_back(static_cast<std::byte>(c));
            }
        }

        std::vector<std::byte>& output_;
        std::size_t max_depth_;
        std::vector<frame> stack_;
        bool root_started_ = false;
    };
} // namespace reflgen::binary

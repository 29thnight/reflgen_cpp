#pragma once
// 바이너리 입력 백엔드 — 형식은 format.h.
//
// 신뢰할 수 없는 입력을 전제로 한다. 길이·원소 수는 남은 바이트 수로 상한을 검사한다
// — 원소 하나는 적어도 1바이트, 객체 항목 하나는 적어도 2바이트(키 길이 + 태그)다.
// 이 검사가 없으면 원소 수를 2^60 으로 적은 몇 바이트짜리 입력이 reserve 로 메모리를
// 소진시킨다. 중첩 깊이도 상한을 둔다.
#include "reflgen/binary/format.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace reflgen::binary
{
    class reader final : public reflgen::reader
    {
      public:
        static constexpr std::size_t default_max_depth = 512;

        explicit reader(std::span<const std::byte> input, std::size_t max_depth = default_max_depth)
            : input_(input), max_depth_(max_depth)
        {
        }

        value_kind peek() override
        {
            switch (peek_tag())
            {
            case tag::null:
                return value_kind::null;
            case tag::false_value:
            case tag::true_value:
                return value_kind::boolean;
            case tag::signed_integer:
            case tag::unsigned_integer:
                return value_kind::integer;
            case tag::floating:
                return value_kind::floating;
            case tag::string:
                return value_kind::string;
            case tag::bytes:
                return value_kind::bytes;
            case tag::array:
                return value_kind::array;
            case tag::object:
                return value_kind::object;
            }
            fail("unknown tag");
        }

        void read_null() override { expect_tag(tag::null, "expected null"); }

        bool read_bool() override
        {
            const tag value = take_tag();
            if (value == tag::true_value)
            {
                return true;
            }
            if (value == tag::false_value)
            {
                return false;
            }
            fail_at(position_ - 1, "expected a boolean");
        }

        std::int64_t read_int() override
        {
            const std::size_t start = position_;
            const tag value = take_tag();
            if (value == tag::signed_integer)
            {
                const std::uint64_t encoded = take_varint();
                return static_cast<std::int64_t>(encoded >> 1) ^ -static_cast<std::int64_t>(encoded & 1);
            }
            if (value == tag::unsigned_integer)
            {
                const std::uint64_t encoded = take_varint();
                if (encoded > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                {
                    fail_at(start, "integer out of range for int64");
                }
                return static_cast<std::int64_t>(encoded);
            }
            fail_at(start, "expected an integer");
        }

        std::uint64_t read_uint() override
        {
            const std::size_t start = position_;
            const tag value = take_tag();
            if (value == tag::unsigned_integer)
            {
                return take_varint();
            }
            if (value == tag::signed_integer)
            {
                const std::uint64_t encoded = take_varint();
                const std::int64_t decoded =
                    static_cast<std::int64_t>(encoded >> 1) ^ -static_cast<std::int64_t>(encoded & 1);
                if (decoded < 0)
                {
                    fail_at(start, "expected a non-negative integer");
                }
                return static_cast<std::uint64_t>(decoded);
            }
            fail_at(start, "expected a non-negative integer");
        }

        double read_float() override
        {
            const std::size_t start = position_;
            const tag value = peek_tag();
            if (value == tag::signed_integer)
            {
                return static_cast<double>(read_int());
            }
            if (value == tag::unsigned_integer)
            {
                return static_cast<double>(read_uint());
            }
            if (value != tag::floating)
            {
                fail_at(start, "expected a number");
            }
            ++position_;
            require(8);
            std::uint64_t bits = 0;
            for (int shift = 0; shift < 64; shift += 8)
            {
                bits |= std::to_integer<std::uint64_t>(input_[position_]) << shift;
                ++position_;
            }
            return std::bit_cast<double>(bits);
        }

        std::string read_string() override
        {
            expect_tag(tag::string, "expected a string");
            return take_text();
        }

        std::vector<std::byte> read_bytes() override
        {
            expect_tag(tag::bytes, "expected bytes");
            const std::size_t size = take_length(1);
            std::vector<std::byte> result(input_.begin() + static_cast<std::ptrdiff_t>(position_),
                                          input_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
            position_ += size;
            return result;
        }

        std::optional<std::size_t> begin_array() override
        {
            expect_tag(tag::array, "expected an array");
            const std::size_t count = take_length(1);
            enter(false, count);
            return count;
        }

        bool next_element() override
        {
            if (stack_.empty() || stack_.back().is_object)
            {
                fail("next_element outside of an array");
            }
            return consume_slot();
        }

        void end_array() override
        {
            if (stack_.empty() || stack_.back().is_object)
            {
                fail("end_array outside of an array");
            }
            leave();
        }

        std::optional<std::size_t> begin_object() override
        {
            expect_tag(tag::object, "expected an object");
            const std::size_t count = take_length(2);
            enter(true, count);
            return count;
        }

        bool next_key(std::string& key) override
        {
            if (stack_.empty() || !stack_.back().is_object)
            {
                fail("next_key outside of an object");
            }
            if (!consume_slot())
            {
                return false;
            }
            key = take_text();
            return true;
        }

        void end_object() override
        {
            if (stack_.empty() || !stack_.back().is_object)
            {
                fail("end_object outside of an object");
            }
            leave();
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
                read_float();
                break;
            case value_kind::floating:
                read_float();
                break;
            case value_kind::string:
                read_string();
                break;
            case value_kind::bytes: {
                expect_tag(tag::bytes, "expected bytes");
                position_ += take_length(1);
                break;
            }
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

        // 루트 값 뒤에 남은 바이트가 없는지 확인한다.
        void finish()
        {
            if (position_ != input_.size())
            {
                fail("unexpected bytes after the root value");
            }
        }

      private:
        struct frame
        {
            bool is_object;
            std::size_t remaining;
        };

        void enter(bool is_object, std::size_t count)
        {
            if (stack_.size() >= max_depth_)
            {
                fail("nesting is deeper than " + std::to_string(max_depth_));
            }
            stack_.push_back({is_object, count});
        }

        bool consume_slot()
        {
            frame& top = stack_.back();
            if (top.remaining == 0)
            {
                return false;
            }
            --top.remaining;
            return true;
        }

        void leave()
        {
            if (stack_.back().remaining != 0)
            {
                fail("container closed with unread elements");
            }
            stack_.pop_back();
        }

        void require(std::size_t count) const
        {
            if (input_.size() - position_ < count)
            {
                fail("unexpected end of input");
            }
        }

        tag peek_tag() const
        {
            require(1);
            const auto value = std::to_integer<std::uint8_t>(input_[position_]);
            if (value > static_cast<std::uint8_t>(tag::object))
            {
                fail("unknown tag");
            }
            return static_cast<tag>(value);
        }

        tag take_tag()
        {
            const tag value = peek_tag();
            ++position_;
            return value;
        }

        void expect_tag(tag expected, const char* message)
        {
            if (peek_tag() != expected)
            {
                fail(message);
            }
            ++position_;
        }

        std::uint64_t take_varint()
        {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                require(1);
                const auto byte = std::to_integer<std::uint64_t>(input_[position_]);
                ++position_;
                if (shift == 63 && byte > 1)
                {
                    fail("varint overflows 64 bits");
                }
                value |= (byte & 0x7F) << shift;
                if ((byte & 0x80) == 0)
                {
                    return value;
                }
            }
            fail("varint overflows 64 bits");
        }

        // 길이·원소 수를 읽고, 원소 하나가 최소 minimum_item_size 바이트라는 전제로 남은
        // 입력에 들어갈 수 있는지 검사한다.
        std::size_t take_length(std::size_t minimum_item_size)
        {
            const std::size_t start = position_;
            const std::uint64_t length = take_varint();
            const std::size_t available = input_.size() - position_;
            if (length > available / minimum_item_size)
            {
                fail_at(start, "length exceeds the remaining input");
            }
            return static_cast<std::size_t>(length);
        }

        std::string take_text()
        {
            const std::size_t size = take_length(1);
            std::string result(reinterpret_cast<const char*>(input_.data() + position_), size);
            position_ += size;
            return result;
        }

        [[noreturn]] void fail(const std::string& message) const { fail_at(position_, message); }

        [[noreturn]] void fail_at(std::size_t offset, const std::string& message) const
        {
            throw serialization_error("binary: " + message + " at offset " + std::to_string(offset));
        }

        std::span<const std::byte> input_;
        std::size_t position_ = 0;
        std::size_t max_depth_;
        std::vector<frame> stack_;
    };
} // namespace reflgen::binary

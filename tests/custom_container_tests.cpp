#include "support.h"
#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace custom
{
    // STL 모양 — begin/end/clear/push_back. 특수화 없이 자동으로 잡혀야 한다.
    template<class T>
    class small_vector
    {
      public:
        using value_type = T;

        small_vector() = default;
        small_vector(std::initializer_list<T> items) : items_(items) {}

        auto begin() const { return items_.begin(); }
        auto end() const { return items_.end(); }
        auto begin() { return items_.begin(); }
        auto end() { return items_.end(); }
        std::size_t size() const { return items_.size(); }
        void clear() { items_.clear(); }
        void push_back(T item) { items_.push_back(std::move(item)); }

        bool operator==(const small_vector&) const = default;

      private:
        std::vector<T> items_;
    };

    // 인터페이스가 STL 과 다른 컨테이너 — container_traits 로 알려 준다.
    template<class T>
    class ring_buffer
    {
      public:
        std::size_t count() const { return items_.size(); }
        const T& at(std::size_t index) const { return items_[index]; }
        void put(T item) { items_.push_back(std::move(item)); }
        void reset() { items_.clear(); }
        std::size_t reserved() const { return reserved_; }
        void prepare(std::size_t capacity) { reserved_ = capacity; }

        bool operator==(const ring_buffer& other) const { return items_ == other.items_; }

      private:
        std::vector<T> items_;
        std::size_t reserved_ = 0;
    };

    // 원소를 한 번에만 받는 불변 컨테이너 — assign 경로.
    class frozen_list
    {
      public:
        frozen_list() = default;
        explicit frozen_list(std::vector<int> items) : items_(std::move(items)) {}

        const std::vector<int>& items() const { return items_; }

        bool operator==(const frozen_list&) const = default;

      private:
        std::vector<int> items_;
    };

    // 맵 모양이지만 인터페이스가 다른 컨테이너.
    class string_table
    {
      public:
        void set(std::string key, int value) { entries_[std::move(key)] = value; }
        const std::map<std::string, int>& entries() const { return entries_; }
        void wipe() { entries_.clear(); }

        bool operator==(const string_table&) const = default;

      private:
        std::map<std::string, int> entries_;
    };

    // 컨테이너가 아닌 값 타입을 전혀 다른 모양으로 적는다 — serializer 특수화.
    struct color
    {
        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;

        bool operator==(const color&) const = default;
    };
} // namespace custom

template<class T>
struct reflgen::container_traits<custom::ring_buffer<T>>
{
    using value_type = T;
    static constexpr reflgen::container_kind kind = reflgen::container_kind::sequence;

    static std::size_t size(const custom::ring_buffer<T>& buffer) { return buffer.count(); }

    template<class F>
    static void for_each(const custom::ring_buffer<T>& buffer, F&& function)
    {
        for (std::size_t i = 0; i < buffer.count(); ++i)
        {
            function(buffer.at(i));
        }
    }

    static void clear(custom::ring_buffer<T>& buffer) { buffer.reset(); }
    static void add(custom::ring_buffer<T>& buffer, T&& item) { buffer.put(std::move(item)); }
    static void reserve(custom::ring_buffer<T>& buffer, std::size_t count) { buffer.prepare(count); }
};

template<>
struct reflgen::container_traits<custom::frozen_list>
{
    using value_type = int;
    static constexpr reflgen::container_kind kind = reflgen::container_kind::sequence;

    static std::size_t size(const custom::frozen_list& list) { return list.items().size(); }

    template<class F>
    static void for_each(const custom::frozen_list& list, F&& function)
    {
        for (const int item : list.items())
        {
            function(item);
        }
    }

    static void assign(custom::frozen_list& list, std::vector<int>&& items)
    {
        list = custom::frozen_list(std::move(items));
    }
};

template<>
struct reflgen::container_traits<custom::string_table>
{
    using key_type = std::string;
    using mapped_type = int;
    static constexpr reflgen::container_kind kind = reflgen::container_kind::map;
    static constexpr bool unique_keys = true;

    static std::size_t size(const custom::string_table& table) { return table.entries().size(); }

    template<class F>
    static void for_each(const custom::string_table& table, F&& function)
    {
        for (const auto& [key, value] : table.entries())
        {
            function(key, value);
        }
    }

    static void clear(custom::string_table& table) { table.wipe(); }
    static void add(custom::string_table& table, std::string&& key, int&& value) { table.set(std::move(key), value); }
};

template<>
struct reflgen::serializer<custom::color>
{
    static void write(reflgen::writer& out, const custom::color& value)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string text = "#";
        for (const unsigned char channel : {value.r, value.g, value.b})
        {
            text += digits[channel >> 4];
            text += digits[channel & 0x0F];
        }
        out.write_string(text);
    }

    static void read(reflgen::reader& in, custom::color& value)
    {
        const std::string text = in.read_string();
        if (text.size() != 7 || text[0] != '#')
        {
            throw reflgen::serialization_error("expected #rrggbb");
        }
        const auto channel = [&](std::size_t offset) {
            return static_cast<unsigned char>(std::stoi(text.substr(offset, 2), nullptr, 16));
        };
        value = {channel(1), channel(3), channel(5)};
    }
};

namespace custom
{
    struct palette
    {
        std::vector<color> colors;
        ring_buffer<int> history;
        string_table table;

        bool operator==(const palette&) const = default;

        static consteval auto reflect()
        {
            return reflgen::schema<palette>(reflgen::field<&palette::colors>, reflgen::field<&palette::history>,
                                            reflgen::field<&palette::table>);
        }
    };
} // namespace custom

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::check_round_trip;
    using reflgen_test::check_throws;
    using reflgen_test::test;

    static_assert(reflgen::serializable<custom::small_vector<int>>);
    static_assert(reflgen::deserializable<custom::small_vector<int>>);
    static_assert(reflgen::deserializable<custom::ring_buffer<std::string>>);
    static_assert(reflgen::deserializable<custom::frozen_list>);
    static_assert(reflgen::deserializable<custom::string_table>);
    static_assert(reflgen::serializable<custom::color> && reflgen::deserializable<custom::color>);

    const test stl_shaped("custom: STL-shaped containers are detected without traits", [] {
        check_round_trip(custom::small_vector<int>{1, 2, 3});
        check_round_trip(custom::small_vector<std::string>{"a"});
        check_equal(reflgen::json::to_string(custom::small_vector<int>{4, 5}), std::string("[4,5]"));
    });

    const test traits_sequence("custom: container_traits with clear/add/reserve", [] {
        custom::ring_buffer<std::string> buffer;
        buffer.put("x");
        buffer.put("y");
        check_round_trip(buffer);
        check_equal(reflgen::json::to_string(buffer), std::string(R"(["x","y"])"));
        // 바이너리 포맷은 원소 수를 알려 주므로 reserve 가 불린다.
        const auto loaded = reflgen_test::binary_round_trip(buffer);
        check_equal(loaded.reserved(), std::size_t{2});
    });

    const test traits_assign("custom: container_traits with assign", [] {
        check_round_trip(custom::frozen_list({7, 8}));
        check_round_trip(custom::frozen_list());
    });

    const test traits_map("custom: container_traits describing a map", [] {
        custom::string_table table;
        table.set("alpha", 1);
        table.set("beta", 2);
        check_round_trip(table);
        check_equal(reflgen::json::to_string(table), std::string(R"({"alpha":1,"beta":2})"));
    });

    const test custom_serializer("custom: serializer<T> specialization changes the shape", [] {
        const custom::color color{0xFF, 0x80, 0x01};
        check_equal(reflgen::json::to_string(color), std::string("\"#ff8001\""));
        check_round_trip(color);
        check_throws([] { reflgen::json::from_string<custom::color>("\"red\""); }, "expected #rrggbb");
    });

    const test nested_customs("custom: custom containers inside reflected types", [] {
        custom::palette palette;
        palette.colors = {{1, 2, 3}};
        palette.history.put(4);
        palette.table.set("k", 5);
        check_round_trip(palette);
        check_equal(reflgen::json::to_string(palette),
                    std::string(R"({"colors":["#010203"],"history":[4],"table":{"k":5}})"));
    });

    // 원소 수 힌트는 남은 입력 바이트로만 묶인다 — 원소가 크면 예약이 그 곱만큼 부푼다.
    // 1000 바이트짜리 입력이 4 KiB 원소 1000개(4 MiB)를 예약시키지 못해야 한다.
    const test bounded_reserve("custom: reserve hints are capped by a byte budget", [] {
        std::vector<std::byte> input{std::byte{0x08}, std::byte{0xE8}, std::byte{0x07}};
        input.resize(input.size() + 1000, std::byte{0x00});

        custom::ring_buffer<std::array<char, 4096>> buffer;
        check_throws([&] { reflgen::binary::from_bytes(input, buffer); }, "expected an array");
        check_equal(buffer.reserved(), reflgen::detail::reserve_budget_bytes / 4096);
    });
} // namespace

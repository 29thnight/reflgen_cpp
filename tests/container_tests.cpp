#include "support.h"
#include <array>
#include <cstddef>
#include <deque>
#include <forward_list>
#include <functional>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <span>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <valarray>
#include <vector>

namespace containers
{
enum class slot
{
    head,
    body
};

struct point
{
    int x = 0;
    int y = 0;

    bool operator==(const point&) const = default;
    auto operator<=>(const point&) const = default;

    static consteval auto reflect()
    {
        return reflgen::schema<point>(reflgen::field<&point::x>, reflgen::field<&point::y>);
    }
};
} // namespace containers

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::check_round_trip;
using reflgen_test::check_round_trip_with;
using reflgen_test::check_throws;
using reflgen_test::test;

const test sequences("container: sequence containers", [] {
    check_round_trip(std::vector<int>{1, 2, 3});
    check_round_trip(std::vector<int>{});
    check_round_trip(std::deque<std::string>{"a", "b"});
    check_round_trip(std::list<double>{0.5, -1.0});
    check_round_trip(std::forward_list<int>{3, 1, 2});
    check_round_trip(std::vector<bool>{true, false, true});
    check_round_trip(std::vector<std::vector<int>>{{1}, {}, {2, 3}});
    check_equal(reflgen::json::to_string(std::forward_list<int>{3, 1, 2}), std::string("[3,1,2]"));
    check_equal(reflgen::json::to_string(std::vector<bool>{true, false}), std::string("[true,false]"));
});

const test sets("container: ordered and unordered sets", [] {
    check_round_trip(std::set<std::string>{"b", "a"});
    check_round_trip(std::multiset<int>{1, 1, 2});
    check_round_trip(std::unordered_set<int>{5, 6, 7});
    check_round_trip(std::unordered_multiset<int>{4, 4});
    check_round_trip(std::set<containers::point>{{1, 2}, {0, 5}});
});

const test object_maps("container: unique maps with string-like keys become objects", [] {
    check_round_trip(std::map<std::string, int>{{"one", 1}, {"two", 2}});
    check_round_trip(std::unordered_map<std::string, std::vector<int>>{{"k", {1, 2}}});
    check_round_trip(std::map<int, std::string>{{-3, "neg"}, {7, "seven"}});
    check_round_trip(std::map<containers::slot, int>{{containers::slot::body, 2}});
    check_round_trip(std::map<bool, int>{{true, 1}, {false, 0}});
    check_round_trip(std::map<std::uint64_t, int>{{18446744073709551615ull, 1}});
    check_equal(reflgen::json::to_string(std::map<int, int>{{1, 2}}), std::string(R"({"1":2})"));
    check_equal(reflgen::json::to_string(std::map<containers::slot, int>{{containers::slot::head, 1}}),
                std::string(R"({"head":1})"));
    check_equal(reflgen::json::to_string(std::map<bool, int>{{true, 1}}), std::string(R"({"true":1})"));
});

const test pair_maps("container: multimaps and structured keys become pair arrays", [] {
    check_round_trip(std::multimap<int, std::string>{{1, "a"}, {1, "b"}});
    check_round_trip(std::unordered_multimap<std::string, int>{{"x", 1}, {"x", 2}});
    check_round_trip(std::map<containers::point, int>{{{1, 2}, 3}});
    check_equal(reflgen::json::to_string(std::multimap<int, int>{{1, 2}, {1, 3}}), std::string("[[1,2],[1,3]]"));
});

const test map_key_errors("container: invalid map keys are reported with their path", [] {
    check_throws([] { reflgen::json::from_string<std::map<int, int>>(R"({"x":1})"); }, "not a valid int");
    check_throws([] { reflgen::json::from_string<std::map<bool, int>>(R"({"yes":1})"); }, "not a valid bool");
    check_throws([] { reflgen::json::from_string<std::map<int, int>>(R"({"1":"s"})"); }, "(at /1)");
    check_throws([] { reflgen::json::from_string<std::multimap<int, int>>("[[1]]"); }, "[key, value]");
    check_throws([] { reflgen::json::from_string<std::multimap<int, int>>("[[1,2,3]]"); }, "[key, value]");
    check_throws([] { reflgen::json::from_string<std::multimap<int, int>>("[[]]"); }, "[key, value]");
});

const test duplicate_keys("container: duplicate object keys, last one wins", [] {
    const auto map = reflgen::json::from_string<std::map<std::string, int>>(R"({"a":1,"a":2})");
    check_equal(map.at("a"), 2);
    const auto from_enum_number = reflgen::json::from_string<std::map<containers::slot, int>>(R"({"1":5})");
    check_equal(from_enum_number.at(containers::slot::body), 5);
});

const test fixed_ranges("container: fixed-size ranges require exact counts", [] {
    check_round_trip(std::array<int, 3>{1, 2, 3});
    check_round_trip(std::array<std::string, 0>{});
    int c_array[3] = {};
    reflgen::json::from_string("[4,5,6]", c_array);
    check_equal(c_array[2], 6);
    check_equal(reflgen::json::to_string(c_array), std::string("[4,5,6]"));
    check_throws([] { reflgen::json::from_string<std::array<int, 3>>("[1,2]"); }, "expected 3 elements, got 2");
    check_throws([] { reflgen::json::from_string<std::array<int, 2>>("[1,2,3]"); }, "got more");

    std::vector<int> storage(2);
    std::span<int> view(storage);
    reflgen::json::from_string("[8,9]", view);
    check_equal(storage[1], 9);
    check_equal(reflgen::json::to_string(std::span<const int>(storage)), std::string("[8,9]"));
});

const test valarrays("container: std::valarray is resized", [] {
    const std::valarray<double> values{1.0, 2.5, -3.0};
    check_round_trip_with(values, [](const std::valarray<double>& a, const std::valarray<double>& b) {
        return a.size() == b.size() && (a == b).min();
    });
});

const test adapters("container: stack, queue and priority_queue keep their order", [] {
    std::stack<int> stack;
    stack.push(1);
    stack.push(2);
    check_round_trip(stack);
    check_equal(reflgen::json::to_string(stack), std::string("[1,2]"));
    check_equal(reflgen_test::json_round_trip(stack).top(), 2);

    std::queue<std::string> queue;
    queue.push("first");
    queue.push("second");
    check_round_trip(queue);
    check_equal(reflgen_test::json_round_trip(queue).front(), std::string("first"));

    std::priority_queue<int, std::vector<int>, std::greater<int>> heap;
    heap.push(5);
    heap.push(1);
    heap.push(3);
    auto loaded = reflgen_test::binary_round_trip(heap);
    check_equal(loaded.size(), std::size_t{3});
    check_equal(loaded.top(), 1);
    loaded.pop();
    check_equal(loaded.top(), 3);

    // 읽기 전 내용은 비워진다.
    std::stack<int> existing;
    existing.push(99);
    reflgen::json::from_string("[7]", existing);
    check_equal(existing.size(), std::size_t{1});
    check_equal(existing.top(), 7);
});

const test byte_blobs("container: std::byte ranges are byte strings", [] {
    const std::vector<std::byte> blob{std::byte{0}, std::byte{0xFF}, std::byte{0x10}};
    check_round_trip(blob);
    check_equal(reflgen::json::to_string(blob), std::string("\"AP8Q\""));
    check_round_trip(std::array<std::byte, 2>{std::byte{1}, std::byte{2}});
    check_throws([] { reflgen::json::from_string<std::array<std::byte, 2>>("\"AP8Q\""); }, "expected 2 bytes");
});

const test reading_replaces_contents("container: reading clears existing elements", [] {
    std::vector<int> values{9, 9, 9};
    reflgen::json::from_string("[1]", values);
    check_equal(values, std::vector<int>{1});
    std::map<std::string, int> map{{"old", 1}};
    reflgen::json::from_string(R"({"new":2})", map);
    check(!map.contains("old"));
});

const test element_paths("container: element failures carry their index", [] {
    check_throws([] { reflgen::json::from_string<std::vector<int>>("[1,2,\"x\"]"); }, "(at /2)");
    check_throws([] { reflgen::json::from_string<std::vector<std::vector<int>>>("[[1],[2,true]]"); }, "(at /1/1)");
});
} // namespace

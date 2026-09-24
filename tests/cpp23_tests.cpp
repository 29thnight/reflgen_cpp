// C++23 빌드에서만 컴파일된다(CMakeLists.txt 가 고른다 — 전처리기로 가르지 않는다).
#include "reflgen/serial/expected.h"
#include "support.h"
#include <expected>
#include <flat_map>
#include <flat_set>
#include <string>

namespace
{
using reflgen_test::check_equal;
using reflgen_test::check_round_trip;
using reflgen_test::check_throws;
using reflgen_test::test;

const test expected_values("cpp23: std::expected is value or error", [] {
    using result = std::expected<int, std::string>;
    check_round_trip(result{5});
    check_round_trip(result{std::unexpect, "failed"});
    check_equal(reflgen::json::to_string(result{5}), std::string(R"({"value":5})"));
    check_equal(reflgen::json::to_string(result{std::unexpect, "no"}), std::string(R"({"error":"no"})"));
    check_round_trip(std::expected<void, int>{});
    check_round_trip(std::expected<void, int>{std::unexpect, 3});
    check_throws([] { static_cast<void>(reflgen::json::from_string<result>("{}")); }, "\"value\" or \"error\"");
    check_throws([] { static_cast<void>(reflgen::json::from_string<result>(R"({"other":1})")); },
                 "\"value\" or \"error\"");
    check_throws([] { static_cast<void>(reflgen::json::from_string<result>(R"({"value":1,"error":"x"})")); },
                 "unexpected key");
});

// flat 컨테이너는 특수화 없이 모양만으로 잡혀야 한다(value_type 이 pair<K, V> 라 map 과 다르다).
const test flat_containers("cpp23: flat_map and flat_set are detected by shape", [] {
    check_round_trip(std::flat_map<std::string, int>{{"b", 2}, {"a", 1}});
    check_round_trip(std::flat_multimap<int, int>{{1, 2}, {1, 3}});
    check_round_trip(std::flat_set<int>{3, 1, 2});
    check_round_trip(std::flat_multiset<int>{1, 1});
    check_equal(reflgen::json::to_string(std::flat_map<int, int>{{1, 2}}), std::string(R"({"1":2})"));
    check_equal(reflgen::json::to_string(std::flat_multimap<int, int>{{1, 2}}), std::string("[[1,2]]"));
});
} // namespace

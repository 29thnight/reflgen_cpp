#include "support.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace objects
{
struct item
{
    std::string name;
    int count = 1;

    bool operator==(const item&) const = default;

    static consteval auto reflect()
    {
        return reflgen::schema<item>(reflgen::field<&item::name>, reflgen::field<&item::count>);
    }
};

struct entity
{
    std::string id = "e";
    int version = 1;

    bool operator==(const entity&) const = default;

    static consteval auto reflect()
    {
        return reflgen::schema<entity>(reflgen::field<&entity::id>, reflgen::field<&entity::version>);
    }
};

struct character : entity
{
    std::string display = "hero";
    std::vector<item> inventory;
    std::map<std::string, item> equipped;
    int cache = 0;
    int legacy = 0;

    bool operator==(const character&) const = default;

    static consteval auto reflect()
    {
        // clang-format off
        return reflgen::schema<character>(
            reflgen::base<entity>,
            reflgen::field<&character::display>.with(reflgen::serialized_name("name")),
            reflgen::field<&character::inventory>,
            reflgen::field<&character::equipped>,
            reflgen::field<&character::cache>.with(reflgen::transient()),
            reflgen::field<&character::legacy>.with(reflgen::required()));
        // clang-format on
    }
};

class account
{
    friend struct reflgen::access;

  public:
    account() = default;
    account(std::string user, int balance) : user_(std::move(user)), balance_(balance) {}

    bool operator==(const account&) const = default;

  private:
    std::string user_;
    int balance_ = 0;

    static consteval auto reflect()
    {
        return reflgen::schema<account>(reflgen::field<&account::user_>.named("user"),
                                        reflgen::field<&account::balance_>.named("balance"));
    }
};

// 외부 서술 대상.
struct vendor_config
{
    int port = 80;
    std::string host = "localhost";

    bool operator==(const vendor_config&) const = default;
};

struct empty
{
    bool operator==(const empty&) const = default;

    static consteval auto reflect() { return reflgen::schema<empty>(); }
};

struct left
{
    int l = 1;

    bool operator==(const left&) const = default;

    static consteval auto reflect() { return reflgen::schema<left>(reflgen::field<&left::l>); }
};

struct right
{
    int r = 2;

    bool operator==(const right&) const = default;

    static consteval auto reflect() { return reflgen::schema<right>(reflgen::field<&right::r>); }
};

struct both : left, right
{
    int b = 3;

    bool operator==(const both&) const = default;

    static consteval auto reflect()
    {
        return reflgen::schema<both>(reflgen::base<left>, reflgen::base<right>, reflgen::field<&both::b>);
    }
};

// 트리 — 자기 자신을 담는 재귀 타입.
struct node
{
    std::string label;
    std::vector<node> children;

    bool operator==(const node&) const = default;

    static consteval auto reflect()
    {
        return reflgen::schema<node>(reflgen::field<&node::label>, reflgen::field<&node::children>);
    }
};
} // namespace objects

template<>
struct reflgen::reflection<objects::vendor_config>
{
    static constexpr auto value = reflgen::schema<objects::vendor_config>(
        reflgen::field<&objects::vendor_config::port>, reflgen::field<&objects::vendor_config::host>);
};

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::check_round_trip;
using reflgen_test::check_throws;
using reflgen_test::test;

objects::character sample()
{
    objects::character value;
    value.display = "knight";
    value.inventory = {{"potion", 3}, {"sword", 1}};
    value.equipped = {{"hand", {"sword", 1}}};
    value.legacy = 4;
    return value;
}

const test nested_round_trip("object: nested objects, containers and inheritance", [] { check_round_trip(sample()); });

const test output_shape("object: parent fields first, renamed and transient fields", [] {
    objects::character value = sample();
    value.cache = 99;
    check_equal(reflgen::json::to_string(value),
                std::string(R"({"id":"e","version":1,"name":"knight",)"
                            R"("inventory":[{"name":"potion","count":3},{"name":"sword","count":1}],)"
                            R"("equipped":{"hand":{"name":"sword","count":1}},"legacy":4})"));
});

const test transient_untouched("object: transient fields are neither written nor read", [] {
    objects::character value = sample();
    value.cache = 99;
    reflgen::json::from_string(R"({"legacy":1,"cache":5})", value);
    check_equal(value.cache, 99);
    check_equal(reflgen_test::json_round_trip(value).cache, 0);
});

const test missing_and_unknown("object: missing keys keep values, unknown keys are skipped", [] {
    objects::character value = sample();
    reflgen::json::from_string(
        R"({"legacy":7,"unknown":{"deep":[1,{"x":null}],"s":"t"},"version":3,"another":[true,false]})", value);
    check_equal(value.version, 3);
    check_equal(value.legacy, 7);
    check_equal(value.display, std::string("knight"));
    check_equal(value.inventory.size(), std::size_t{2});
});

const test required_fields("object: required fields must be present", [] {
    check_throws([] { reflgen::json::from_string<objects::character>(R"({"id":"x"})"); },
                 "missing required field (at /legacy)");
});

const test error_paths("object: errors report the path of the failing value", [] {
    check_throws([] { reflgen::json::from_string<objects::character>(R"({"inventory":[{},{"count":"many"}]})"); },
                 "(at /inventory/1/count)");
    check_throws([] { reflgen::json::from_string<objects::character>(R"({"equipped":{"a/b":{"count":true}}})"); },
                 "(at /equipped/a~1b/count)");
    try
    {
        reflgen::json::from_string<objects::character>(R"({"inventory":[{"name":5}]})");
        check(false, "expected an exception");
    }
    catch (const reflgen::serialization_error& error)
    {
        check_equal(error.path(), std::string("/inventory/0/name"));
        check_equal(error.message().find("json: expected a string"), std::size_t{0});
    }
});

const test private_fields("object: private fields through reflgen::access", [] {
    const objects::account value("kim", 100);
    check_round_trip(value);
    check_equal(reflgen::json::to_string(value), std::string(R"({"user":"kim","balance":100})"));
});

const test external_description("object: externally described types", [] {
    check_round_trip(objects::vendor_config{8080, "example.org"});
    check_equal(reflgen::json::to_string(objects::vendor_config{}), std::string(R"({"port":80,"host":"localhost"})"));
});

const test empty_and_multiple("object: empty types and multiple inheritance", [] {
    check_round_trip(objects::empty{});
    check_equal(reflgen::json::to_string(objects::empty{}), std::string("{}"));
    check_round_trip(objects::both{});
    check_equal(reflgen::json::to_string(objects::both{}), std::string(R"({"l":1,"r":2,"b":3})"));
});

const test recursive_types("object: recursive types", [] {
    objects::node tree{"root", {{"a", {}}, {"b", {{"c", {}}}}}};
    check_round_trip(tree);
});

const test binary_skips_unknown("object: binary format skips unknown keys too", [] {
    const auto bytes = reflgen::binary::to_bytes(sample());
    const auto as_entity = reflgen::binary::from_bytes<objects::entity>(bytes);
    check_equal(as_entity.id, std::string("e"));
});
} // namespace

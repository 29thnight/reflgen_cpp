// 코드 생성기 end-to-end 시험 — game_types.h 의 attribute 에서 생성된 서술이 손으로 쓴
// 서술과 같은 일을 하는가.
#include "game_types.h"
#include "harness.h"
#include "reflgen/json.h"
#include "reflgen_generated_tests.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::require;
using reflgen_test::test;

static_assert(reflgen::reflectable<generated_tests::stats>);
static_assert(reflgen::reflectable<generated_tests::hero>);
static_assert(reflgen::reflectable<generated_tests::nested::marker>);
static_assert(!reflgen::reflectable<generated_tests::plain>);
static_assert(reflgen::reflectable<generated_tests::limits::slot>);
static_assert(!reflgen::reflectable<generated_tests::limits>);

const test schema_names("generated: schema names come from the attribute or the qualified name", [] {
    check_equal(reflgen::schema_of<generated_tests::stats>.name, std::string_view("tests.stats"));
    check_equal(reflgen::schema_of<generated_tests::hero>.name, std::string_view("tests.hero"));
    check_equal(reflgen::schema_of<generated_tests::entity>.name, std::string_view("generated_tests::entity"));
    check_equal(reflgen::schema_of<generated_tests::nested::marker>.name,
                std::string_view("generated_tests::nested::marker"));
});

const test fields_and_directives("generated: all members are reflected except ignored ones", [] {
    std::vector<std::string_view> names;
    reflgen::for_each_field<generated_tests::stats>([&](const auto& field) { names.push_back(field.name); });
    require(names.size() == 4);
    check_equal(names[0], std::string_view("level"));
    check_equal(names[1], std::string_view("health"));
    check_equal(names[2], std::string_view("frame"));
    check_equal(names[3], std::string_view("secret_"));
});

const test attributes("generated: attributes are carried over, including user scopes", [] {
    const auto& fields = reflgen::schema_of<generated_tests::stats>.fields;
    const auto& level = std::get<0>(fields);
    static_assert(std::remove_cvref_t<decltype(level)>::has_attribute<reflgen::range<int>>());
    // max_level 은 클래스의 이름공간에서 찾아진다.
    check_equal(level.attribute<reflgen::range<int>>().max, 99);

    const auto& health = std::get<1>(fields);
    check_equal(health.attribute<reflgen::display_name>().value, std::string_view("Health"));
    check_equal(health.attribute<generated_tests::tooltip>().text, std::string_view("hit points"));

    static_assert(std::remove_cvref_t<decltype(std::get<2>(fields))>::has_attribute<reflgen::transient>());
    check_equal(std::get<3>(fields).attribute<reflgen::serialized_name>().value, std::string_view("secret"));
});

const test methods("generated: methods marked [[reflgen::reflect]] carry parameter names", [] {
    const auto& level_up = std::get<0>(reflgen::schema_of<generated_tests::stats>.methods);
    check_equal(level_up.name, std::string_view("level_up"));
    check_equal(level_up.parameter_names[0], std::string_view("amount"));
    generated_tests::stats stats;
    check_equal(level_up.invoke(stats, 4), 5);
});

const test exact_enum_table("generated: enums get an exact table, even beyond the scan range", [] {
    const auto& entries = reflgen::enum_entries<generated_tests::element>;
    require(entries.size() == 4);
    check_equal(reflgen::enum_name(generated_tests::element::wind), std::string_view("wind"));
    check_equal(reflgen::enum_name(generated_tests::element::earth), std::string_view("earth"));
    check_equal(reflgen::json::to_string(generated_tests::element::wind), std::string("\"wind\""));
});

const test private_members_serialize("generated: private members serialize through reflgen::access", [] {
    generated_tests::stats stats;
    stats.level = 12;
    stats.set_secret(42);
    stats.frame = 99;
    const std::string text = reflgen::json::to_string(stats);
    check_equal(text, std::string(R"({"level":12,"health":100.0,"secret":42})"));
    const auto loaded = reflgen::json::from_string<generated_tests::stats>(text);
    check_equal(loaded.secret(), 42);
    check_equal(loaded.frame, 0);
});

const test class_member_names("generated: attribute arguments see the class's and enclosing classes' members", [] {
    const auto& fields = reflgen::schema_of<generated_tests::limits::slot>.fields;
    const auto& count = std::get<0>(fields);
    check_equal(count.attribute<reflgen::range<int>>().min, 1);  // slot::floor
    check_equal(count.attribute<reflgen::range<int>>().max, 64); // limits::cap
    check_equal(std::get<2>(fields).attribute<reflgen::display_name>().value, std::string_view("Tag"));
});

const test declarator_attributes("generated: an attribute after a declarator applies to that declarator only", [] {
    std::vector<std::string_view> names;
    reflgen::for_each_field<generated_tests::limits::slot>([&](const auto& field) { names.push_back(field.name); });
    require(names.size() == 3);
    check_equal(names[0], std::string_view("count"));
    check_equal(names[1], std::string_view("weight"));
    check_equal(names[2], std::string_view("tag"));
});

const test conversion_operator("generated: conversion operators can be reflected", [] {
    const auto& truthy = std::get<0>(reflgen::schema_of<generated_tests::limits::slot>.methods);
    check_equal(truthy.name, std::string_view("operator bool"));
    generated_tests::limits::slot slot;
    check(truthy.invoke(slot));
    slot.count = 0;
    check(!truthy.invoke(slot));
});

const test inheritance_and_registration("generated: bases, registration and polymorphic tags", [] {
    reflgen::generated::register_generated_tests();
    const reflgen::type_descriptor* hero = reflgen::default_registry().find("tests.hero");
    require(hero != nullptr);
    check_equal(hero->fields()[0].name(), std::string_view("name"));
    check(hero->bases().size() == 1);
    check(reflgen::default_registry().find("tests.stats") != nullptr);
    check(reflgen::default_registry().find("generated_tests::nested::marker") != nullptr);

    std::unique_ptr<generated_tests::entity> actor = std::make_unique<generated_tests::hero>();
    auto& concrete = static_cast<generated_tests::hero&>(*actor);
    concrete.name = "arthur";
    concrete.affinity = generated_tests::element::wind;
    concrete.tags = {"brave"};
    concrete.inventory = {{"potion", 3}};
    const std::string text = reflgen::json::to_string(actor);
    check(text.starts_with(R"({"type":"tests.hero","value":{"name":"arthur","base_stats":)"), text);

    const auto loaded = reflgen::json::from_string<std::unique_ptr<generated_tests::entity>>(text);
    const auto* loaded_hero = dynamic_cast<const generated_tests::hero*>(loaded.get());
    require(loaded_hero != nullptr);
    check_equal(loaded_hero->name, std::string("arthur"));
    check(loaded_hero->affinity == generated_tests::element::wind);
    check_equal(loaded_hero->inventory.at("potion"), 3);
});
} // namespace

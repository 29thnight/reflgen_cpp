#include "harness.h"
#include "reflgen/core/attributes.h"
#include "reflgen/core/schema.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace schemas
{
// 사용자 정의 속성 — 생성자가 constexpr 인 구조체 하나면 된다.
struct tooltip
{
    std::string_view text;

    constexpr explicit tooltip(std::string_view value) : text(value) {}
};

struct creature
{
    int hp = 10;
    float speed = 1.0f;

    int heal(int amount)
    {
        hp += amount;
        return hp;
    }

    static consteval auto reflect()
    {
        // clang-format off
        return reflgen::schema<creature>(
            reflgen::field<&creature::hp>.with(reflgen::range(0, 100), tooltip("health")),
            reflgen::field<&creature::speed>.named("move_speed"),
            reflgen::method<&creature::heal>.parameters("amount"))
            .named("schemas.creature")
            .with(reflgen::display_name("Creature"));
        // clang-format on
    }
};

struct dragon : creature
{
    bool flying = true;

    static consteval auto reflect()
    {
        return reflgen::schema<dragon>(reflgen::base<creature>, reflgen::field<&dragon::flying>);
    }
};

// reflect() 를 private 으로 두고 access 로 연다.
class vault
{
    friend struct reflgen::access;

  public:
    int gold() const { return gold_; }

  private:
    int gold_ = 7;

    static consteval auto reflect() { return reflgen::schema<vault>(reflgen::field<&vault::gold_>); }
};

// 손댈 수 없는 타입 — 외부 서술.
struct third_party
{
    int a = 1;
    std::string b = "b";
};

// 부모의 reflect() 를 물려받기만 한 타입.
struct forgot_reflect : creature
{
    int extra = 0;
};

struct plain
{
    int x;
};
} // namespace schemas

template<>
struct reflgen::reflection<schemas::third_party>
{
    static constexpr auto value = reflgen::schema<schemas::third_party>(reflgen::field<&schemas::third_party::a>,
                                                                        reflgen::field<&schemas::third_party::b>);
};

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::test;

static_assert(reflgen::reflectable<schemas::creature>);
static_assert(reflgen::reflectable<schemas::vault>);
static_assert(reflgen::reflectable<schemas::third_party>);
static_assert(!reflgen::reflectable<schemas::plain>);
static_assert(!reflgen::reflectable<int>);
// 물려받은 reflect() 는 자기 서술로 치지 않는다.
static_assert(!reflgen::detail::member_reflection_is_own<schemas::forgot_reflect>());

const test local_schema("schema: local fields, names and type name", [] {
    constexpr const auto& schema = reflgen::schema_of<schemas::creature>;
    static_assert(std::remove_cvref_t<decltype(schema)>::field_count == 2);
    static_assert(std::remove_cvref_t<decltype(schema)>::method_count == 1);
    check_equal(schema.name, std::string_view("schemas.creature"));
    check_equal(std::get<0>(schema.fields).name, std::string_view("hp"));
    check_equal(std::get<1>(schema.fields).name, std::string_view("move_speed"));
});

const test compile_time_attributes("schema: attributes are queryable at compile time", [] {
    constexpr const auto& hp = std::get<0>(reflgen::schema_of<schemas::creature>.fields);
    using hp_type = std::remove_cvref_t<decltype(hp)>;
    static_assert(hp_type::has_attribute<reflgen::range<int>>());
    static_assert(hp_type::has_attribute<schemas::tooltip>());
    static_assert(!hp_type::has_attribute<reflgen::transient>());
    static_assert(hp.attribute<reflgen::range<int>>().max == 100);
    check_equal(hp.attribute<schemas::tooltip>().text, std::string_view("health"));

    constexpr const auto& schema = reflgen::schema_of<schemas::creature>;
    static_assert(std::remove_cvref_t<decltype(schema)>::has_attribute<reflgen::display_name>());
    check_equal(schema.attribute<reflgen::display_name>().value, std::string_view("Creature"));
});

const test methods("schema: methods carry parameter names and can be invoked", [] {
    constexpr const auto& heal = std::get<0>(reflgen::schema_of<schemas::creature>.methods);
    check_equal(heal.name, std::string_view("heal"));
    check_equal(heal.parameter_names[0], std::string_view("amount"));
    schemas::creature creature;
    check_equal(heal.invoke(creature, 5), 15);
    check_equal(creature.hp, 15);

    std::size_t count = 0;
    reflgen::for_each_method<schemas::dragon>([&](const auto&) { ++count; });
    check_equal(count, std::size_t{1});
});

const test inheritance_order("schema: fields are visited parent first", [] {
    std::vector<std::string_view> visited;
    reflgen::for_each_field<schemas::dragon>([&](const auto& field) { visited.push_back(field.name); });
    check_equal(visited.size(), std::size_t{3});
    check_equal(visited[0], std::string_view("hp"));
    check_equal(visited[1], std::string_view("move_speed"));
    check_equal(visited[2], std::string_view("flying"));
    static_assert(reflgen::field_count<schemas::dragon>() == 3);
    static_assert(std::is_same_v<reflgen::direct_bases_t<schemas::dragon>, reflgen::type_list<schemas::creature>>);
});

const test object_visit("schema: for_each_field(object) exposes member references", [] {
    schemas::dragon dragon;
    int total = 0;
    reflgen::for_each_field(dragon, [&](const auto& field, auto& value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, int>)
        {
            total += value;
            value = 42;
            check_equal(field.name, std::string_view("hp"));
        }
    });
    check_equal(total, 10);
    check_equal(dragon.hp, 42);
});

const test private_members("schema: private reflect() through reflgen::access", [] {
    schemas::vault vault;
    int seen = 0;
    reflgen::for_each_field(vault, [&](const auto&, const int& value) { seen = value; });
    check_equal(seen, vault.gold());
});

const test external_schema("schema: reflection<T> describes a type from outside", [] {
    constexpr const auto& schema = reflgen::schema_of<schemas::third_party>;
    check_equal(schema.name, std::string_view("schemas::third_party"));
    check_equal(std::get<1>(schema.fields).name, std::string_view("b"));
});

const test runtime_attribute_view("schema: attribute_list finds attributes by type", [] {
    constexpr const auto& hp = std::get<0>(reflgen::schema_of<schemas::creature>.fields);
    const std::array refs = {
        reflgen::attribute_ref{reflgen::type_id_of<reflgen::range<int>>(), &std::get<0>(hp.attributes)},
        reflgen::attribute_ref{reflgen::type_id_of<schemas::tooltip>(), &std::get<1>(hp.attributes)}};
    const reflgen::attribute_list list(refs);
    check_equal(list.size(), std::size_t{2});
    check(!list.empty());
    check(list.contains<schemas::tooltip>());
    check(!list.contains<reflgen::hidden>());
    const auto* range = list.find<reflgen::range<int>>();
    reflgen_test::require(range != nullptr);
    check_equal(range->min, 0);
    check(list.begin()->get_if<reflgen::range<int>>() == range);
    check(reflgen::attribute_list{}.empty());
});
} // namespace

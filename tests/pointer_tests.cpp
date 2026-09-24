#include "support.h"
#include <memory>
#include <string>
#include <vector>

namespace pointers
{
struct plain
{
    int value = 1;

    bool operator==(const plain&) const = default;

    static consteval auto reflect() { return reflgen::schema<plain>(reflgen::field<&plain::value>); }
};

struct component
{
    virtual ~component() = default;
    virtual std::string kind() const { return "component"; }

    bool enabled = true;

    static consteval auto reflect()
    {
        return reflgen::schema<component>(reflgen::field<&component::enabled>).named("pointers.component");
    }
};

struct light : component
{
    std::string kind() const override { return "light"; }

    float intensity = 1.0f;

    static consteval auto reflect()
    {
        return reflgen::schema<light>(reflgen::base<component>, reflgen::field<&light::intensity>)
            .named("pointers.light");
    }
};

struct spot_light : light
{
    std::string kind() const override { return "spot_light"; }

    float angle = 30.0f;

    static consteval auto reflect()
    {
        return reflgen::schema<spot_light>(reflgen::base<light>, reflgen::field<&spot_light::angle>)
            .named("pointers.spot_light");
    }
};

// 다중 상속 — component 가 두 번째 부모라 포인터 보정이 필요하다.
struct tag_holder
{
    int tag = 5;

    static consteval auto reflect() { return reflgen::schema<tag_holder>(reflgen::field<&tag_holder::tag>); }
};

struct sensor : tag_holder, component
{
    std::string kind() const override { return "sensor"; }

    int range = 10;

    static consteval auto reflect()
    {
        return reflgen::schema<sensor>(reflgen::base<tag_holder>, reflgen::base<component>,
                                       reflgen::field<&sensor::range>)
            .named("pointers.sensor");
    }
};

// 등록하지 않는 파생.
struct secret_component : component
{
    static consteval auto reflect()
    {
        return reflgen::schema<secret_component>(reflgen::base<component>).named("pointers.secret");
    }
};

// component 의 파생임을 base<> 로 선언하지 않은 타입.
struct undeclared : component
{
    int x = 0;

    static consteval auto reflect()
    {
        return reflgen::schema<undeclared>(reflgen::field<&undeclared::x>).named("pointers.undeclared");
    }
};

struct scene
{
    std::vector<std::unique_ptr<component>> components;
    std::shared_ptr<component> main;

    static consteval auto reflect()
    {
        return reflgen::schema<scene>(reflgen::field<&scene::components>, reflgen::field<&scene::main>);
    }
};

void register_all()
{
    reflgen::register_type<light>();
    reflgen::register_type<spot_light>();
    reflgen::register_type<sensor>();
    reflgen::register_type<undeclared>();
}
} // namespace pointers

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::check_throws;
using reflgen_test::require;
using reflgen_test::test;

const test non_polymorphic("pointer: non-polymorphic pointers are null or the value", [] {
    const auto unique = std::make_unique<pointers::plain>();
    check_equal(reflgen::json::to_string(unique), std::string(R"({"value":1})"));
    const auto loaded = reflgen_test::json_round_trip(unique);
    require(loaded != nullptr);
    check_equal(*loaded, *unique);

    const std::shared_ptr<pointers::plain> empty;
    check_equal(reflgen::json::to_string(empty), std::string("null"));
    check(reflgen_test::binary_round_trip(empty) == nullptr);

    const auto shared = std::make_shared<int>(4);
    check_equal(*reflgen_test::binary_round_trip(shared), 4);
});

const test polymorphic_round_trip("pointer: polymorphic pointers keep their dynamic type", [] {
    pointers::register_all();
    pointers::scene scene;
    auto spot = std::make_unique<pointers::spot_light>();
    spot->angle = 45.0f;
    spot->intensity = 2.0f;
    scene.components.push_back(std::move(spot));
    scene.components.push_back(std::make_unique<pointers::sensor>());
    scene.components.push_back(nullptr);
    scene.main = std::make_shared<pointers::light>();

    const std::string text = reflgen::json::to_string(scene);
    check(text.find(R"({"type":"pointers.spot_light","value":{"enabled":true,"intensity":2.0,"angle":45.0}})") !=
              std::string::npos,
          text);

    const auto verify = [](const pointers::scene& loaded) {
        require(loaded.components.size() == 3);
        check_equal(loaded.components[0]->kind(), std::string("spot_light"));
        check_equal(static_cast<const pointers::spot_light&>(*loaded.components[0]).angle, 45.0f);
        check_equal(loaded.components[1]->kind(), std::string("sensor"));
        // 두 번째 부모로의 포인터 보정이 맞아야 필드가 제자리에 있다.
        const auto& sensor = dynamic_cast<const pointers::sensor&>(*loaded.components[1]);
        check_equal(sensor.range, 10);
        check_equal(sensor.tag, 5);
        check(loaded.components[2] == nullptr);
        require(loaded.main != nullptr);
        check_equal(loaded.main->kind(), std::string("light"));
    };
    verify(reflgen::json::from_string<pointers::scene>(text));
    verify(reflgen::binary::from_bytes<pointers::scene>(reflgen::binary::to_bytes(scene)));
});

const test exact_static_type("pointer: the static type itself needs no registration", [] {
    const std::unique_ptr<pointers::component> base = std::make_unique<pointers::component>();
    const std::string text = reflgen::json::to_string(base);
    check_equal(text, std::string(R"({"type":"pointers.component","value":{"enabled":true}})"));
    const auto loaded = reflgen::json::from_string<std::unique_ptr<pointers::component>>(text);
    require(loaded != nullptr);
    check_equal(loaded->kind(), std::string("component"));
});

const test polymorphic_errors("pointer: polymorphic failures are explicit", [] {
    pointers::register_all();
    check_throws(
        [] {
            const std::unique_ptr<pointers::component> secret = std::make_unique<pointers::secret_component>();
            reflgen::json::to_string(secret);
        },
        "is not registered");
    using pointer = std::unique_ptr<pointers::component>;
    check_throws([] { reflgen::json::from_string<pointer>(R"({"type":"nope","value":{}})"); },
                 "unknown polymorphic type");
    check_throws([] { reflgen::json::from_string<pointer>(R"({"value":{},"type":"pointers.light"})"); },
                 "must start with a \"type\" key");
    check_throws([] { reflgen::json::from_string<pointer>(R"({"type":"pointers.light"})"); }, "\"value\" key");
    check_throws([] { reflgen::json::from_string<pointer>(R"({"type":"pointers.light","value":{},"x":1})"); },
                 "unexpected key 'x'");
    check_throws([] { reflgen::json::from_string<pointer>(R"({"type":"pointers.undeclared","value":{}})"); },
                 "is not declared as derived from");
    check_throws([] { reflgen::json::from_string<pointer>(R"({"type":"pointers.light","value":{"intensity":"x"}})"); },
                 "(at /value/intensity)");
});
} // namespace

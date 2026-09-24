#include "support.h"
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace runtime
{
    struct marker
    {
        int level;

        constexpr explicit marker(int value) : level(value) {}
    };

    struct shape
    {
        virtual ~shape() = default;
        std::string label = "shape";

        static consteval auto reflect()
        {
            return reflgen::schema<shape>(reflgen::field<&shape::label>.with(reflgen::description("label")))
                .named("runtime.shape")
                .with(marker(1));
        }
    };

    struct tagged
    {
        int tag = 3;

        static consteval auto reflect() { return reflgen::schema<tagged>(reflgen::field<&tagged::tag>); }
    };

    // 다중 상속 — 두 번째 부모는 오프셋이 0이 아니다.
    struct circle : tagged, shape
    {
        double radius = 1.5;
        std::string label = "circle";

        static consteval auto reflect()
        {
            return reflgen::schema<circle>(
                       reflgen::base<tagged>, reflgen::base<shape>,
                       reflgen::field<&circle::radius>.with(reflgen::range(0.0, 10.0), marker(7)),
                       reflgen::field<&circle::label>.with(reflgen::serialized_name("circle_label")))
                .named("runtime.circle");
        }
    };

    struct with_callback
    {
        int value = 1;
        std::function<void()> callback;

        static consteval auto reflect()
        {
            return reflgen::schema<with_callback>(reflgen::field<&with_callback::value>,
                                                  reflgen::field<&with_callback::callback>);
        }
    };

    struct scene
    {
        tagged item;

        static consteval auto reflect() { return reflgen::schema<scene>(reflgen::field<&scene::item>); }
    };

    struct other_named
    {
        int x = 0;

        static consteval auto reflect()
        {
            return reflgen::schema<other_named>(reflgen::field<&other_named::x>).named("runtime.circle");
        }
    };
} // namespace runtime

namespace
{
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::check_throws;
    using reflgen_test::require;
    using reflgen_test::test;

    const test descriptor_fields("runtime: descriptors list inherited fields parent first", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::circle>();
        check_equal(type.name(), std::string_view("runtime.circle"));
        check_equal(type.size(), sizeof(runtime::circle));
        check_equal(type.alignment(), alignof(runtime::circle));
        require(type.fields().size() == 4);
        check_equal(type.fields()[0].name(), std::string_view("tag"));
        check_equal(type.fields()[1].name(), std::string_view("label"));
        check_equal(type.fields()[1].declaring_type_name(), std::string_view("runtime::shape"));
        check_equal(type.fields()[2].name(), std::string_view("radius"));
        check_equal(type.fields()[3].key(), std::string_view("circle_label"));
        check_equal(type.fields()[2].type_name(), std::string_view("double"));
        check(type.fields()[2].type() == reflgen::type_id_of<double>());
        check(type.fields()[2].reflected_type() == nullptr);
    });

    const test field_access("runtime: field addresses work through base offsets", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::circle>();
        runtime::circle circle;
        // 부모(shape)의 label 과 자식의 label 이 다른 필드다.
        auto* base_label = static_cast<std::string*>(type.fields()[1].address(&circle));
        *base_label = "changed";
        check_equal(static_cast<const runtime::shape&>(circle).label, std::string("changed"));
        check_equal(circle.label, std::string("circle"));

        const auto* radius = static_cast<const double*>(type.fields()[2].address(static_cast<const void*>(&circle)));
        check_equal(*radius, 1.5);
    });

    const test find_field("runtime: find_field prefers the most derived declaration", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::circle>();
        const reflgen::field_info* label = type.find_field("label");
        require(label != nullptr);
        check_equal(label->declaring_type_name(), std::string_view("runtime::circle"));
        check(type.find_field("missing") == nullptr);
    });

    const test runtime_attributes("runtime: attributes are readable through type erasure", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::circle>();
        const reflgen::attribute_list attributes = type.fields()[2].attributes();
        const auto* range = attributes.find<reflgen::range<double>>();
        require(range != nullptr);
        check_equal(range->max, 10.0);
        const auto* custom = attributes.find<runtime::marker>();
        require(custom != nullptr);
        check_equal(custom->level, 7);
        check(!attributes.contains<reflgen::hidden>());

        const reflgen::type_descriptor& shape = reflgen::type_descriptor_of<runtime::shape>();
        const auto* type_marker = shape.attributes().find<runtime::marker>();
        require(type_marker != nullptr);
        check_equal(type_marker->level, 1);
        check_equal(shape.fields()[0].attributes().find<reflgen::description>()->value, std::string_view("label"));
    });

    const test reflected_field_types("runtime: fields and bases of reflected types link to descriptors", [] {
        const reflgen::type_descriptor& scene = reflgen::type_descriptor_of<runtime::scene>();
        check(scene.fields()[0].reflected_type() == &reflgen::type_descriptor_of<runtime::tagged>());

        const reflgen::type_descriptor& circle = reflgen::type_descriptor_of<runtime::circle>();
        check(circle.bases().size() == 2);
        check(circle.bases()[1].descriptor() == &reflgen::type_descriptor_of<runtime::shape>());
        check(circle.bases()[0].type() == reflgen::type_id_of<runtime::tagged>());
    });

    const test upcast("runtime: upcast follows declared bases with pointer adjustment", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::circle>();
        runtime::circle circle;
        void* as_shape = type.upcast(&circle, reflgen::type_id_of<runtime::shape>());
        check(as_shape == static_cast<runtime::shape*>(&circle));
        void* as_tagged = type.upcast(&circle, reflgen::type_id_of<runtime::tagged>());
        check(as_tagged == static_cast<runtime::tagged*>(&circle));
        check(type.upcast(&circle, reflgen::type_id_of<runtime::circle>()) == &circle);
        check(type.upcast(&circle, reflgen::type_id_of<int>()) == nullptr);
    });

    const test create_and_serialize("runtime: create and serialize through the descriptor", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::circle>();
        require(type.is_constructible());
        reflgen::type_descriptor::instance instance = type.create();
        static_cast<runtime::circle*>(instance.get())->radius = 4.0;

        std::string text;
        reflgen::json::writer out(text);
        type.serialize(out, instance.get());
        check_equal(text, std::string(R"({"tag":3,"label":"shape","radius":4.0,"circle_label":"circle"})"));

        reflgen::json::reader in(R"({"radius":9.5})");
        type.deserialize(in, instance.get());
        check_equal(static_cast<runtime::circle*>(instance.get())->radius, 9.5);

        std::string field_text;
        reflgen::json::writer field_out(field_text);
        type.fields()[2].serialize(field_out, instance.get());
        check_equal(field_text, std::string("9.5"));

        reflgen::json::reader field_in("2.0");
        type.fields()[2].deserialize(field_in, instance.get());
        check_equal(static_cast<runtime::circle*>(instance.get())->radius, 2.0);
    });

    const test non_serializable_fields("runtime: non-serializable fields fail at run time only", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::with_callback>();
        check(!type.is_serializable());
        check(!type.is_deserializable());
        check(type.fields()[0].is_serializable());
        check(!type.fields()[1].is_serializable());
        runtime::with_callback object;
        std::string text;
        reflgen::json::writer out(text);
        check_throws([&] { type.serialize(out, &object); }, "is not serializable");
        check_throws([&] { type.fields()[1].serialize(out, &object); }, "field 'callback' is not serializable");
        reflgen::json::reader in("{}");
        check_throws([&] { type.deserialize(in, &object); }, "is not deserializable");
        check_throws([&] { type.fields()[1].deserialize(in, &object); }, "is not deserializable");
    });

    const test registry_lookup("runtime: registry finds by name, id and dynamic type", [] {
        reflgen::registry registry;
        const reflgen::type_descriptor& circle = registry.add<runtime::circle>();
        registry.add<runtime::circle>(); // 같은 서술자는 다시 넣어도 된다
        reflgen::register_type<runtime::shape>(registry);
        check_equal(registry.size(), std::size_t{2});
        check(registry.find("runtime.circle") == &circle);
        check(registry.find(reflgen::type_id_of<runtime::circle>()) == &circle);
        check(registry.find("missing") == nullptr);
        check(registry.find(reflgen::type_id_of<int>()) == nullptr);

        const runtime::circle object;
        const runtime::shape& as_shape = object;
        check(registry.find(typeid(as_shape)) == &circle);
        check(registry.find(typeid(int)) == nullptr);
        check(registry.types().front() == &circle);
    });

    const test registry_conflicts("runtime: registry rejects two types under one name", [] {
        reflgen::registry registry;
        registry.add<runtime::circle>();
        check_throws<std::invalid_argument>([&] { registry.add<runtime::other_named>(); }, "already registered");
    });
} // namespace

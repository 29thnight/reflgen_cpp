#include "support.h"
#include <functional>
#include <stdexcept>
#include <memory>
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

    // 런타임 열거형 — 필드가 자기 열거자 표를 든다.
    enum class mood : unsigned char
    {
        calm = 1,
        angry = 7,
    };

    struct creature
    {
        mood state = mood::calm;
        int legs = 4;

        static consteval auto reflect()
        {
            return reflgen::schema<creature>(reflgen::field<&creature::state>, reflgen::field<&creature::legs>);
        }
    };

    // 런타임 메서드 — 인자·반환값·const·noexcept·상속.
    struct counter
    {
        int value = 0;

        int add(int amount)
        {
            value += amount;
            return value;
        }

        std::string describe(const std::string& prefix, bool loud) const
        {
            return prefix + std::to_string(value) + (loud ? "!" : "");
        }

        void reset() noexcept { value = 0; }

        static consteval auto reflect()
        {
            return reflgen::schema<counter>(
                reflgen::field<&counter::value>, reflgen::method<&counter::add>.parameters("amount"),
                reflgen::method<&counter::describe>.parameters("prefix", "loud").with(marker(2)),
                reflgen::method<&counter::reset>);
        }
    };

    struct bounded_counter : counter
    {
        int limit = 10;

        bool at_limit() const { return value >= limit; }

        static consteval auto reflect()
        {
            return reflgen::schema<bounded_counter>(reflgen::base<counter>, reflgen::field<&bounded_counter::limit>,
                                                    reflgen::method<&bounded_counter::at_limit>);
        }
    };

    // 참조 파라미터 — 인자 칸의 객체를 그대로 받는다(쓰면 호출자 쪽에 보인다). rvalue 참조 파라미터는 그 객체에서 옮긴다.
    struct mailbox
    {
        std::string stored;
        std::unique_ptr<int> held;

        void read_into(std::string& out) const { out = stored; }
        void take(std::unique_ptr<int>&& value) { held = std::move(value); }

        static consteval auto reflect()
        {
            return reflgen::schema<mailbox>(reflgen::method<&mailbox::read_into>, reflgen::method<&mailbox::take>);
        }
    };

    // 대입 연산이 없는 타입 — 참조로 돌려주면 호출자의 result 에 담을 수 없다.
    struct fixed_id
    {
        const int value;
    };

    // 런타임 호출의 가장자리: move-only 값 파라미터, 담을 수 없는 반환, 참조 한정자, volatile.
    struct odd_signatures
    {
        std::unique_ptr<int> held;
        fixed_id id{5};
        int calls = 0;

        void adopt(std::unique_ptr<int> value) { held = std::move(value); }
        const fixed_id& identity() const { return id; }
        int as_lvalue() & { return ++calls; }
        int as_rvalue() && { return ++calls; }
        int as_volatile() volatile { return 0; }

        static consteval auto reflect()
        {
            return reflgen::schema<odd_signatures>(
                reflgen::method<&odd_signatures::adopt>, reflgen::method<&odd_signatures::identity>,
                reflgen::method<&odd_signatures::as_lvalue>, reflgen::method<&odd_signatures::as_rvalue>,
                reflgen::method<&odd_signatures::as_volatile>);
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

    const test runtime_enums("runtime: enum fields carry their enumerator table", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::creature>();
        check(type.find_field("legs")->enumeration() == nullptr);
        const reflgen::enum_descriptor* mood = type.find_field("state")->enumeration();
        require(mood != nullptr);
        check(mood == &reflgen::enum_descriptor_of<runtime::mood>());
        check(mood->id() == reflgen::type_id_of<runtime::mood>());
        require(mood->entries().size() == 2);
        check_equal(mood->entries()[0].name, std::string_view("calm"));
        check_equal(mood->entries()[1].value, 7LL);
        check(mood->find("angry") == &mood->entries()[1]);
        check(mood->find(1LL) == &mood->entries()[0]);
        check(mood->find("sleepy") == nullptr);

        // 필드 주소로 값을 읽고 쓴다 — 기반 타입의 폭은 서술자가 안다.
        runtime::creature object;
        void* state = type.find_field("state")->address(&object);
        check_equal(mood->read(state), 1LL);
        mood->write(state, 7);
        check(object.state == runtime::mood::angry);
    });

    const test descriptor_methods("runtime: descriptors list inherited methods parent first with their signatures", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::bounded_counter>();
        require(type.methods().size() == 4);
        check_equal(type.methods()[0].name(), std::string_view("add"));
        check_equal(type.methods()[1].name(), std::string_view("describe"));
        check_equal(type.methods()[2].name(), std::string_view("reset"));
        check_equal(type.methods()[3].name(), std::string_view("at_limit"));
        check_equal(type.methods()[0].declaring_type_name(), std::string_view("runtime::counter"));
        check_equal(type.methods()[3].declaring_type_name(), std::string_view("runtime::bounded_counter"));

        const reflgen::method_info& describe = type.methods()[1];
        require(describe.parameters().size() == 2);
        check_equal(describe.parameters()[0].name(), std::string_view("prefix"));
        check(describe.parameters()[0].type() == reflgen::type_id_of<std::string>());
        check_equal(describe.parameters()[1].name(), std::string_view("loud"));
        check(describe.parameters()[1].type() == reflgen::type_id_of<bool>());
        check(describe.return_type() == reflgen::type_id_of<std::string>());
        check(describe.is_const());
        check(!type.methods()[0].is_const());
        check(type.methods()[2].return_type() == reflgen::type_id_of<void>());
        const auto* tag = describe.attributes().find<runtime::marker>();
        require(tag != nullptr);
        check_equal(tag->level, 2);
        check(type.find_method("at_limit") == &type.methods()[3]);
        check(type.find_method("missing") == nullptr);
    });

    const test method_invoke("runtime: methods are invoked through type erasure", [] {
        runtime::bounded_counter object;
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::bounded_counter>();

        int amount = 7;
        int sum = 0;
        void* add_arguments[] = {&amount};
        type.find_method("add")->invoke(&object, add_arguments, &sum);
        check_equal(sum, 7);
        check_equal(object.value, 7);
        type.find_method("add")->invoke(&object, add_arguments); // 반환값을 버린다
        check_equal(object.value, 14);

        std::string prefix = "n=";
        bool loud = true;
        std::string text;
        void* describe_arguments[] = {&prefix, &loud};
        type.find_method("describe")->invoke(&object, describe_arguments, &text);
        check_equal(text, std::string("n=14!"));

        bool full = false;
        type.find_method("at_limit")->invoke(&object, {}, &full);
        check(full);
        type.find_method("reset")->invoke(&object, {});
        check_equal(object.value, 0);

        check_throws<std::invalid_argument>([&] { type.find_method("add")->invoke(&object, {}); },
                                            "expects 1 argument");
    });

    const test method_reference_parameters(
        "runtime: reference parameters get the argument, rvalue ones move from it", [] {
            runtime::mailbox box;
            const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::mailbox>();

            auto value = std::make_unique<int>(42);
            void* take_arguments[] = {&value};
            type.find_method("take")->invoke(&box, take_arguments);
            check(value == nullptr); // 옮겨졌다 — 옮긴 unique_ptr 은 비는 것이 보장된다
            require(box.held != nullptr);
            check_equal(*box.held, 42);
            check(type.find_method("take")->parameters()[0].type() == reflgen::type_id_of<std::unique_ptr<int>>());

            box.stored = "letter";
            std::string copy;
            void* read_arguments[] = {&copy};
            type.find_method("read_into")->invoke(&box, read_arguments);
            check_equal(copy, std::string("letter")); // 참조 파라미터 — 호출자의 객체에 썼다
        });

    // 등록 함수는 반영된 메서드 전부의 호출 썽크를 만든다 — 어떤 시그니처에서도 컴파일이 멈추면 안 된다. 인자 칸으로
    // 부를 수 없는 메서드(&& 한정자, volatile)는 런타임 표에서 빠진다(컴파일 때 서술 — schema_of — 에는 남는다).
    const test method_signature_edges("runtime: every method signature compiles; uncallable ones are left out", [] {
        const reflgen::type_descriptor& type = reflgen::type_descriptor_of<runtime::odd_signatures>();
        require(type.methods().size() == 3);
        check(type.find_method("as_rvalue") == nullptr);
        check(type.find_method("as_volatile") == nullptr);
        check_equal(
            std::tuple_size_v<std::remove_cvref_t<decltype(reflgen::schema_of<runtime::odd_signatures>.methods)>>,
            std::size_t{5});

        runtime::odd_signatures object;
        auto value = std::make_unique<int>(9);
        void* adopt_arguments[] = {&value};
        type.find_method("adopt")->invoke(&object, adopt_arguments); // 복사할 수 없는 값 파라미터 — 옮긴다
        check(value == nullptr);
        require(object.held != nullptr);
        check_equal(*object.held, 9);

        int calls = 0;
        type.find_method("as_lvalue")->invoke(&object, {}, &calls);
        check_equal(calls, 1);

        type.find_method("identity")->invoke(&object, {}); // 반환값을 버리는 것은 된다
        runtime::fixed_id target{0};
        check_throws<std::invalid_argument>([&] { type.find_method("identity")->invoke(&object, {}, &target); },
                                            "cannot be assigned");
    });
} // namespace

#include "support.h"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>
#if defined(__cpp_lib_expected)
#include "reflgen/serial/expected.h"
#endif

namespace runtime_values
{
    struct leaf
    {
        int number = 3;
        std::string text = "native";
        static consteval auto reflect()
        { return reflgen::schema<leaf>(reflgen::field<&leaf::number>, reflgen::field<&leaf::text>); }
    };
    struct recursive
    {
        int number = 1;
        std::vector<recursive> children;
        static consteval auto reflect()
        { return reflgen::schema<recursive>(reflgen::field<&recursive::number>, reflgen::field<&recursive::children>); }
    };
    struct wrapped_string { std::string text; };
    struct opaque { int number = 0; };
    struct script_only { int number = 1; std::string name = "runtime"; };
    struct indexed_map { std::vector<std::pair<std::string, script_only>> entries{{"first", {}}}; };
    struct opaque_map {};
    struct script_owner
    {
        std::vector<script_only> values{{}};
        script_only update(script_only value, const std::vector<script_only>& items)
        { value.number += items.front().number; return value; }
    };
    struct dual_schema
    {
        int saved = 1;
        int runtime = 2;
        static consteval auto reflect() { return reflgen::schema<dual_schema>(reflgen::field<&dual_schema::saved>); }
    };
    struct authoring_record
    {
        inline static int authoring_calls = 0;
        int number = 1;
        authoring_record echo(authoring_record value) { return value; }
        static consteval auto reflect()
        { return reflgen::schema<authoring_record>(reflgen::field<&authoring_record::number>, reflgen::method<&authoring_record::echo>); }
    };
    struct authoring_opaque
    {
        std::function<void()> callback;
        static consteval auto reflect()
        { return reflgen::schema<authoring_opaque>(reflgen::field<&authoring_opaque::callback>); }
    };
    struct span_methods
    {
        int calls = 0;
        int sum(std::span<const script_only> values)
        { ++calls; int total = 0; for (const auto& value : values) total += value.number; return total; }
        int update(std::span<int, 2> values)
        { ++calls; values[0] += values[1]; return values[0]; }
        void unsupported(std::span<bool>) { ++calls; }
        static consteval auto reflect()
        { return reflgen::schema<span_methods>(reflgen::method<&span_methods::sum>, reflgen::method<&span_methods::update>, reflgen::method<&span_methods::unsupported>); }
    };
    struct material_parameter
    {
        std::string id = "color";
        std::variant<std::monostate, bool, std::int64_t, double,
            std::array<double, 3>, std::array<double, 4>, std::string> value;
        static consteval auto reflect()
        { return reflgen::schema<material_parameter>(reflgen::field<&material_parameter::id>, reflgen::field<&material_parameter::value>); }
    };
    struct reference_payload { leaf& mutable_value; const leaf& constant_value; };
    struct owned_snapshot
    {
        leaf item{73, "retained"};
        const leaf& get_item() const { return item; }
        static consteval auto reflect()
        { return reflgen::schema<owned_snapshot>(reflgen::field<&owned_snapshot::item>, reflgen::method<&owned_snapshot::get_item>); }
    };
    struct borrowing_destructor
    {
        inline static char last_character = '\0';
        std::string_view value;
        explicit borrowing_destructor(std::string_view text) : value(text) {}
        borrowing_destructor(borrowing_destructor&& other) noexcept : value(std::exchange(other.value, {})) {}
        ~borrowing_destructor() { if (!value.empty()) last_character = value.front(); }
    };
    struct lookup_methods
    {
        mutable int calls = 0;
        leaf native{29, "native lookup"};
        script_only entry{43, "state"};
        const leaf& find(std::string_view name) const
        { ++calls; if (name != "entry") throw std::out_of_range("entry"); return native; }
        script_only* find_pointer(std::string_view name) const
        { ++calls; if (name != "entry") return nullptr; return const_cast<script_only*>(&entry); }
        const leaf& echo(const leaf& value) const { ++calls; return value; }
        std::string_view text_view(std::string_view value) const { ++calls; return value; }
        const char* text_pointer(const char* value) const { ++calls; return value; }
        std::span<const int> numbers(std::span<const int> value) const { ++calls; return value; }
        borrowing_destructor retained_destructor(std::string_view value) const
        { ++calls; return borrowing_destructor(value); }
        leaf& mutate(std::string_view) { ++calls; return native; }
        const leaf& output(leaf& value) const { ++calls; return value; }
        const leaf& moved(leaf&& value) const { ++calls; return value; }
        const leaf& pointer_argument(leaf*) const { ++calls; return native; }
        const leaf& callback_argument(void (*)()) const { ++calls; return native; }
        const leaf& mutable_span(std::span<int>) const { ++calls; return native; }
        leaf& no_argument() { ++calls; return native; }
        const leaf& internal(std::string_view) const { ++calls; return native; }
        static consteval auto reflect()
        {
            return reflgen::schema<lookup_methods>(
                reflgen::method<&lookup_methods::find>, reflgen::method<&lookup_methods::find_pointer>,
                reflgen::method<&lookup_methods::echo>, reflgen::method<&lookup_methods::text_view>,
                reflgen::method<&lookup_methods::text_pointer>, reflgen::method<&lookup_methods::numbers>,
                reflgen::method<&lookup_methods::retained_destructor>,
                reflgen::method<&lookup_methods::mutate>, reflgen::method<&lookup_methods::output>,
                reflgen::method<&lookup_methods::moved>, reflgen::method<&lookup_methods::pointer_argument>,
                reflgen::method<&lookup_methods::callback_argument>, reflgen::method<&lookup_methods::mutable_span>,
                reflgen::method<&lookup_methods::no_argument>,
                reflgen::method<&lookup_methods::internal>.with(reflgen::script_internal{}));
        }
    };
    struct root
    {
        leaf profile;
        const leaf fixed{};
        leaf protected_value;
        std::vector<leaf> items{{}, {}};
        std::map<std::string, leaf> entries{{"one", {}}};
        std::optional<leaf> maybe{leaf{}};
        std::unique_ptr<leaf> owner = std::make_unique<leaf>();
        std::shared_ptr<leaf> shared = std::make_shared<leaf>();
        leaf* raw = nullptr;
        std::vector<bool> bits{true, false};
        std::set<int> numbers{3, 7};
        std::array<leaf, 2> fixed_items{};
        std::variant<int, leaf> choice{leaf{}};
        std::tuple<int, std::string> tuple{1, "two"};
        wrapped_string label{"label"};
        opaque custom;
        std::function<void()> callback;
        int calls = 0;

        leaf& get_profile() { return profile; }
        const leaf& get_const_profile() const { return profile; }
        std::string describe(const std::string& prefix, int count)
        { ++calls; return prefix + std::to_string(count); }
        std::vector<leaf> exchange(const std::vector<leaf>& input, leaf& output)
        { ++calls; output = input.front(); return input; }
        void reset() { ++calls; }
        void unsafe_argument(leaf*) { ++calls; }
        std::string from_view(std::string_view value) { ++calls; return std::string(value); }
        std::string from_cstring(const char* value) { ++calls; return value; }
        leaf& internal_profile() { ++calls; return profile; }
        std::shared_ptr<leaf> get_shared() const { return shared; }
        owned_snapshot snapshot() const { return {}; }
        static consteval auto reflect()
        {
            return reflgen::schema<root>(
                reflgen::field<&root::profile>, reflgen::field<&root::fixed>,
                reflgen::field<&root::protected_value>.with(reflgen::readonly{}),
                reflgen::field<&root::items>, reflgen::field<&root::entries>, reflgen::field<&root::maybe>,
                reflgen::field<&root::owner>, reflgen::field<&root::raw>, reflgen::field<&root::bits>,
                reflgen::field<&root::numbers>, reflgen::field<&root::fixed_items>, reflgen::field<&root::choice>,
                reflgen::field<&root::tuple>, reflgen::field<&root::label>, reflgen::field<&root::custom>,
                reflgen::field<&root::callback>,
                reflgen::method<&root::get_profile>, reflgen::method<&root::get_const_profile>,
                reflgen::method<&root::describe>.parameters("prefix", "count"),
                reflgen::method<&root::exchange>.parameters("input", "output"), reflgen::method<&root::reset>,
                reflgen::method<&root::unsafe_argument>, reflgen::method<&root::from_view>,
                reflgen::method<&root::from_cstring>, reflgen::method<&root::internal_profile>.with(reflgen::script_internal{}),
                reflgen::method<&root::get_shared>, reflgen::method<&root::snapshot>);
        }
    };
}

template<> struct reflgen::runtime_schema<runtime_values::script_only>
{
    static constexpr auto value = reflgen::schema<runtime_values::script_only>(
        reflgen::field<&runtime_values::script_only::number>, reflgen::field<&runtime_values::script_only::name>);
};
template<> struct reflgen::runtime_schema<runtime_values::script_owner>
{
    static constexpr auto value = reflgen::schema<runtime_values::script_owner>(
        reflgen::field<&runtime_values::script_owner::values>, reflgen::method<&runtime_values::script_owner::update>);
};
template<> struct reflgen::runtime_schema<runtime_values::dual_schema>
{
    static constexpr auto value = reflgen::schema<runtime_values::dual_schema>(
        reflgen::field<&runtime_values::dual_schema::saved>, reflgen::field<&runtime_values::dual_schema::runtime>);
};
template<> struct reflgen::runtime_schema<runtime_values::reference_payload>
{
    static constexpr auto value = reflgen::schema<runtime_values::reference_payload>(
        reflgen::accessor_field<runtime_values::reference_payload, [](auto& object) -> decltype(auto) { return (object.mutable_value); }>.named("mutable_value"),
        reflgen::accessor_field<runtime_values::reference_payload, [](auto& object) -> decltype(auto) { return (object.constant_value); }>.named("constant_value"));
};
template<> struct reflgen::runtime_value_traits<runtime_values::indexed_map>
{
    static constexpr auto shape = reflgen::value_shape::map;
    using children = reflgen::type_list<std::string, runtime_values::script_only>;
    static std::size_t size(const runtime_values::indexed_map& value) { return value.entries.size(); }
    static const runtime_values::script_only& at(const runtime_values::indexed_map& value, std::size_t index) { return value.entries.at(index).second; }
    static runtime_values::script_only& at(runtime_values::indexed_map& value, std::size_t index) { return value.entries.at(index).second; }
    static const std::string& key(const runtime_values::indexed_map& value, std::size_t index) { return value.entries.at(index).first; }
};
template<> struct reflgen::runtime_value_traits<runtime_values::opaque_map>
{
    static constexpr auto shape = reflgen::value_shape::map;
    using children = reflgen::type_list<std::string, int>;
};
template<> struct reflgen::serializer<runtime_values::authoring_record>
{
    static void write(reflgen::writer& out, const runtime_values::authoring_record& value)
    { ++runtime_values::authoring_record::authoring_calls; out.write_int(value.number); }
    static void read(reflgen::reader& in, runtime_values::authoring_record& value)
    { ++runtime_values::authoring_record::authoring_calls; value.number = static_cast<int>(in.read_int()); }
};
template<> struct reflgen::runtime_value_traits<runtime_values::authoring_record>
{ static constexpr bool prefer_record_fields = true; };
template<> struct reflgen::serializer<runtime_values::authoring_opaque>
{
    static void write(reflgen::writer& out, const runtime_values::authoring_opaque&) { out.write_null(); }
    static void read(reflgen::reader& in, runtime_values::authoring_opaque&) { in.read_null(); }
};
template<> struct reflgen::runtime_value_traits<runtime_values::authoring_opaque>
{ static constexpr bool prefer_record_fields = true; };
template<> struct reflgen::runtime_value_traits<const char*>
{
    static constexpr auto shape = reflgen::value_shape::string;
    using children = reflgen::type_list<>;
    static constexpr bool deserializable = false;
    static constexpr bool assignment_safe = false;
    static constexpr bool argument_safe = true;
    static void write(reflgen::writer& out, const char* const& value) { out.write_string(value ? value : ""); }
};
template<> struct reflgen::runtime_value_traits<runtime_values::script_only*>
{
    static constexpr auto shape = reflgen::value_shape::pointer;
    using children = reflgen::type_list<runtime_values::script_only>;
    static constexpr bool mutable_borrow = true;
    static constexpr bool retained_borrow = true;
    static constexpr bool deserializable = false;
    static constexpr bool assignment_safe = false;
    static constexpr bool argument_safe = false;
    static const runtime_values::script_only* get(runtime_values::script_only* const& value) { return value; }
    static runtime_values::script_only* get(runtime_values::script_only*& value) { return value; }
};

template<> struct reflgen::serializer<runtime_values::wrapped_string>
{
    static void write(reflgen::writer& out, const runtime_values::wrapped_string& value) { out.write_string(value.text); }
    static void read(reflgen::reader& in, runtime_values::wrapped_string& value) { value.text = in.read_string(); }
};
template<> struct reflgen::runtime_value_traits<runtime_values::wrapped_string>
{
    static constexpr auto shape = reflgen::value_shape::string;
    using children = reflgen::type_list<>;
    static constexpr bool assignment_safe = true;
};
template<> struct reflgen::serializer<runtime_values::opaque>
{
    static void write(reflgen::writer& out, const runtime_values::opaque& value) { out.write_int(value.number); }
    static void read(reflgen::reader& in, runtime_values::opaque& value) { value.number = static_cast<int>(in.read_int()); }
};
template<> struct reflgen::runtime_value_traits<std::unique_ptr<runtime_values::leaf>>
{
    static constexpr auto shape = reflgen::value_shape::pointer;
    using children = reflgen::type_list<runtime_values::leaf>;
    static constexpr bool mutable_borrow = true;
    static constexpr bool deserializable = false;
    static constexpr bool assignment_safe = false;
    static constexpr bool argument_safe = false;
    static const runtime_values::leaf* get(const std::unique_ptr<runtime_values::leaf>& value) { return value.get(); }
    static runtime_values::leaf* get(std::unique_ptr<runtime_values::leaf>& value) { return value.get(); }
};
template<> struct reflgen::runtime_value_traits<std::shared_ptr<runtime_values::leaf>>
{
    static constexpr auto shape = reflgen::value_shape::pointer;
    using children = reflgen::type_list<runtime_values::leaf>;
    static constexpr bool mutable_borrow = true;
    static constexpr bool retained_borrow = true;
    static constexpr bool deserializable = false;
    static constexpr bool assignment_safe = false;
    static constexpr bool argument_safe = false;
    static const runtime_values::leaf* get(const std::shared_ptr<runtime_values::leaf>& value) { return value.get(); }
    static runtime_values::leaf* get(std::shared_ptr<runtime_values::leaf>& value) { return value.get(); }
};

namespace
{
    using namespace reflgen_test;
    std::string text(const reflgen::value_view& value)
    {
        std::string result;
        reflgen::json::writer output(result);
        value.serialize(output);
        return result;
    }
    void assign(const reflgen::value_view& value, std::string_view input)
    {
        reflgen::json::reader reader(input);
        value.deserialize(reader);
        reader.finish();
    }
    std::string invoke(const reflgen::method_info& method, void* object, std::string_view arguments)
    {
        reflgen::json::reader input(arguments);
        std::string result;
        reflgen::json::writer output(result);
        method.invoke_serialized(object, input, output);
        input.finish();
        return result;
    }
    reflgen::value_view borrow_with_arguments(const reflgen::method_info& method, void* object, std::string_view arguments)
    {
        reflgen::json::reader input(arguments);
        auto view = method.borrow_result(object, input);
        input.finish();
        return view;
    }

    const test navigation("runtime values: records, sequences, maps and optional values preserve native identity", [] {
        runtime_values::root object;
        const auto root = reflgen::type_descriptor_of<runtime_values::root>().value().view(&object);
        assign(root.select_member("items").select_index(1).select_member("number"), "42");
        check_equal(object.items[1].number, 42);
        assign(root.select_member("entries").select_index(0).select_member("text"), R"("changed")");
        check_equal(object.entries.at("one").text, std::string("changed"));
        check_equal(text(root.select_member("entries").select_key(0)), std::string(R"("one")"));
        check(!root.select_member("entries").select_key(0).is_writable());
        assign(root.select_member("maybe").dereference().select_member("number"), "29");
        check_equal(object.maybe->number, 29);
        assign(root.select_member("fixed_items").select_index(1).select_member("number"), "31");
        check_equal(object.fixed_items[1].number, 31);
        assign(root.select_member("choice").select_index(0).select_member("number"), "47");
        check_equal(std::get<runtime_values::leaf>(object.choice).number, 47);
        assign(root.select_member("tuple").select_index(1), R"("tuple")");
        check_equal(std::get<1>(object.tuple), std::string("tuple"));
    });

    const test readonly("runtime values: readonly propagates through nested paths and map keys", [] {
        runtime_values::root object;
        const auto root = reflgen::type_descriptor_of<runtime_values::root>().value().view(&object);
        for (const auto field : {"fixed", "protected_value"})
        {
            const auto value = root.select_member(field).select_member("number");
            check(!value.is_writable());
            check_throws<std::invalid_argument>([&] { assign(value, "7"); }, "readonly");
        }
        const auto& constant = object;
        const auto read = reflgen::type_descriptor_of<runtime_values::root>().value().view(&constant);
        check(!read.select_member("items").select_index(0).select_member("number").is_writable());
        check(!read.select_member("owner").dereference().select_member("number").is_writable());
        check(!root.select_member("numbers").select_index(0).is_writable());
        check(!root.descriptor().is_assignable());
    });

    const test bounds("runtime values: absent members, empty references and invalid indices fail explicitly", [] {
        runtime_values::root object;
        const auto root = reflgen::type_descriptor_of<runtime_values::root>().value().view(&object);
        check_throws<std::out_of_range>([&] { root.select_member("missing"); }, "unknown");
        check_throws<std::out_of_range>([&] { root.select_member("items").select_index(2); }, "range");
        check_throws<std::out_of_range>([&] { root.select_member("entries").select_key(1); }, "range");
        object.maybe.reset();
        check_throws<std::out_of_range>([&] { root.select_member("maybe").dereference(); }, "empty");
        object.owner.reset();
        check_throws<std::out_of_range>([&] { root.select_member("owner").dereference(); }, "empty");
        object.raw = &object.profile;
        check_throws<std::invalid_argument>([&] { root.select_member("raw").dereference(); }, "permitted");
        check(!root.select_member("raw").descriptor().unsupported_reason().empty());
    });

    const test borrow("runtime values: explicit owner adapters borrow without replacing ownership", [] {
        runtime_values::root object;
        auto* identity = object.owner.get();
        const auto root = reflgen::type_descriptor_of<runtime_values::root>().value().view(&object);
        assign(root.select_member("owner").dereference().select_member("number"), "44");
        check(object.owner.get() == identity);
        check_equal(identity->number, 44);
        check(!root.select_member("owner").descriptor().is_deserializable());
        check(!root.select_member("owner").descriptor().is_assignable());
    });

    const test proxies("runtime values: proxy range elements are owning readonly snapshots", [] {
        runtime_values::root object;
        const auto root = reflgen::type_descriptor_of<runtime_values::root>().value().view(&object);
        auto snapshot = root.select_member("bits").select_index(0);
        object.bits.clear();
        object.bits.shrink_to_fit();
        check_equal(text(snapshot), std::string("true"));
        check(!snapshot.is_writable());
        check_throws<std::invalid_argument>([&] { assign(snapshot, "false"); }, "readonly");
    });

    const test assignment("runtime values: staged writes preserve native values on parse errors", [] {
        runtime_values::leaf object;
        const auto value = reflgen::value_descriptor_of<runtime_values::leaf>().view(&object);
        require(value.descriptor().is_assignable());
        check_throws([&] { assign(value, R"({"number":99,"text":[]})"); });
        check_equal(object.number, 3);
        check_equal(object.text, std::string("native"));
        assign(value, R"({"number":17})");
        check_equal(object.number, 17);
        check_equal(object.text, std::string("native"));
        std::vector<int> numbers{1, 2};
        check_throws([&] { assign(reflgen::value_descriptor_of<decltype(numbers)>().view(&numbers), R"([3,"bad"])"); });
        check_equal(numbers, std::vector<int>({1, 2}));
    });

    const test custom("runtime values: custom serializers keep their explicit shape and capabilities", [] {
        runtime_values::root object;
        const auto root = reflgen::type_descriptor_of<runtime_values::root>().value().view(&object);
        const auto label = root.select_member("label");
        check(label.descriptor().shape() == reflgen::value_shape::string);
        assign(label, R"("utf8 값")");
        check_equal(object.label.text, std::string("utf8 값"));
        const auto opaque = root.select_member("custom");
        check(opaque.descriptor().is_serializable());
        check(!opaque.descriptor().is_complete());
        check(!opaque.descriptor().is_assignable());
        check(!opaque.descriptor().unsupported_reason().empty());
        check(root.select_member("callback").descriptor().shape() == reflgen::value_shape::unsupported);
    });

    const test closure("runtime values: closure is recursive, deterministic and retains unsupported types", [] {
        const std::array roots{&reflgen::type_descriptor_of<runtime_values::root>(), &reflgen::type_descriptor_of<runtime_values::recursive>()};
        const auto values = reflgen::value_type_closure(roots);
        const std::array reversed{roots[1], roots[0]};
        const auto again = reflgen::value_type_closure(reversed);
        check(values == again);
        const auto contains = [&](auto id) { return std::ranges::any_of(values, [&](const auto* value) { return value->id() == id; }); };
        check(contains(reflgen::type_id_of<runtime_values::leaf>()));
        check(contains(reflgen::type_id_of<runtime_values::recursive>()));
        check(contains(reflgen::type_id_of<std::vector<runtime_values::recursive>>()));
        check(contains(reflgen::type_id_of<std::function<void()>>()));
        check(contains(reflgen::type_id_of<void>()));
        check(std::ranges::is_sorted(values, {}, [](const auto* value) { return value->name(); }));
    });

    const test methods("runtime values: serialized calls materialize strings, records, containers and ref outputs", [] {
        runtime_values::root object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::root>();
        const auto& describe = *type.find_method("describe");
        require(describe.is_serialized_invocable());
        check(describe.parameters()[0].passing() == reflgen::parameter_passing::const_reference);
        check(describe.parameters()[0].value().shape() == reflgen::value_shape::string);
        check_equal(invoke(describe, &object, R"(["prefix",7])"), std::string(R"({"result":"prefix7","arguments":["prefix",7]})"));
        const auto& exchange = *type.find_method("exchange");
        require(exchange.is_serialized_invocable());
        check(exchange.parameters()[1].passing() == reflgen::parameter_passing::mutable_reference);
        check_equal(invoke(exchange, &object, R"([[{"number":19,"text":"input"}],{}])"),
            std::string(R"({"result":[{"number":19,"text":"input"}],"arguments":[[{"number":19,"text":"input"}],{"number":19,"text":"input"}]})"));
        check_equal(invoke(*type.find_method("reset"), &object, "[]"), std::string(R"({"result":null,"arguments":[]})"));
        check_equal(object.calls, 3);
        const auto& unsafe = *type.find_method("unsafe_argument");
        check(!unsafe.is_serialized_invocable());
        check(!unsafe.serialized_unavailable_reason().empty());
    });

    const test arguments("runtime values: malformed argument arrays are rejected before native side effects", [] {
        runtime_values::root object;
        const auto& method = *reflgen::type_descriptor_of<runtime_values::root>().find_method("describe");
        for (const auto input : {R"(["prefix"])", R"(["prefix",1,2])", R"(["prefix",{}])"})
            check_throws([&] { invoke(method, &object, input); });
        check_equal(object.calls, 0);
    });

    const test live_getters("runtime values: reference getters expose live storage with const propagation", [] {
        runtime_values::root object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::root>();
        const auto& getter = *type.find_method("get_profile");
        require(getter.can_borrow_result());
        check(getter.return_passing() == reflgen::parameter_passing::mutable_reference);
        assign(getter.borrow_result(&object).select_member("number"), "55");
        check_equal(object.profile.number, 55);
        const auto& constant = *type.find_method("get_const_profile");
        require(constant.can_borrow_result());
        check(!constant.borrow_result(&object).is_writable());
        check(!constant.borrow_result(static_cast<const void*>(&object)).is_writable());
        check_throws<std::invalid_argument>([&] { getter.borrow_result(static_cast<const void*>(&object)); }, "const");
        check(!type.find_method("describe")->can_borrow_result());
    });

    const test runtime_only("runtime values: runtime schemas expose nested values without changing authoring serialization", [] {
        static_assert(!reflgen::reflectable<runtime_values::script_only>);
        static_assert(!reflgen::serializable<runtime_values::script_only>);
        static_assert(reflgen::runtime_reflectable<runtime_values::script_only>);
        runtime_values::script_owner object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::script_owner>();
        const auto view = type.value().view(&object);
        assign(view.select_member("values"), R"([{"number":5,"name":"live"}])");
        check_equal(object.values[0].number, 5);
        check_equal(text(view), std::string(R"({"values":[{"number":5,"name":"live"}]})"));
        const auto* method = type.find_method("update");
        require(method->is_serialized_invocable());
        check_equal(invoke(*method, &object, R"([{"number":3},[{"number":4}]])"),
            std::string(R"({"result":{"number":7,"name":"runtime"},"arguments":[{"number":3,"name":"runtime"},[{"number":4,"name":"runtime"}]]})"));
        reflgen::registry registry;
        registry.add<runtime_values::script_only>();
        check(registry.find(reflgen::type_id_of<runtime_values::script_only>()) != nullptr);
        runtime_values::dual_schema dual;
        check_equal(reflgen::json::to_string(dual), std::string(R"({"saved":1})"));
        check_equal(text(reflgen::type_descriptor_of<runtime_values::dual_schema>().value().view(&dual)),
            std::string(R"({"saved":1,"runtime":2})"));
    });

    const test argument_backing("runtime values: borrowed string arguments own backing only for the invocation", [] {
        runtime_values::root object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::root>();
        check_equal(invoke(*type.find_method("from_view"), &object, R"(["view 값"] )"),
            std::string(R"({"result":"view 값","arguments":["view 값"]})"));
        check_equal(invoke(*type.find_method("from_cstring"), &object, R"(["native"] )"),
            std::string(R"({"result":"native","arguments":["native"]})"));
        check(!reflgen::value_descriptor_of<std::string_view>().is_assignable());
        check(!reflgen::value_descriptor_of<const char*>().is_assignable());
    });

    const test internal_methods("runtime values: internal native methods remain visible but cannot be invoked", [] {
        runtime_values::root object;
        const auto& method = *reflgen::type_descriptor_of<runtime_values::root>().find_method("internal_profile");
        check(!method.is_invocable());
        check(!method.is_serialized_invocable());
        check(!method.can_borrow_result());
        check(method.serialized_unavailable_reason().find("script_internal") != std::string_view::npos);
        check_throws<std::invalid_argument>([&] { invoke(method, &object, "[]"); }, "script_internal");
        check_throws<std::invalid_argument>([&] { method.invoke(&object, {}); }, "script_internal");
        check_throws<std::invalid_argument>([&] { method.borrow_result(&object); }, "borrowable");
        check_equal(object.calls, 0);
    });

    const test accessors("runtime values: reference data fields preserve exact names and referent constness", [] {
        runtime_values::leaf mutable_value, constant_value;
        runtime_values::reference_payload object{mutable_value, constant_value};
        const auto& descriptor = reflgen::type_descriptor_of<runtime_values::reference_payload>();
        const auto value = descriptor.value().view(&object);
        check(!value.descriptor().is_assignable());
        assign(value.select_member("mutable_value").select_member("number"), "25");
        check_equal(mutable_value.number, 25);
        check(!value.select_member("constant_value").select_member("number").is_writable());
        check(!descriptor.value().view(static_cast<const void*>(&object)).select_member("mutable_value").is_writable());
        check(descriptor.fields()[0].declaring_type() == descriptor.id());
        check_equal(text(value), std::string(R"({"mutable_value":{"number":25,"text":"native"},"constant_value":{"number":3,"text":"native"}})"));
    });

    const test retained_results("runtime values: owning getter results retain pointee storage and preserve readonly snapshots", [] {
        runtime_values::root object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::root>();
        const auto& getter = *type.find_method("get_shared");
        require(getter.can_borrow_result());
        std::weak_ptr<runtime_values::leaf> lifetime = object.shared;
        auto wrapper = getter.borrow_result(&object);
        object.shared.reset();
        auto child = wrapper.dereference();
        wrapper = {};
        check(!lifetime.expired());
        assign(child.select_member("number"), "41");
        check_equal(lifetime.lock()->number, 41);
        child = {};
        check(lifetime.expired());

        auto snapshot = type.find_method("snapshot")->borrow_result(&object);
        check(!snapshot.is_writable());
        const auto& inner = *snapshot.descriptor().reflected_type()->find_method("get_item");
        auto nested = inner.borrow_result(snapshot.data());
        nested.retain_parent(snapshot);
        snapshot = {};
        check_equal(text(nested.select_member("number")), std::string("73"));
        check(!nested.is_writable());
    });

    const test lookup_results("runtime values: const argument lookups expose readonly native results and preserve no-argument getters", [] {
        runtime_values::lookup_methods object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::lookup_methods>();
        const auto& method = *type.find_method("find");
        require(method.can_borrow_result());
        require(method.can_borrow_result_with_arguments());
        check(method.borrow_unavailable_reason().empty());
        const auto found = borrow_with_arguments(method, &object, R"(["entry"])");
        check(found.data() == &object.native);
        check(!found.is_writable());
        check_throws<std::invalid_argument>([&] { assign(found.select_member("number"), "7"); }, "readonly");
        check_equal(object.native.number, 29);
        reflgen::json::reader constant_input(R"(["entry"])");
        check(method.borrow_result(static_cast<const void*>(&object), constant_input).data() == &object.native);
        constant_input.finish();
        check_throws<std::invalid_argument>([&] { method.borrow_result(&object); }, "borrowable");

        auto pointer = borrow_with_arguments(*type.find_method("find_pointer"), &object, R"(["entry"])");
        auto target = pointer.dereference();
        pointer = {};
        check(target.data() == &object.entry);
        check(!target.is_writable());
        check(!target.select_member("number").is_writable());
        check_equal(text(target.select_member("number")), std::string("43"));

        const auto& no_argument = *type.find_method("no_argument");
        assign(no_argument.borrow_result(&object).select_member("number"), "31");
        assign(borrow_with_arguments(no_argument, &object, "[]").select_member("number"), "37");
        check_equal(object.native.number, 37);
    });

    const test lookup_arguments("runtime values: borrowed lookups reject invalid inputs and unsafe signatures before native execution", [] {
        runtime_values::lookup_methods object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::lookup_methods>();
        const auto& method = *type.find_method("find");
        for (const auto input : {"[]", R"(["entry","extra"])", "[{}]", "[null]", "{}", R"(["entry",])"})
            check_throws([&] { borrow_with_arguments(method, &object, input); });
        for (const auto name : {"mutate", "output", "moved", "pointer_argument", "callback_argument", "mutable_span", "internal"})
        {
            const auto& rejected = *type.find_method(name);
            check(!rejected.can_borrow_result());
            check(!rejected.can_borrow_result_with_arguments());
            check(!rejected.borrow_unavailable_reason().empty());
            check_throws<std::invalid_argument>([&] { borrow_with_arguments(rejected, &object, "[null]"); });
        }
        check_throws([&] { borrow_with_arguments(*type.find_method("no_argument"), &object, "[1]"); });
        check_equal(object.calls, 0);
    });

    const test lookup_argument_lifetimes("runtime values: borrowed argument records, string views and spans retain their backing storage", [] {
        runtime_values::lookup_methods object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::lookup_methods>();
        const std::string long_text(256, 'x');
        const auto argument = "[\"" + long_text + "\"]";
        auto view = borrow_with_arguments(*type.find_method("text_view"), &object, argument);
        check_equal(text(view), "\"" + long_text + "\"");
        auto pointer = borrow_with_arguments(*type.find_method("text_pointer"), &object, argument);
        check_equal(text(pointer), "\"" + long_text + "\"");
        auto record = borrow_with_arguments(*type.find_method("echo"), &object,
            "[{\"number\":61,\"text\":\"" + long_text + "\"}]");
        auto field = record.select_member("text");
        record = {};
        check_equal(text(field), "\"" + long_text + "\"");
        check(!field.is_writable());
        auto numbers = borrow_with_arguments(*type.find_method("numbers"), &object, "[[11,23,47]]");
        auto element = numbers.select_index(2);
        numbers = {};
        check_equal(text(element), std::string("47"));
        check(!element.is_writable());
        auto destructor = borrow_with_arguments(*type.find_method("retained_destructor"), &object, argument);
        runtime_values::borrowing_destructor::last_character = '\0';
        destructor = {};
        check_equal(runtime_values::borrowing_destructor::last_character, 'x');
    });

    const test lookup_parent_lifetime("runtime values: borrowed lookups retain both temporary arguments and their parent snapshot", [] {
        auto owner = std::make_shared<runtime_values::lookup_methods>();
        std::weak_ptr<runtime_values::lookup_methods> lifetime = owner;
        const auto& type = reflgen::type_descriptor_of<runtime_values::lookup_methods>();
        auto parent = reflgen::value_view(type.value(), owner.get(), owner.get(), owner);
        auto found = borrow_with_arguments(*type.find_method("find"), owner.get(), R"(["entry"])");
        found.retain_parent(parent);
        owner.reset();
        parent = {};
        check(!lifetime.expired());
        auto number = found.select_member("number");
        found = {};
        check(!lifetime.expired());
        check_equal(text(number), std::string("29"));
        number = {};
        check(lifetime.expired());
    });

    const test indexed_adapters("runtime values: explicit container adapters navigate mapped values without STL entry conventions", [] {
        runtime_values::indexed_map object;
        const auto& descriptor = reflgen::value_descriptor_of<runtime_values::indexed_map>();
        const auto value = descriptor.view(&object);
        check(descriptor.can_select_key());
        check(descriptor.can_select_index());
        check(descriptor.is_serializable());
        check(!descriptor.is_deserializable());
        check(!descriptor.is_assignable());
        assign(value.select_index(0).select_member("number"), "15");
        check_equal(object.entries[0].second.number, 15);
        check(!value.select_key(0).is_writable());
        check_equal(text(value.select_key(0)), std::string(R"("first")"));
        check_equal(text(value), std::string(R"([["first",{"number":15,"name":"runtime"}]])"));
        check(!descriptor.view(static_cast<const void*>(&object)).select_index(0).is_writable());
        object.entries.clear();
        check_throws<std::out_of_range>([&] { value.select_index(0); }, "range");
        check_throws<std::out_of_range>([&] { value.select_key(0); }, "range");
        const auto& opaque = reflgen::value_descriptor_of<runtime_values::opaque_map>();
        check(!opaque.can_select_index());
        check(!opaque.is_serializable());
        check(!opaque.unsupported_reason().empty());
    });

    const test record_policy("runtime values: field serialization can avoid authoring hooks without hiding unsupported fields", [] {
        runtime_values::authoring_record object;
        object.authoring_calls = 0;
        const auto& type = reflgen::type_descriptor_of<runtime_values::authoring_record>();
        const auto value = type.value().view(&object);
        check_equal(text(value), std::string(R"({"number":1})"));
        assign(value, R"({"number":5})");
        check_equal(object.number, 5);
        check_equal(invoke(*type.find_method("echo"), &object, R"([{"number":8}])"),
            std::string(R"({"result":{"number":8},"arguments":[{"number":8}]})"));
        check_equal(object.authoring_calls, 0);
        std::string saved;
        reflgen::json::writer output(saved);
        reflgen::serialize(output, object);
        check_equal(saved, std::string("5"));
        check_equal(object.authoring_calls, 1);
        const auto& opaque = reflgen::value_descriptor_of<runtime_values::authoring_opaque>();
        check(!opaque.is_serializable());
        check(!opaque.is_deserializable());
    });

    const test span_arguments("runtime values: span arguments own invocation storage while persistent views cannot be replaced", [] {
        runtime_values::span_methods object;
        const auto& type = reflgen::type_descriptor_of<runtime_values::span_methods>();
        require(type.find_method("sum")->is_serialized_invocable());
        check_equal(invoke(*type.find_method("sum"), &object, R"([[{"number":2},{"number":3}]])"),
            std::string(R"({"result":5,"arguments":[[{"number":2,"name":"runtime"},{"number":3,"name":"runtime"}]]})"));
        require(type.find_method("update")->is_serialized_invocable());
        check_equal(invoke(*type.find_method("update"), &object, "[[5,4]]"),
            std::string(R"({"result":9,"arguments":[[9,4]]})"));
        check_equal(object.calls, 2);
        check_throws<reflgen::serialization_error>([&] { invoke(*type.find_method("update"), &object, "[[5]]"); }, "fixed extent");
        check_equal(object.calls, 2);
        check(!type.find_method("unsupported")->is_serialized_invocable());

        int items[2] = {1, 2};
        std::span<int> borrowed(items);
        const auto view = reflgen::value_descriptor_of<decltype(borrowed)>().view(&borrowed);
        check(!view.descriptor().is_assignable());
        check_throws<std::invalid_argument>([&] { assign(view, "[7,8]"); }, "staged");
        check_equal(items[0], 1);
        assign(view.select_index(0), "6");
        check_equal(items[0], 6);
        std::span<const int, 2> constant(items);
        check(!reflgen::value_descriptor_of<decltype(constant)>().view(&constant).select_index(0).is_writable());
    });

    const test material_variant("runtime values: material variants with monostate support safe whole-value replacement", [] {
        const auto& empty = reflgen::value_descriptor_of<std::monostate>();
        check(empty.shape() == reflgen::value_shape::void_value);
        check(empty.is_serializable());
        check(empty.is_assignable());
        runtime_values::material_parameter object;
        const auto value = reflgen::value_descriptor_of<runtime_values::material_parameter>().view(&object);
        require(value.descriptor().is_assignable());
        check_equal(text(value), std::string(R"({"id":"color","value":[0,null]})"));
        assign(value, R"({"id":"tint","value":[4,[1,2,3]]})");
        check_equal(object.id, std::string("tint"));
        check_equal(std::get<4>(object.value)[2], 3.0);
        check_throws<reflgen::serialization_error>([&] { assign(value, R"({"id":"broken","value":[0,1]})"); });
        check_equal(object.id, std::string("tint"));
        check(object.value.index() == 4);
        assign(value, R"({"value":[0,null]})");
        check(std::holds_alternative<std::monostate>(object.value));
    });

#if defined(__cpp_lib_expected)
    const test expected_values("runtime values: expected retains recursive success and error contracts", [] {
        using result = std::expected<runtime_values::script_only, std::string>;
        result value{runtime_values::script_only{8, "physics"}};
        const auto& descriptor = reflgen::value_descriptor_of<result>();
        require(descriptor.is_serializable());
        require(descriptor.is_assignable());
        check(descriptor.shape() == reflgen::value_shape::expected);
        require(descriptor.children().size() == 2);
        check_equal(descriptor.children()[0].name(), std::string_view("value"));
        check_equal(descriptor.children()[1].name(), std::string_view("error"));
        check_equal(text(descriptor.view(&value)), std::string(R"({"value":{"number":8,"name":"physics"}})"));
        assign(descriptor.view(&value), R"({"error":"bad state"})");
        require(!value.has_value());
        check_equal(value.error(), std::string("bad state"));
        check_equal(text(descriptor.view(&value).select_index(0)), std::string(R"("bad state")"));
        std::expected<void, std::string> empty;
        const auto& void_descriptor = reflgen::value_descriptor_of<decltype(empty)>();
        check_equal(text(void_descriptor.view(&empty)), std::string(R"({"value":null})"));
        check_equal(void_descriptor.view(&empty).size(), std::size_t(0));
        assign(void_descriptor.view(&empty), R"({"error":"blocked"})");
        check_equal(empty.error(), std::string("blocked"));
    });
#endif
}

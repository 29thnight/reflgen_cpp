#include "runtime_types.h"
#include "harness.h"
#include "reflgen/json.h"
#include "reflgen/runtime/registry.h"
#include <algorithm>

namespace
{
    using namespace reflgen_test;
    static_assert(!reflgen::reflectable<runtime_generated::surface>);
    static_assert(!reflgen::serializable<runtime_generated::surface>);
    static_assert(reflgen::runtime_reflectable<runtime_generated::surface>);
    static_assert(reflgen::reflectable<runtime_generated::authored>);

    std::string runtime_text(reflgen::value_view value)
    {
        std::string result;
        reflgen::json::writer out(result);
        value.serialize(out);
        return result;
    }

    const test public_surface("generated runtime: public fields, inherited values and methods are complete", [] {
        runtime_generated::surface object;
        const auto& type = reflgen::type_descriptor_of<runtime_generated::surface>();
        check_equal(type.name(), std::string_view("runtime.public_surface"));
        require(type.find_field("inherited") != nullptr);
        require(type.find_field("item") != nullptr);
        require(type.find_field("reference") != nullptr);
        require(type.find_field("readonly_reference") != nullptr);
        require(type.find_field("alias_reference") != nullptr);
        require(type.find_field("readonly_alias_reference") != nullptr);
        check(type.value().view(&object).select_member("alias_reference").mutable_data() == &object.item);
        check(!type.value().view(&object).select_member("readonly_alias_reference").is_writable());
        check(type.find_field("private_value") == nullptr);
        check(type.find_method("private_method") == nullptr);
        check(type.find_method("hidden") == nullptr);
        check(type.find_method("compile_time") == nullptr);
        check(type.find_method("unavailable") == nullptr);
        require(type.find_method("lifecycle") != nullptr);
        check(!type.find_method("lifecycle")->is_serialized_invocable());
        check_equal(runtime_text(type.value().view(&object).select_member("values").select_index(0).select_member("label")),
                    std::string("\"runtime\""));
    });

    const test public_overloads("generated runtime: overloaded public signatures compile and invoke exactly", [] {
        runtime_generated::surface object;
        const auto& type = reflgen::type_descriptor_of<runtime_generated::surface>();
        std::size_t count = 0;
        for (const auto& method : type.methods())
        {
            if (method.name() != "add") continue;
            ++count;
            require(method.is_serialized_invocable());
            const bool scalar = method.parameters()[0].type() == reflgen::type_id_of<int>();
            reflgen::json::reader input(scalar ? "[2]" : "[[2,3]]");
            std::string result;
            reflgen::json::writer output(result);
            method.invoke_serialized(&object, input, output);
            input.finish();
            check_equal(result, scalar ? std::string(R"({"result":6,"arguments":[2]})")
                                       : std::string(R"({"result":9,"arguments":[[2,3]]})"));
        }
        check_equal(count, std::size_t(2));
        check_equal(object.calls, 1);
    });

    const test authored_compatibility("generated runtime: public method opt-in preserves authored private fields", [] {
        runtime_generated::authored value;
        check_equal(reflgen::json::to_string(value), std::string(R"({"value":3,"saved_private":5})"));
        const auto& type = reflgen::type_descriptor_of<runtime_generated::authored>();
        require(type.find_method("inspect") != nullptr);
        require(type.find_method("inspect")->is_serialized_invocable());
    });

    const test inherited_overloads("generated runtime: using declarations preserve exact local and inherited overloads", [] {
        runtime_generated::imported_overload object;
        const auto& type = reflgen::type_descriptor_of<runtime_generated::imported_overload>();
        require(type.methods().size() == 2);
        for (const auto& method : type.methods())
        {
            const bool local = method.parameters()[0].type() == reflgen::type_id_of<long>();
            reflgen::json::reader input("[4]");
            std::string result;
            reflgen::json::writer output(result);
            method.invoke_serialized(&object, input, output);
            input.finish();
            check_equal(result, local ? std::string(R"({"result":6,"arguments":[4]})")
                                      : std::string(R"({"result":5,"arguments":[4]})"));
        }
    });

    const test complex_overloads("generated runtime: overloaded function-pointer and array-reference results remain valid type-ids", [] {
        runtime_generated::complex_returns object;
        const auto& type = reflgen::type_descriptor_of<runtime_generated::complex_returns>();
        require(type.methods().size() == 4);
        for (const auto& method : type.methods())
        {
            if (method.name() == "callback_for")
            {
                check(method.return_type() == reflgen::type_id_of<runtime_generated::complex_returns::callback>());
                check(!method.is_serialized_invocable());
            }
            else
            {
                check(method.is_serialized_invocable());
                reflgen::json::reader input("[1]");
                std::string result;
                reflgen::json::writer output(result);
                method.invoke_serialized(&object, input, output);
                input.finish();
                check_equal(result, method.parameters()[0].type() == reflgen::type_id_of<int>()
                    ? std::string(R"({"result":[11,12],"arguments":[1]})")
                    : std::string(R"({"result":[11,12],"arguments":[1.0]})"));
            }
        }
    });
}

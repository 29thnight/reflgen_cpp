#include "reflgen/json.h"
#include "reflgen/binary.h"
#include "reflgen/runtime/registry.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <type_traits>
#include <utility>
import reflgen.test.engine;
import reflgen.test.engine.serialization;
#include "reflgen_engine.h"
#include "serializers.h"

void require(bool result, std::source_location location = std::source_location::current())
{
    if (!result)
        throw std::runtime_error("IFC engine assertion failed at " + std::to_string(location.line()));
}

int main()
try
{
    using engine_fixture::component;
    using engine_fixture::component_base;
    static_assert(reflgen::reflectable<component>);
    static_assert(reflgen::reflectable<component::settings>);
    static_assert(!reflgen::reflectable<engine_fixture::opaque_value>);
    static_assert(!reflgen::reflectable<engine_fixture::counted>);
    static_assert(reflgen::field_count<component>() == 7);
    static_assert(reflgen::field_count<engine_fixture::legacy_public_only>() == 1);
    static_assert(reflgen::field_count<engine_fixture::explicitly_ignored_private>() == 1);

    reflgen::registry registry;
    reflgen::generated::register_engine(registry);
    const auto* type = registry.find("engine_fixture::component");
    require(type != nullptr && registry.find("engine_fixture::component::settings") != nullptr);
    require(registry.find("engine_fixture::opaque_value") == nullptr);
    require(type->bases().size() == 1);
    component original;
    require(type->bases()[0].cast(&original) == static_cast<component_base*>(&original));
    require(type->find_field("internal_count") == nullptr);
    const auto* secret = type->find_field("secret_");
    const auto* protected_field = type->find_field("protected_");
    const auto* external = type->find_field("external");
    require(secret != nullptr && protected_field != nullptr && external != nullptr);
    *static_cast<int*>(secret->address(&original)) = 99;
    *static_cast<int*>(protected_field->address(&original)) = 44;
    original.entity_id = 71;
    original.owner->entity_id = 83;
    original.external.code = 13;
    original.configuration.limit = 64;
    require(external->is_serializable() && external->is_deserializable());
    require(reflgen::enum_name(component::mode::active) == "active");

    // The descriptor used here was instantiated in the generated registration TU.
    // Missing registration headers/imports therefore fails this runtime path.
    std::string text;
    reflgen::json::writer writer(text);
    type->serialize(writer, &original);
    require(text.find("\"external\":1013") != std::string::npos);
    require(text.find("\"secret\":99") != std::string::npos);
    require(text.find("internal_count") == std::string::npos);
    component loaded;
    reflgen::json::reader reader(text);
    type->deserialize(reader, &loaded);
    require(loaded.entity_id == 71 && loaded.owner && loaded.owner->entity_id == 83);
    require(loaded.external.code == 13 && loaded.configuration.limit == 64);
    require(*static_cast<int*>(secret->address(&loaded)) == 99);
    require(*static_cast<int*>(protected_field->address(&loaded)) == 44);
    const auto binary = reflgen::binary::from_bytes<component>(reflgen::binary::to_bytes(original));
    require(binary.entity_id == 71 && binary.owner && binary.owner->entity_id == 83);
    require(binary.external.code == 13 && binary.state == component::mode::active);

    unsigned called = 0;
    unsigned picks = 0;
    unsigned inherited_choices = 0;
    unsigned own_choices = 0;
    for (const auto& method : type->methods())
    {
        int answer = 0;
        if (method.name() == "apply" && !method.is_const())
        {
            int value = 5;
            void* arguments[] = {&value};
            require(method.parameters()[0].type() == reflgen::type_id_of<int>());
            method.invoke(&original, arguments, &answer);
            require(answer == 76);
            ++called;
        }
        if (method.name() == "apply" && method.is_const())
        {
            float value = 5.0f;
            void* arguments[] = {&value};
            require(method.parameters()[0].type() == reflgen::type_id_of<float>());
            method.invoke(&original, arguments, &answer);
            require(answer == 81);
            ++called;
        }
        if (method.name() == "hidden_call")
        {
            int value = 7;
            void* arguments[] = {&value};
            method.invoke(&original, arguments, &answer);
            require(answer == 106);
            ++called;
        }
        if (method.name() == "choose")
        {
            require(method.is_const() && method.parameters().size() == 1);
            if (method.parameters()[0].type() == reflgen::type_id_of<int>())
            {
                int value = 5;
                void* arguments[] = {&value};
                method.invoke(&original, arguments, &answer);
                require(answer == 76);
                ++inherited_choices;
            }
            else
            {
                require(method.parameters()[0].type() == reflgen::type_id_of<float>());
                float value = 5.0f;
                void* arguments[] = {&value};
                method.invoke(&original, arguments, &answer);
                require(answer == 86);
                ++own_choices;
            }
        }
        if (method.name() == "pick")
        {
            method.invoke(&original, {}, &answer);
            require(answer == (method.is_const() ? 2 : 1));
            ++picks;
        }
    }
    require(called == 3 && picks == 2); // && methods are schema-only, never invoked on a void* lvalue.
    require(inherited_choices == 1 && own_choices == 1);
    bool rvalue_called = false;
    reflgen::for_each_method<component>([&](const auto& method) {
        using pointer = typename std::remove_cvref_t<decltype(method)>::pointer_type;
        if constexpr (std::is_same_v<pointer, int (component::*)() && noexcept>)
        {
            require(method.invoke(std::move(original)) == 3);
            rvalue_called = true;
        }
    });
    require(rvalue_called);
    const auto* legacy = registry.find("engine_fixture::legacy_public_only");
    require(legacy && legacy->fields().size() == 1 && legacy->methods().size() == 1);
    engine_fixture::legacy_public_only legacy_object;
    float value = 3.0f;
    void* arguments[] = {&value};
    int result = 0;
    legacy->methods()[0].invoke(&legacy_object, arguments, &result);
    require(result == 25);
    const auto* ignored = registry.find("engine_fixture::explicitly_ignored_private");
    require(ignored && ignored->fields().size() == 1 && ignored->find_field("ignored_") == nullptr);
    engine_fixture::explicitly_ignored_private ignored_object;
    const auto* visible = ignored->find_field("visible");
    require(visible != nullptr && *static_cast<int*>(visible->address(&ignored_object)) == 24);
    std::cout << "IFC_ENGINE_OK inheritance CRTP private nested overloads serializers " << text << '\n';
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}

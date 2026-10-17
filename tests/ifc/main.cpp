#include "reflgen/json.h"
#include "reflgen/binary.h"
#include "reflgen/runtime/registry.h"
#include <iostream>
#include <source_location>
#include <stdexcept>

import reflgen.test.types;
#include "reflgen_ifc_game.h"

void require(bool result, std::source_location location = std::source_location::current())
{
    if (!result)
        throw std::runtime_error("IFC reflection assertion failed at line " + std::to_string(location.line()));
}

int main()
try
{
    reflgen::registry registry;
    reflgen::generated::register_ifc_game(registry);
    require(registry.size() == 2);
    reflgen::generated::register_ifc_game(registry);
    require(registry.size() == 2);
    require(registry.find("not_exported") == nullptr);
    const auto* descriptor = registry.find("game::player");
    require(descriptor != nullptr);
    require(descriptor == &reflgen::type_descriptor_of<game::player>());
    require(descriptor->fields().size() == 5);
    require(descriptor->methods().size() == 2);
    game::player original;
    original.name = "IFC hero";
    original.location.x = 3.5f;
    const auto json = reflgen::json::to_string(original);
    require(json.find("secret_") == std::string::npos);
    require(json.find("protected_") == std::string::npos);
    auto loaded = reflgen::json::from_string<game::player>(json);
    require(loaded.name == original.name && loaded.location.x == 3.5f && loaded.kind == game::element::water);
    require(loaded.inventory == original.inventory);
    const auto bytes = reflgen::binary::to_bytes(original);
    const auto binary_loaded = reflgen::binary::from_bytes<game::player>(bytes);
    require(binary_loaded.name == original.name && binary_loaded.inventory == original.inventory);
    require(binary_loaded.location.x == original.location.x && binary_loaded.kind == original.kind);
    std::string runtime_json;
    reflgen::json::writer writer(runtime_json);
    descriptor->serialize(writer, &original);
    require(runtime_json == json);
    game::player runtime_loaded;
    reflgen::json::reader reader(runtime_json);
    descriptor->deserialize(reader, &runtime_loaded);
    require(runtime_loaded.name == original.name && runtime_loaded.location.x == original.location.x);
    require(reflgen::enum_name(game::element::water) == "water");
    int amount = 9;
    void* arguments[] = {&amount};
    for (const auto& method : descriptor->methods())
        if (method.name() == "damage")
            method.invoke(&loaded, arguments);
    require(loaded.health == 91);
    int score = 0;
    for (const auto& method : descriptor->methods())
        if (method.name() == "score")
            method.invoke(&loaded, {}, &score);
    require(score == 133);
    std::cout << "IFC_RUNTIME_JSON_OK " << json << '\n';
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}

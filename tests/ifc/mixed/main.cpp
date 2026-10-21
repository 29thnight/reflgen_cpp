#include <iostream>
#include <string>
#include "settings.h"
#include "reflgen/json.h"
import mixed.types;
#include "reflgen_mixed_types.h"
#include "registration.h"

int main()
{
    reflgen::registry registry;
    reflgen::generated::register_mixed(registry);
    reflgen::generated::register_mixed_types(registry);
    const auto header_json = reflgen::json::to_string(settings{});
    const auto module_json = reflgen::json::to_string(mixed::player{});
    const auto loaded = reflgen::json::from_string<mixed::player>(module_json);
    if (registry.size() != 2 || loaded.value != 7 || loaded.ignored != 5 || loaded.extra.value != 29 ||
        module_json.find("ignored") != std::string::npos || module_json.find("\"score\":7") == std::string::npos)
        return 1;
    const auto* descriptor = registry.find("mixed::player");
    if (!descriptor || !descriptor->find_field("extra") || !descriptor->find_field("extra")->is_serializable())
        return 2;
    mixed::player value;
    std::string runtime_json;
    reflgen::json::writer writer(runtime_json);
    descriptor->serialize(writer, &value);
    if (runtime_json.find("\"extra\":1029") == std::string::npos)
        return 3;
    std::cout << "MIXED_OK " << header_json << ' ' << module_json << '\n';
}

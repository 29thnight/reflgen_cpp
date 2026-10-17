#include <iostream>
#include "reflgen/generated.h"
import reflgen.json;
#include "reflgen_native_module.h"
static_assert(reflgen::standard_reflection_enabled);
static_assert(reflgen::field_count<native_module::actor>() == 1);
int main()
{
    reflgen::registry registry;
    reflgen::generated::register_native_module(registry);
    reflgen::generated::register_native_module();
    if (!reflgen::default_registry().find("module.actor")) return 3;
    bool rejected = false;
    try { (void)reflgen::json::from_string<native_module::actor>("{\"score\":\"invalid\"}"); }
    catch (const reflgen::serialization_error& error) { rejected = error.path() == "/score"; }
    if (!rejected) return 2;
    const auto text = reflgen::json::to_string(native_module::actor{});
    auto loaded = reflgen::json::from_string<native_module::actor>(text);
    if (registry.size() != 1 || loaded.value != 17 || text != "{\"score\":17}") return 1;
    std::cout << "STD_NAMED_JSON_OK " << text << '\n';
}

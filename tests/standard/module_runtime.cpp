#include "reflgen/json.h"
#include <iostream>
#include "reflgen_native_module.h"
static_assert(reflgen::field_count<native_module::actor>() == 1);
static_assert(!reflgen::reflectable<native_module::label>);
static_assert(std::get<0>(reflgen::schema_of<native_module::actor>.fields).attribute<native_module::label>().id == 12);
int main()
{
    reflgen::registry registry;
    reflgen::generated::register_native_module(registry);
    const auto text = reflgen::json::to_string(native_module::actor{});
    auto loaded = reflgen::json::from_string<native_module::actor>(text);
    if (registry.size() != 1 || text != "{\"score\":17}" || loaded.value != 17 || loaded.cache != 23) return 1;
    std::cout << "STD_MODULE_OK " << text << '\n';
}

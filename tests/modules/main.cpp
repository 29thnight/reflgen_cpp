#include <iostream>
#include <string>
#if defined(REFLGEN_HEADER_BEFORE_IMPORT)
#include "reflgen/generated.h"
#endif
#include <stdexcept>
#if defined(REFLGEN_IMPORT_UMBRELLA)
import reflgen;
#else
import reflgen.json;
import reflgen.binary;
import reflgen.runtime;
import reflgen.serialization;
#endif
#include "model.h"
void require(bool value)
{
    if (!value)
        throw std::runtime_error("named module assertion failed");
}
int main()
try
{
    static_assert(reflgen::reflectable<module_item>);
    static_assert(reflgen::serializable<custom_number>);
    static_assert(reflgen::schema_of<module_item>.field_count == 3);
    require(header_descriptor() == &reflgen::type_descriptor_of<module_item>());
    require(header_registry() == &reflgen::default_registry());
    reflgen::registry target;
    target.add<module_item>();
    require(target.size() == 1);
    module_item item;
    item.cache = 123;
    const auto text = reflgen::json::to_string(item);
    require(text.find("\"number\":7") != std::string::npos && text.find("cache") == std::string::npos);
    require(reflgen::json::from_string<module_item>(text).cache == 9);
    bool rejected = false;
    try { (void)reflgen::json::from_string<module_item>("{\"number\":\"invalid\"}"); }
    catch (const reflgen::serialization_error& error) { rejected = error.path() == "/number"; }
    require(rejected);
    require(reflgen::json::from_string<module_item>(text).value == 7);
    require(reflgen::binary::from_bytes<module_item>(reflgen::binary::to_bytes(item)).value == 7);
    require(reflgen::json::from_string<custom_number>(reflgen::json::to_string(custom_number{})).value == 12);
    require(reflgen::json::from_string<external_model>(reflgen::json::to_string(external_model{})).value == 21);
    require(reflgen::binary::from_bytes<custom_number>(reflgen::binary::to_bytes(custom_number{})).value == 12);
    std::cout << "NAMED_MODULES_OK " << text << '\n';
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}

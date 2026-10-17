#include <string>
#if defined(TEST_JSON)
import reflgen.json;
#elif defined(TEST_BINARY)
import reflgen.binary;
#else
import reflgen.core;
#endif
#include "model.h"
int main()
{
    static_assert(reflgen::reflectable<module_item>);
    static_assert(reflgen::field_count<module_item>() == 3);
    reflgen::registry registry;
    registry.add<module_item>();
#if defined(TEST_JSON)
    return reflgen::json::from_string<module_item>(reflgen::json::to_string(module_item{})).value == 7 ? 0 : 1;
#elif defined(TEST_BINARY)
    return reflgen::binary::from_bytes<module_item>(reflgen::binary::to_bytes(module_item{})).value == 7 ? 0 : 1;
#else
    return registry.size() == 1 ? 0 : 1;
#endif
}

#include "types.h"
#include "reflgen_standard_fixture.h"
int main()
{
    reflgen::registry registry;
    reflgen::generated::register_standard_fixture(registry);
    reflgen::generated::register_standard_fixture();
    if (!reflgen::default_registry().find("std.player")) return 2;
    registry.add<player>();
    const auto text = reflgen::json::to_string(player{});
    const auto loaded = reflgen::json::from_string<player>(text);
    auto binary = reflgen::binary::to_bytes(player{});
    auto binary_loaded = reflgen::binary::from_bytes<player>(binary);
    player object;
    int amount = 2, result = 0;
    void* args[] = {&amount};
    registry.find("std.player")->find_method("damage")->invoke(&object, args, &result);
    if (registry.size() != 1 || !registry.find("std.player") || loaded.health != 7 ||
        text != "{\"hp\":7}" || loaded.ignored != 5 || loaded.cache != 9 ||
        binary_loaded.health != 7 || object.health != 5 || result != 5)
        throw std::runtime_error("standard reflection JSON/registration mismatch");
    std::cout << "STD_REFLECTION_OK " << text << '\n';
}

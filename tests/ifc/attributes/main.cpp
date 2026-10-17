#include "reflgen/json.h"
#include "reflgen/binary.h"
#include "reflgen/runtime/registry.h"
#include <iostream>
#include <stdexcept>
#include <source_location>
import reflgen.test.annotated;
import reflgen.test.attributes;
#include "reflgen_annotated.h"

void require(bool value, std::source_location location = std::source_location::current())
{
    if (!value)
        throw std::runtime_error("module attribute assertion failed at " + std::to_string(location.line()));
}
int main()
try
{
    using namespace reflgen;
    static_assert(schema_of<annotated::actor>.field_count == 6);
    static_assert(!reflectable<annotated::excluded>);
    static_assert(std::get<0>(schema_of<annotated::actor>.fields).attribute<range<int>>().max == 100);
    registry target;
    generated::register_annotated(target);
    const auto* type = target.find("Actor");
    require(type != nullptr && target.size() == 1);
    require(target.find("annotated::excluded") == nullptr);
    for (const auto name : {"ignored", "bits", "reference", "shared_first", "shared_second", "name_ignored"})
        require(type->find_field(name) == nullptr);
    require(type->find_field("kept") != nullptr);
    require(type->attributes().find<editor::tooltip>()->value == 9);
    require(type->attributes().find<display_name>()->value == std::string_view("Actor UI"));
    require(type->fields()[0].attributes().find<required>() != nullptr);
    require(type->fields()[1].attributes().find<hidden>() != nullptr);
    require(type->fields()[1].attributes().find<readonly>() != nullptr);
    require(type->fields()[2].attributes().find<editor::tooltip>()->value == 42);
    require(type->fields()[3].attributes().find<editor::tooltip>() == nullptr);
    require(type->fields()[5].attributes().find<category>()->value == std::string_view("Active"));
    require(type->fields()[5].attributes().find<description>()->value == std::string_view("[[fake::ignore]] \"text\""));
    require(type->methods().size() == 1);
    require(type->methods()[0].attributes().find<description>()->value == std::string_view("Damage"));
    annotated::actor original{};
    original.first = 42;
    original.cache = 999;
    original.ignored = 999;
    const auto text = json::to_string(original);
    require(text.find("\"hp\":75") != std::string::npos);
    require(text.find("cache") == std::string::npos && text.find("ignored") == std::string::npos);
    const auto loaded = json::from_string<annotated::actor>(text);
    require(loaded.health == 75 && loaded.first == 42 && loaded.cache == 7);
    require(loaded.ignored == 1 && loaded.name_ignored == 3 && loaded.kept == 4);
    auto changed_ignored = original;
    changed_ignored.ignored = 123;
    changed_ignored.shared_first = 124;
    changed_ignored.shared_second = 125;
    changed_ignored.name_ignored = 126;
    require(json::to_string(changed_ignored) == text);
    require(binary::to_bytes(changed_ignored) == binary::to_bytes(original));
    json::from_string("{\"hp\":80,\"ignored\":1234,\"name_ignored\":1234}", original);
    require(original.health == 80 && original.ignored == 999 && original.name_ignored == 3);
    const auto restored = binary::from_bytes<annotated::actor>(binary::to_bytes(original));
    require(restored.health == 80 && restored.first == 42 && restored.cache == 7 && restored.ignored == 1);
    bool rejected = false;
    try
    {
        static_cast<void>(json::from_string<annotated::actor>("{}"));
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    require(rejected);
    std::cout << "IFC_ATTRIBUTES_OK " << text << '\n';
    std::cout << "IFC_IGNORE_OK type fields methods bitfield reference declarators json binary\n";
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}

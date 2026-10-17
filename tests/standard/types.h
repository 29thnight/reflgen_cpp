#pragma once
#include "reflgen/json.h"
#include "reflgen/binary.h"
#include <iostream>
#include <stdexcept>

namespace editor { struct tooltip { int id; }; }
struct [[=reflgen::reflect("std.player"), =reflgen::display_name("Player")]] player
{
    [[=reflgen::serialized_name("hp"), =reflgen::range(0, 100), =editor::tooltip{42}]] int health = 7;
    [[=reflgen::ignore{}]] unsigned bits : 3 = 1;
    [[=reflgen::ignore{}]] int ignored = 5;
    [[=reflgen::transient{}]] int cache = 9;
    [[=reflgen::reflect{}]] int damage(int amount) { return health -= amount; }
  private:
    int secret = 8;
};
enum class [[=reflgen::reflect{}]] flags { low = -2000, high = 10000, omitted [[=reflgen::ignore{}]] = 42 };
struct [[=reflgen::reflect{}, =reflgen::ignore{}]] excluded { int field; };
static_assert(!reflgen::reflectable<excluded>);
static_assert(reflgen::field_count<player>() == 2);
static_assert(reflgen::enum_entries<flags>.size() == 2);
static_assert(reflgen::enum_name(flags::high) == "high");
static_assert(std::get<0>(reflgen::schema_of<player>.fields).attribute<editor::tooltip>().id == 42);
static_assert(std::tuple_size_v<decltype(reflgen::schema_of<player>.methods)> == 1);
static_assert(std::get<0>(reflgen::schema_of<player>.methods).parameter_names[0] == "amount");

struct [[=reflgen::reflect("annotation.name")]] manual_priority
{
    int kept = 1, excluded = 2;
    static consteval auto reflect() { return reflgen::schema<manual_priority>(reflgen::field<&manual_priority::kept>).named("manual.name"); }
};
static_assert(reflgen::field_count<manual_priority>() == 1);
static_assert(reflgen::schema_of<manual_priority>.name == "manual.name");


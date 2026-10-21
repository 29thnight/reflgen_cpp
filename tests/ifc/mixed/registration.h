#pragma once
#include "reflgen/serial/serializer.h"
import mixed.types;

template<>
struct reflgen::serializer<mixed::payload>
{
    static void write(reflgen::writer& out, const mixed::payload& value) { out.write_int(value.value + 1000); }
    static void read(reflgen::reader& in, mixed::payload& value) { value.value = static_cast<int>(in.read_int()) - 1000; }
};

#pragma once
#include "reflgen/serial/serializer.h"

// This definition is deliberately absent from the provider and generated schema.
// Both the provider type and the separately imported module must be visible here.
static_assert(engine_fixture::serializer_bias == 1000);
template<>
struct reflgen::serializer<engine_fixture::opaque_value>
{
    static void write(reflgen::writer& out, const engine_fixture::opaque_value& value)
    {
        out.write_int(value.code + engine_fixture::serializer_bias);
    }
    static void read(reflgen::reader& in, engine_fixture::opaque_value& value)
    {
        value.code = static_cast<int>(in.read_int()) - engine_fixture::serializer_bias;
    }
};

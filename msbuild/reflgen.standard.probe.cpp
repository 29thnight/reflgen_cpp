#include "reflgen/generated.h"
#if !REFLGEN_USE_STD_REFLECTION
#error "C++26 reflection is unavailable"
#endif
struct [[=reflgen::reflect("probe")]] reflgen_standard_probe
{
    [[=reflgen::ignore{}]] int omitted;
    [[=reflgen::serialized_name("v")]] int value;
};
static_assert(reflgen::field_count<reflgen_standard_probe>() == 1);
static_assert(reflgen::schema_of<reflgen_standard_probe>.name == "probe");

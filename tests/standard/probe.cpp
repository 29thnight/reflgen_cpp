#include "reflgen/generated.h"
#if !REFLGEN_USE_STD_REFLECTION
#error "reflection or <meta> is unavailable"
#endif
struct [[=reflgen::reflect("probe")]] probe_type
{
    [[=reflgen::ignore{}]] int omitted;
    [[=reflgen::serialized_name("v")]] int value;
};
static_assert(reflgen::field_count<probe_type>() == 1);
static_assert(reflgen::schema_of<probe_type>.name == "probe");
int main() { return 0; }

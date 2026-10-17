module;
#include "reflgen/generated.h"
export module standard.types;
export namespace native_module
{
    struct [[=reflgen::attribute{}]] label { int id; };
    struct [[=reflgen::reflect("module.actor")]] actor
    {
        [[=reflgen::serialized_name("score"), =label{12}]] int value = 17;
        [[=reflgen::ignore{}]] int cache = 23;
    };
}

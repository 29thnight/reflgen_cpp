#include "reflgen/reflgen.h"
#include "model.h"
const reflgen::type_descriptor* header_descriptor()
{
    return &reflgen::type_descriptor_of<module_item>();
}
reflgen::registry* header_registry()
{
    return &reflgen::default_registry();
}

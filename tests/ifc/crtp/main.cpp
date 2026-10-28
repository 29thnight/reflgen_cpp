#include "reflgen/json.h"
#include "reflgen/runtime/registry.h"
#include <stdexcept>

import reflgen.test.crtp;
#include "reflgen_ifc_crtp.h"

int main()
{
    reflgen::registry registry;
    reflgen::generated::register_ifc_crtp(registry);
    const auto* type = registry.find("game::guarded_point");
    if (!type || type->bases().size() != 1 || type->fields().size() != 2) return 1;
    game::guarded_point input;
    input.x = 12;
    input.shield = 34;
    const auto output = reflgen::json::from_string<game::guarded_point>(reflgen::json::to_string(input));
    return output.x == 12 && output.shield == 34 ? 0 : 2;
}

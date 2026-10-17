#include "reflgen/json.h"
struct legacy
{
    int value = 3;
    static consteval auto reflect() { return reflgen::schema<legacy>(reflgen::field<&legacy::value>); }
};
static_assert(!reflgen::standard_reflection_enabled);
int main() { return reflgen::json::to_string(legacy{}) == "{\"value\":3}" ? 0 : 1; }

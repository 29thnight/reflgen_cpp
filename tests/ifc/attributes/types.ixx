export module reflgen.test.annotated;
// The attribute module is intentionally not re-exported. Generated code must import it explicitly.
import reflgen.test.attributes;

#define HEALTH_METADATA [[using reflgen: serialized_name("hp"), range(0, max_health), required]]
export namespace annotated
{
    struct [[reflgen::ignore]] excluded { int value = 0; };
    struct [[reflgen::reflect("Actor"), reflgen::display_name("Actor UI"), editor::tooltip(9)]] actor
    {
        static constexpr int max_health = 100;
        HEALTH_METADATA int health = 75;
        [[reflgen::transient, reflgen::hidden, reflgen::readonly]] int cache = 7;
        [[reflgen::ignore]] int ignored = 1;
        [[reflgen::ignore]] unsigned bits : 1 = 0;
        [[reflgen::ignore]] int& reference = health;
        [[reflgen::ignore]] int shared_first = 1, shared_second = 2;
        int first [[editor::tooltip(42)]], second = 2;
        int name_ignored [[reflgen::ignore]] = 3, kept = 4;
#if defined(REFLGEN_ATTRIBUTE_TEST)
        [[reflgen::category("Active"), reflgen::description(R"tag([[fake::ignore]] "text")tag")]] int active = 3;
#else
        [[reflgen::ignore]] int inactive = 3;
#endif
        [[reflgen::ignore]] void overloaded(int) {}
        [[reflgen::ignore]] void overloaded(float) {}
        [[reflgen::description("Damage")]] void damage(int amount) { health -= amount; }
    };
}

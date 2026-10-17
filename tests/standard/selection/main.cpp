#include "reflgen/json.h"
#include "reflgen/binary.h"
#include <iostream>
struct settings
{
    int value = 3;
    static consteval auto reflect() { return reflgen::schema<settings>(reflgen::field<&settings::value>); }
};
static_assert(reflgen::standard_reflection_enabled == bool(EXPECT_STANDARD));
static_assert(REFLGEN_CXX20);
static_assert(EXPECT_LANGUAGE < 23 || REFLGEN_CXX23);
static_assert(EXPECT_LANGUAGE < 26 || REFLGEN_CXX26);
static_assert(!REFLGEN_CXX26 || REFLGEN_CXX23);
static_assert(!REFLGEN_CXX23 || REFLGEN_CXX20);
static_assert(!REFLGEN_HAS_STD_REFLECTION || REFLGEN_CXX26);
static_assert(!REFLGEN_USE_STD_REFLECTION || REFLGEN_HAS_STD_REFLECTION);
constexpr auto wire = reflgen::binary::detail::little_bytes(0x0807060504030201ULL);
static_assert(wire[0] == std::byte{1} && wire[7] == std::byte{8});
int main()
{
    const auto bytes = reflgen::binary::to_bytes(1.0);
    if (bytes != std::vector<std::byte>{std::byte{5}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                                        std::byte{0}, std::byte{0}, std::byte{0xf0}, std::byte{0x3f}})
        return 2;
    if (reflgen::binary::from_bytes<double>(bytes) != 1.0)
        return 3;
    if (reflgen::binary::from_bytes<settings>(reflgen::binary::to_bytes(settings{})).value != 3)
        return 4;
    std::cout << "CPLUSPLUS=" << REFLGEN_CPLUSPLUS << " RAW=" << __cplusplus << " BYTESWAP=" << REFLGEN_HAS_BYTESWAP
              << " HAS_STD=" << REFLGEN_HAS_STD_REFLECTION << " USE_STD=" << REFLGEN_USE_STD_REFLECTION << '\n';
    return reflgen::json::to_string(settings{}) == "{\"value\":3}" ? 0 : 1;
}

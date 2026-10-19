#pragma once
#include <string>
#include <string_view>

namespace reflgen::generator
{
    // Compatibility digest only, not signing/authentication.
    std::string binding_sha256(std::string_view bytes);
} // namespace reflgen::generator

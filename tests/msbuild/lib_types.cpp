#include "lib_types.h"

namespace msbuild_lib
{
    int length_squared(const point& value)
    {
        return value.x * value.x + value.y * value.y;
    }
} // namespace msbuild_lib

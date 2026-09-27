#include "peer_types.h"
#include "reflgen/json.h"

static_assert(reflgen::reflectable<msbuild_lib::point>, "reflgen_msbuild_lib's injection did not reach this library");

std::string msbuild_peer::point_text()
{
    return reflgen::json::to_string(msbuild_lib::point{});
}

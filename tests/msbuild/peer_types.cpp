#include "peer_types.h"
#include "reflgen/json.h"

static_assert(reflgen::reflectable<msbuild_lib::point>, "reflgen_msbuild_lib's injection did not reach this library");
// 서술은 있다 — 등록 함수만 컴파일하지 않는다.
static_assert(reflgen::reflectable<msbuild_peer::handle_holder>);
static_assert(reflgen::field_count<msbuild_peer::handle_holder>() == 2);

std::string msbuild_peer::point_text()
{
    return reflgen::json::to_string(msbuild_lib::point{});
}

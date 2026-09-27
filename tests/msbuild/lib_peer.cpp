// reflgen_msbuild_lib 가 reflgen_msbuild_peer 의 header 를 include 한다 — 그쪽 서술은 ReflgenReference 로 받는다.
#include "lib_types.h"
#include "peer_types.h"
#include "reflgen/json.h"

static_assert(reflgen::reflectable<msbuild_peer::badge>, "reflgen_msbuild_peer's injection did not reach this library");

std::string msbuild_lib::badge_text()
{
    return reflgen::json::to_string(msbuild_peer::badge{});
}

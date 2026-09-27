#pragma once
// 생성 코드가 기대는 것만 모은 header. 생성기의 주입 header 가 모든 번역 단위에 강제 include 하므로 서술에 필요한
// 코어만 담는다 — 직렬화·등록소·포맷 백엔드는 쓰는 쪽이 따로 include 한다(reflgen/reflgen.h, reflgen/json.h).
#include "reflgen/core/attributes.h"
#include "reflgen/core/descriptor.h"
#include "reflgen/core/directives.h"
#include "reflgen/core/enum.h"
#include "reflgen/core/hook.h"
#include "reflgen/core/schema.h"
#include "reflgen/core/version.h"
#include <array>
#include <concepts>

namespace reflgen
{
    class registry; // 주입 header 는 등록 함수를 선언만 한다 — 등록소의 정의는 등록 함수를 정의하는 쪽이 본다.
} // namespace reflgen

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

    namespace detail
    {
        // 생성된 서술을 T 가 불완전한 자리에서 쓰면 — 흔한 자리는 다른 모듈의 반영 타입을 전방 선언만 한 필드
        // (std::shared_ptr<Material> 등)를 직렬화하는 등록 함수다 — 생성된 reflection<T> 의 static_assert 가 원인과
        // 고칠 곳을 말한다(complete_type, core/hook.h). 여기서는 서술 본문(&T::member)을 실체화하지 않아 그 뒤에
        // 이해할 수 없는 연쇄 오류가 따라 나오지 않게 한다. 그 경우의 빈 스키마는 쓰이지 않는다(컴파일이 이미 멈췄다).
        template<class T>
        consteval auto generated_description()
        {
            if constexpr (complete_type<T>)
            {
                return access::describer<T>::describe();
            }
            else
            {
                return schema<T>();
            }
        }
    } // namespace detail
} // namespace reflgen

#pragma once
// 순환 참조 감지 — 쓰기 경로 위에 지금 열려 있는 간접 참조 대상들의 주소.
//
// 값은 순환할 수 없다. 순환은 간접 참조(shared_ptr, reference_wrapper)를 거쳐서만 생긴다.
// 그래서 간접 참조를 따라 들어갈 때 대상 주소를 경로에 올리고, 이미 경로 위에 있는 대상을
// 다시 만나면 그 자리에서 실패한다. 경로를 빠져나오면 내리므로, 같은 객체를 서로 다른
// 경로에서 가리키는 공유(DAG)는 순환이 아니라서 통과한다 — 각 경로에서 값으로 적힌다.
//
// 이것이 없으면 A→B→A 를 쓰는 순간 재귀가 끝나지 않아 stack overflow 로 죽는다.
//
// 스레드마다 따로 둔다(thread_local) — 여러 스레드가 동시에 직렬화해도 서로의 경로를 보지
// 않는다. 문서 단위 상태를 writer 에 싣지 않은 이유는, 사용자가 만든 백엔드에도 검사가
// 똑같이 걸려야 하기 때문이다.
#include "reflgen/serial/error.h"
#include <memory>
#include <type_traits>
#include <vector>

namespace reflgen::detail
{
inline thread_local std::vector<const void*> active_indirections;

// 같은 객체를 기반 클래스 포인터로 보든 파생 포인터로 보든 같은 주소가 되게, 다형 타입은
// 가장 파생된 객체의 주소로 맞춘다(다중 상속에서는 기반마다 주소가 다르다).
template<class T>
const void* identity_address(const T& object) noexcept
{
    if constexpr (std::is_polymorphic_v<T>)
    {
        return dynamic_cast<const void*>(std::addressof(object));
    }
    else
    {
        return static_cast<const void*>(std::addressof(object));
    }
}

class indirection_guard
{
  public:
    explicit indirection_guard(const void* address)
    {
        for (const void* active : active_indirections)
        {
            if (active == address)
            {
                throw serialization_error("cyclic reference: the object is already being serialized on this path");
            }
        }
        active_indirections.push_back(address);
    }

    ~indirection_guard() { active_indirections.pop_back(); }

    indirection_guard(const indirection_guard&) = delete;
    indirection_guard& operator=(const indirection_guard&) = delete;
};
} // namespace reflgen::detail

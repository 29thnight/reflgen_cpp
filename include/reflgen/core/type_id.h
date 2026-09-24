#pragma once
// 타입 식별자 — 정규화된 타입 이름의 FNV-1a 64비트 해시.
//
// std::type_info 를 쓰지 않는 이유는 셋이다:
//   - RTTI 를 끈 빌드(/GR-, -fno-rtti)에서도 속성 조회가 돌아야 한다.
//   - 주소 기반 식별자(인라인 변수의 주소)는 DLL 마다 갈린다. 이름 해시는 모듈과
//     실행 회차가 달라도 같은 값이다.
//   - constexpr 이라 속성 표를 상수 초기화로 만들 수 있다.
#include "reflgen/core/name.h"
#include <compare>
#include <cstdint>
#include <functional>
#include <string_view>

namespace reflgen
{
class type_id
{
  public:
    constexpr type_id() noexcept = default;
    constexpr explicit type_id(std::uint64_t value) noexcept : value_(value) {}

    constexpr std::uint64_t value() const noexcept { return value_; }

    friend constexpr bool operator==(const type_id&, const type_id&) noexcept = default;
    friend constexpr std::strong_ordering operator<=>(const type_id&, const type_id&) noexcept = default;

  private:
    std::uint64_t value_ = 0;
};

namespace detail
{
constexpr std::uint64_t fnv1a(std::string_view text) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : text)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace detail

template<class T>
constexpr type_id type_id_of() noexcept
{
    return type_id{detail::fnv1a(type_name_of<T>())};
}
} // namespace reflgen

template<>
struct std::hash<reflgen::type_id>
{
    std::size_t operator()(const reflgen::type_id& id) const noexcept { return static_cast<std::size_t>(id.value()); }
};

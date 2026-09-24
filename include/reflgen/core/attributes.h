#pragma once
// 속성 — 기본 제공 속성과, 사용자 정의 속성을 런타임에 타입을 지워 읽는 창구.
//
// 속성은 "생성자 호출식"으로 적는다: reflgen::range(0.0f, 1.0f).
// 팩토리 함수가 아니라 타입 자체를 부르게 한 이유는 표기의 수명 때문이다 —
//   C++20/23 : [[reflgen::range(0.0f, 1.0f)]]   코드 생성기가 인자 토큰을 그대로 옮긴다
//   C++26    : [[=reflgen::range(0.0f, 1.0f)]]  주석(annotation) 값이 된다
// 두 경우 모두 같은 식이 같은 타입의 값을 만든다. 그래서 사용자 정의 속성도 생성자가
// constexpr 인 구조체 하나면 된다. 속성 타입에는 아무 제약도 걸지 않는다(리터럴
// 타입이기만 하면 schema 의 constexpr 튜플에 담긴다).
//
// C++26 주석 값은 구조적 타입이어야 해서 std::string_view 를 담을 수 없다. 그
// 백엔드가 들어올 때 문자열 속성은 std::define_static_string 기반으로 바뀐다 —
// 소비자는 .value 를 string_view 로 읽으므로 영향이 없다.
#include "reflgen/core/type_id.h"
#include <cstddef>
#include <span>
#include <string_view>

namespace reflgen
{
// 사람이 읽는 이름 (인스펙터 라벨 등).
struct display_name
{
    std::string_view value;

    constexpr explicit display_name(std::string_view text) noexcept : value(text) {}
};

// 설명문 (툴팁 등).
struct description
{
    std::string_view value;

    constexpr explicit description(std::string_view text) noexcept : value(text) {}
};

// 값의 허용 구간. 직렬화는 검사하지 않는다 — 편집기·검증기가 소비하는 표기다.
template<class T>
struct range
{
    T min;
    T max;

    constexpr range(T min_value, T max_value) noexcept : min(min_value), max(max_value) {}
};

// 직렬화 키 이름을 멤버 이름과 다르게 둔다. 멤버 이름을 바꿔도 파일 호환을
// 유지하려면 이것으로 옛 이름을 못 박는다.
struct serialized_name
{
    std::string_view value;

    constexpr explicit serialized_name(std::string_view text) noexcept : value(text) {}
};

// 직렬화에서 제외한다. 캐시·런타임 핸들처럼 저장할 이유가 없는 필드에 붙인다.
struct transient
{
};

// 역직렬화 입력에 반드시 있어야 한다. 기본은 "없으면 기존 값을 유지"다 — 필드를
// 새로 더해도 옛 파일이 읽히게 하려는 기본값이라 뒤집지 않는다.
struct required
{
};

// 편집기 표시 힌트. 직렬화와 무관하다.
struct hidden
{
};

struct readonly
{
};

// 런타임 속성 한 항목 — 타입 식별자와 정본 값의 주소. 값은 schema_of<T> 안(정적
// 저장소)에 있으므로 주소는 프로그램 수명 동안 유효하다.
class attribute_ref
{
  public:
    constexpr attribute_ref(type_id type, const void* value) noexcept : type_(type), value_(value) {}

    constexpr type_id type() const noexcept { return type_; }
    constexpr const void* data() const noexcept { return value_; }

    template<class A>
    constexpr const A* get_if() const noexcept
    {
        return type_ == type_id_of<A>() ? static_cast<const A*>(value_) : nullptr;
    }

  private:
    type_id type_;
    const void* value_;
};

// 필드·타입에 붙은 속성들의 런타임 뷰.
class attribute_list
{
  public:
    using value_type = attribute_ref;
    using iterator = std::span<const attribute_ref>::iterator;

    constexpr attribute_list() noexcept = default;
    constexpr explicit attribute_list(std::span<const attribute_ref> items) noexcept : items_(items) {}

    constexpr iterator begin() const noexcept { return items_.begin(); }
    constexpr iterator end() const noexcept { return items_.end(); }
    constexpr std::size_t size() const noexcept { return items_.size(); }
    constexpr bool empty() const noexcept { return items_.empty(); }

    // 같은 타입이 여럿 붙었으면 첫 번째를 돌려준다.
    template<class A>
    constexpr const A* find() const noexcept
    {
        for (const attribute_ref& item : items_)
        {
            if (const A* value = item.get_if<A>())
            {
                return value;
            }
        }
        return nullptr;
    }

    template<class A>
    constexpr bool contains() const noexcept
    {
        return find<A>() != nullptr;
    }

  private:
    std::span<const attribute_ref> items_;
};
} // namespace reflgen

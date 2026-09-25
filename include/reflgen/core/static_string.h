#pragma once
// 컴파일 타임 문자열 — 속성(display_name·description·serialized_name·category)과 지시어(reflect)가 담는 문자열.
//
// std::string_view 처럼 읽힌다(암시적 변환, 비교, data·size·begin·end). 다른 점은 구조적 타입이라는 것 — 멤버가
// 모두 공개된 스칼라라서 C++26 주석 값([[=reflgen::display_name("HP")]])과 C++20 클래스 NTTP 가 될 수 있다.
// std::string_view 는 멤버가 공개되지 않아 될 수 없다.
//
// 문자를 소유하지 않는다: 정적 저장소(문자열 리터럴, 정적 배열)를 가리켜야 한다. 속성은 스키마의 상수 식에서만
// 만들므로 그렇게 된다. C++26 백엔드는 std::define_static_string 으로 만든 배열을 가리키게 만든다 — 주석 값의
// 포인터는 문자열 리터럴을 가리킬 수 없기 때문이다. 읽는 쪽은 바뀌지 않는다.
#include <compare>
#include <cstddef>
#include <string_view>

namespace reflgen
{
    struct static_string
    {
        // 구조적 타입이 되려고 공개했다 — 직접 쓰지 말고 view()·data()·size() 로 읽는다.
        const char* pointer = nullptr;
        std::size_t length = 0;

        constexpr static_string() noexcept = default;
        constexpr static_string(std::string_view text) noexcept : pointer(text.data()), length(text.size()) {}

        constexpr std::string_view view() const noexcept { return {pointer, length}; }
        constexpr operator std::string_view() const noexcept { return view(); }

        constexpr const char* data() const noexcept { return pointer; }
        constexpr std::size_t size() const noexcept { return length; }
        constexpr bool empty() const noexcept { return length == 0; }
        constexpr const char* begin() const noexcept { return pointer; }
        constexpr const char* end() const noexcept { return pointer + length; }

        friend constexpr bool operator==(const static_string& left, const static_string& right) noexcept
        {
            return left.view() == right.view();
        }

        friend constexpr bool operator==(const static_string& left, std::string_view right) noexcept
        {
            return left.view() == right;
        }

        friend constexpr auto operator<=>(const static_string& left, const static_string& right) noexcept
        {
            return left.view() <=> right.view();
        }

        friend constexpr auto operator<=>(const static_string& left, std::string_view right) noexcept
        {
            return left.view() <=> right;
        }
    };
} // namespace reflgen

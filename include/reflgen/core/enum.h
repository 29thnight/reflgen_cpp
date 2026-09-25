#pragma once
// 열거형 표 — 외부 라이브러리(magic_enum) 없이 같은 원리로 자급한다.
//
// 기본은 값 범위 스캔이다: 후보 값마다 NTTP 시그니처를 찍어 이름이 붙는지 본다.
// 스캔 범위는 enum_range<E> 로 넓히거나 좁힌다(비용은 범위 폭에 비례한다).
// 범위 밖 값(예: 1 << 10 같은 비트 플래그)은 표에서 빠진다 — 직렬화는 그런 값을
// 정수로 적으므로 값이 사라지지는 않지만, 이름으로 적고 싶으면 범위를 넓히거나
// reflection<E> 특수화로 정확한 표를 공급한다(코드 생성기가 하는 일이 이것이다).
#include "reflgen/core/hook.h"
#include "reflgen/core/name.h"
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace reflgen
{
    template<class E>
    struct enum_entry
    {
        std::string_view name;
        E value;
    };

    template<class E>
    struct enum_range
    {
        static constexpr long long min = -128;
        static constexpr long long max = 128;
    };

    namespace detail
    {
        template<class E>
        concept described_enum = std::is_enum_v<E> && has_external_reflection<E>;

        template<class E, long long I>
        consteval E enum_from_integer() noexcept
        {
            return static_cast<E>(static_cast<std::underlying_type_t<E>>(I));
        }

        // 고정 기저 타입이 없는 비스코프드 열거형의 "값의 범위"는 열거자를 담는 최소 비트
        // 폭이다. 그 밖의 값을 상수 평가로 만들면 Clang 은 오류로 막는다(clang 16 이후) —
        // 스캔은 정의상 그런 값을 두드린다. 템플릿 인자 안에서의 그 실패는 치환 실패라서
        // requires 로 가려낼 수 있고, 그런 값은 정의상 열거자가 아니므로 건너뛴다.
        //
        // 이미 깨져 본 길: bit_cast 로 값을 만들어 검사를 피하는 방법은 Clang 에서 인자
        // 변환 단계에 다시 막히고, MSVC 에서는 음수 열거자의 이름을 잃는다
        // (bit_cast<gaps>(-2) → "(enum gaps)0xfffffffe"). 판별은 magic_enum 과 같은
        // integral_constant 방식이다.
        template<class E, long long I>
        concept enum_value_representable =
            requires { typename std::integral_constant<E, static_cast<E>(static_cast<std::underlying_type_t<E>>(I))>; };

        template<class E>
        consteval long long enum_scan_min() noexcept
        {
            using underlying = std::underlying_type_t<E>;
            constexpr long long requested = enum_range<E>::min;
            if constexpr (std::is_signed_v<underlying>)
            {
                constexpr long long lowest = static_cast<long long>((std::numeric_limits<underlying>::min)());
                return requested < lowest ? lowest : requested;
            }
            else
            {
                return requested < 0 ? 0 : requested;
            }
        }

        template<class E>
        consteval long long enum_scan_max() noexcept
        {
            using underlying = std::underlying_type_t<E>;
            constexpr long long requested = enum_range<E>::max;
            constexpr auto highest = static_cast<unsigned long long>((std::numeric_limits<underlying>::max)());
            if constexpr (requested >= 0 && static_cast<unsigned long long>(requested) > highest)
            {
                return static_cast<long long>(highest);
            }
            else
            {
                return requested;
            }
        }

        template<class E, long long I>
        consteval bool is_enum_value_named() noexcept
        {
            if constexpr (enum_value_representable<E, I>)
            {
                return !enumerator_name_storage<enum_from_integer<E, I>()>::raw.empty();
            }
            else
            {
                return false;
            }
        }

        template<class E, long long I>
        inline constexpr bool enum_value_named = is_enum_value_named<E, I>();

        // 스캔 범위의 값 하나 — 이름이 있으면 그 항목, 없으면 이름이 빈 항목.
        template<class E, long long I>
        consteval enum_entry<E> scanned_entry() noexcept
        {
            if constexpr (enum_value_named<E, I>)
            {
                using storage = enumerator_name_storage<enum_from_integer<E, I>()>;
                return enum_entry<E>{std::string_view{storage::buffer.data(), storage::raw.size()},
                                     enum_from_integer<E, I>()};
            }
            else
            {
                return enum_entry<E>{};
            }
        }

        // 범위 전체를 배열 초기화의 pack 확장으로 훑는다. fold 식으로 쓰면 값 수만큼 식이 중첩되어 clang 21 까지의
        // 기본 한도(256)를 넘는다 — 기본 스캔 범위 [-128, 128] 만 해도 257 값이다.
        template<class E, long long Min, std::size_t... Is>
        consteval std::array<enum_entry<E>, sizeof...(Is)> scan_values(std::index_sequence<Is...>) noexcept
        {
            return {scanned_entry<E, Min + static_cast<long long>(Is)>()...};
        }

        template<class E, long long Min, std::size_t Width>
        inline constexpr auto scanned_values = scan_values<E, Min>(std::make_index_sequence<Width>{});

        template<class E, long long Min, std::size_t Width>
        consteval std::size_t count_named_values() noexcept
        {
            std::size_t count = 0;
            for (const enum_entry<E>& entry : scanned_values<E, Min, Width>)
            {
                count += entry.name.empty() ? 0 : 1;
            }
            return count;
        }

        template<class E, long long Min, std::size_t Width, std::size_t N>
        consteval std::array<enum_entry<E>, N> collect_named_values() noexcept
        {
            std::array<enum_entry<E>, N> result{};
            std::size_t next = 0;
            for (const enum_entry<E>& entry : scanned_values<E, Min, Width>)
            {
                if (!entry.name.empty())
                {
                    result[next++] = entry;
                }
            }
            return result;
        }

        template<class E>
        consteval auto make_enum_entries() noexcept
        {
            if constexpr (described_enum<E>)
            {
                using table = std::remove_cvref_t<decltype(reflection<E>::value)>;
                static_assert(std::is_same_v<typename table::value_type, enum_entry<E>>,
                              "reflgen::reflection<Enum>::value must be a std::array<reflgen::enum_entry<Enum>, N>");
                return reflection<E>::value;
            }
            else
            {
                constexpr long long min = enum_scan_min<E>();
                constexpr long long max = enum_scan_max<E>();
                if constexpr (max < min)
                {
                    return std::array<enum_entry<E>, 0>{};
                }
                else
                {
                    constexpr std::size_t width = static_cast<std::size_t>(max - min + 1);
                    constexpr std::size_t count = count_named_values<E, min, width>();
                    return collect_named_values<E, min, width, count>();
                }
            }
        }
    } // namespace detail

    // 열거자 표. 스캔으로 만든 표는 값 오름차순이고, reflection<E> 로 공급한 표는
    // 공급한 순서 그대로다. 타입당 하나의 정본(inline 변수 템플릿)이다.
    template<class E>
        requires std::is_enum_v<E>
    inline constexpr auto enum_entries = detail::make_enum_entries<E>();

    template<class E>
        requires std::is_enum_v<E>
    constexpr std::string_view enum_name(E value) noexcept
    {
        for (const enum_entry<E>& entry : enum_entries<E>)
        {
            if (entry.value == value)
            {
                return entry.name;
            }
        }
        return {};
    }

    template<class E>
        requires std::is_enum_v<E>
    constexpr std::optional<E> enum_cast(std::string_view name) noexcept
    {
        for (const enum_entry<E>& entry : enum_entries<E>)
        {
            if (entry.name == name)
            {
                return entry.value;
            }
        }
        return std::nullopt;
    }
} // namespace reflgen

#pragma once
// 런타임 열거형 서술자 — 열거자 표(enum_entries<E>)를 타입을 지운 표로 물질화한 것.
//
// 편집기의 콤보 상자나 콘솔의 "이름으로 값 넣기"처럼 열거형을 런타임 값으로만 아는 소비자가 쓴다. enum 필드는
// field_info::enumeration() 으로 이 서술자를 준다. 값은 long long 으로 오간다 — 기반 타입의 폭은 서술자가 알고
// read·write 가 맞춰 읽고 쓴다.
#include "reflgen/core/enum.h"
#include "reflgen/core/name.h"
#include "reflgen/core/type_id.h"
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

namespace reflgen
{
    class enum_descriptor
    {
      public:
        struct entry
        {
            std::string_view name;
            long long value;
        };

        std::string_view name() const noexcept { return name_; }
        type_id id() const noexcept { return id_; }
        // 열거자 표 — 스캔한 표는 값 오름차순, reflection<E> 로 공급한 표는 그 순서다.
        std::span<const entry> entries() const noexcept { return entries_; }

        const entry* find(std::string_view entry_name) const noexcept
        {
            for (const entry& candidate : entries_)
            {
                if (candidate.name == entry_name)
                {
                    return &candidate;
                }
            }
            return nullptr;
        }

        const entry* find(long long value) const noexcept
        {
            for (const entry& candidate : entries_)
            {
                if (candidate.value == value)
                {
                    return &candidate;
                }
            }
            return nullptr;
        }

        // value 는 이 열거형 객체를 가리킨다(field_info::address 가 준 주소 등).
        long long read(const void* value) const noexcept { return read_(value); }
        void write(void* value, long long number) const noexcept { write_(value, number); }

        constexpr enum_descriptor(std::string_view name, type_id id, std::span<const entry> entries,
                                  long long (*read)(const void*) noexcept,
                                  void (*write)(void*, long long) noexcept) noexcept
            : name_(name), id_(id), entries_(entries), read_(read), write_(write)
        {
        }

      private:
        std::string_view name_;
        type_id id_;
        std::span<const entry> entries_;
        long long (*read_)(const void*) noexcept;
        void (*write_)(void*, long long) noexcept;
    };

    namespace detail
    {
        template<class E>
        long long read_enum_value(const void* value) noexcept
        {
            return static_cast<long long>(static_cast<std::underlying_type_t<E>>(*static_cast<const E*>(value)));
        }

        template<class E>
        void write_enum_value(void* value, long long number) noexcept
        {
            *static_cast<E*>(value) = static_cast<E>(static_cast<std::underlying_type_t<E>>(number));
        }

        template<class E>
        inline constexpr auto runtime_enum_entries = []<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::array<enum_descriptor::entry, sizeof...(Is)>{enum_descriptor::entry{
                enum_entries<E>[Is].name,
                static_cast<long long>(static_cast<std::underlying_type_t<E>>(enum_entries<E>[Is].value))}...};
        }(std::make_index_sequence<enum_entries<E>.size()>{});

        template<class E>
        inline constexpr enum_descriptor runtime_enum{type_name_of<E>(), type_id_of<E>(),
                                                      std::span<const enum_descriptor::entry>(runtime_enum_entries<E>),
                                                      &read_enum_value<E>, &write_enum_value<E>};
    } // namespace detail

    template<class E>
        requires std::is_enum_v<E>
    constexpr const enum_descriptor& enum_descriptor_of() noexcept
    {
        return detail::runtime_enum<E>;
    }
} // namespace reflgen

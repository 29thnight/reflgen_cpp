#pragma once
// STL 모양 컨테이너의 기본 container_traits. 사용자 특수화와 같은 규약을 따른다 —
// 직렬화 코드는 둘을 구별하지 않는다(container_traits_of).
#include "reflgen/serial/container_traits.h"
#include "reflgen/serial/detail/shape.h"
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace reflgen::detail
{
// 정의가 없으면 "읽기 규약을 모르는 범위"다(뷰 등). 쓰기는 범위 모양으로 된다.
template<class C>
struct default_container_traits;

template<class C>
    requires map_like<C> && requires(C& map) { map.clear(); }
struct default_container_traits<C>
{
    using key_type = typename C::key_type;
    using mapped_type = typename C::mapped_type;

    static constexpr container_kind kind = container_kind::map;
    static constexpr bool unique_keys = unique_map<C>;

    static std::size_t size(const C& map) { return range_size(map); }

    template<class F>
    static void for_each(const C& map, F&& function)
    {
        for (const auto& entry : map)
        {
            function(entry.first, entry.second);
        }
    }

    static void clear(C& map) { map.clear(); }

    // 중복 키 입력은 마지막 값이 이긴다(JSON 파서들의 관례). insert_or_assign 이 없는
    // 컨테이너(multimap 등)는 emplace — multimap 이면 전부 남는다.
    static void add(C& map, key_type&& key, mapped_type&& mapped)
    {
        if constexpr (requires { map.insert_or_assign(std::move(key), std::move(mapped)); })
        {
            map.insert_or_assign(std::move(key), std::move(mapped));
        }
        else
        {
            map.emplace(std::move(key), std::move(mapped));
        }
    }
};

template<class C>
    requires(!map_like<C> && growable_range<C>)
struct default_container_traits<C>
{
    using value_type = std::ranges::range_value_t<C>;

    static constexpr container_kind kind = container_kind::sequence;

    static std::size_t size(const C& range) { return range_size(range); }

    // std::vector<bool> 처럼 참조가 대리 객체인 컨테이너는 값으로 바꿔 넘긴다 —
    // 규약이 const value_type& 이다.
    template<class F>
    static void for_each(const C& range, F&& function)
    {
        for (auto&& element : range)
        {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(element)>, value_type>)
            {
                function(element);
            }
            else
            {
                function(static_cast<value_type>(element));
            }
        }
    }

    static void clear(C& range) { range.clear(); }

    static void reserve(C& range, std::size_t count)
    {
        if constexpr (requires { range.reserve(count); })
        {
            range.reserve(count);
        }
    }

    // 원소를 뒤에 붙이는 호출체. forward_list 는 꼬리 반복자를 들고 있어야 순서를
    // 지키며 O(1) 로 붙일 수 있어서, add 대신 상태를 가진 inserter 를 둔다.
    static auto inserter(C& range)
    {
        if constexpr (emplace_back_range<C>)
        {
            return [&range](value_type&& item) { range.emplace_back(std::move(item)); };
        }
        else if constexpr (push_back_range<C>)
        {
            return [&range](value_type&& item) { range.push_back(std::move(item)); };
        }
        else if constexpr (value_insert_range<C>)
        {
            return [&range](value_type&& item) { range.insert(std::move(item)); };
        }
        else if constexpr (insert_after_range<C>)
        {
            return [&range, tail = range.before_begin()](value_type&& item) mutable {
                tail = range.insert_after(tail, std::move(item));
            };
        }
        else
        {
            return [&range](value_type&& item) { range.insert(range.end(), std::move(item)); };
        }
    }
};

template<class C>
    requires(!map_like<C> && !growable_range<C> && resizable_range<C>)
struct default_container_traits<C>
{
    using value_type = std::ranges::range_value_t<C>;

    static constexpr container_kind kind = container_kind::sequence;

    static std::size_t size(const C& range) { return range_size(range); }

    template<class F>
    static void for_each(const C& range, F&& function)
    {
        for (const auto& element : range)
        {
            function(element);
        }
    }

    static void assign(C& range, std::vector<value_type>&& items)
    {
        range.resize(items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            range[i] = std::move(items[i]);
        }
    }
};

template<class C>
concept has_user_container_traits = requires { container_traits<C>::kind; };

template<class C>
concept has_default_container_traits = requires { default_container_traits<C>::kind; };

// 사용자 특수화가 있으면 그것, 없으면 기본.
template<class C>
using container_traits_of =
    std::conditional_t<has_user_container_traits<C>, container_traits<C>, default_container_traits<C>>;

template<class Traits, class C>
concept traits_with_inserter = requires(C& container) { Traits::inserter(container); };

template<class Traits, class C>
concept traits_with_add = requires(C& container, typename Traits::value_type item) {
    Traits::clear(container);
    Traits::add(container, std::move(item));
};

template<class Traits, class C>
concept traits_with_assign = requires(C& container, std::vector<typename Traits::value_type> items) {
    Traits::assign(container, std::move(items));
};

template<class Traits, class C>
concept traits_with_reserve = requires(C& container) { Traits::reserve(container, std::size_t{}); };
} // namespace reflgen::detail

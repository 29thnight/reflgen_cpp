#pragma once
// 모양 판정 — 타입이 어떤 인터페이스를 가졌는가. 이름이 아니라 모양으로 가르는 것이
// 원칙이다: 사용자·서드파티 컨테이너가 STL 과 같은 모양이면 같은 길을 탄다.
// 이름으로 가르는 것은 모양만으로 뜻이 갈리지 않는 표준 타입(optional, variant,
// chrono, path 등)뿐이다.
#include <bitset>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <memory>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace reflgen::detail
{
    template<class T, template<class...> class Template>
    inline constexpr bool is_specialization_of_v = false;

    template<template<class...> class Template, class... Args>
    inline constexpr bool is_specialization_of_v<Template<Args...>, Template> = true;

    template<class T>
    inline constexpr bool is_bitset_v = false;

    template<std::size_t N>
    inline constexpr bool is_bitset_v<std::bitset<N>> = true;

    template<class T>
    inline constexpr bool is_duration_v = false;

    template<class Rep, class Period>
    inline constexpr bool is_duration_v<std::chrono::duration<Rep, Period>> = true;

    template<class T>
    inline constexpr bool is_time_point_v = false;

    template<class Clock, class Duration>
    inline constexpr bool is_time_point_v<std::chrono::time_point<Clock, Duration>> = true;

    template<class T>
    concept character = std::is_same_v<T, char> || std::is_same_v<T, wchar_t> || std::is_same_v<T, char8_t> ||
                        std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

    template<class T>
    concept c_string = std::is_pointer_v<T> && character<std::remove_cv_t<std::remove_pointer_t<T>>>;

    template<class T>
    concept char_array = std::is_array_v<T> && std::rank_v<T> == 1 && (std::extent_v<T> > 0) &&
                         character<std::remove_cv_t<std::remove_extent_t<T>>>;

    template<class T>
    concept smart_pointer = is_specialization_of_v<T, std::unique_ptr> || is_specialization_of_v<T, std::shared_ptr>;

    // std::stack · std::queue · std::priority_queue 와 같은 모양.
    template<class T>
    concept container_adapter = requires {
        typename T::container_type;
        typename T::value_type;
    } && !std::ranges::range<T> && requires(T& adapter, typename T::value_type value) {
        adapter.push(std::move(value));
        adapter.pop();
        adapter.empty();
    };

    template<class T>
    concept map_like = std::ranges::input_range<const T> && requires {
        typename T::key_type;
        typename T::mapped_type;
    } && requires(const std::ranges::range_value_t<const T>& entry) {
        entry.first;
        entry.second;
    };

    // 키가 유일한 맵만 객체 모양으로 적는다 — multimap 을 객체로 적으면 중복 키가 생긴다.
    // insert 가 pair<iterator, bool> 을 돌려주는 것이 유일 키 컨테이너의 표지다.
    template<class T>
    concept unique_map = map_like<T> && requires(T& map, typename T::value_type entry) {
        { map.insert(std::move(entry)).second } -> std::convertible_to<bool>;
    };

    template<class T>
    concept byte_blob = std::ranges::contiguous_range<const T> && std::ranges::sized_range<const T> &&
                        std::is_same_v<std::remove_cv_t<std::ranges::range_value_t<const T>>, std::byte>;

    template<class T>
    concept tuple_like = requires { std::tuple_size<T>::value; };

    // 문자열 키로 적을 수 있는 키 타입.
    template<class T>
    concept string_key = is_specialization_of_v<T, std::basic_string> && std::is_same_v<typename T::value_type, char>;

    template<class T>
    concept key_codec = string_key<T> || (std::integral<T> && !character<T>) || std::is_enum_v<T>;

    template<class R>
    concept emplace_back_range =
        requires(R& range, std::ranges::range_value_t<R> item) { range.emplace_back(std::move(item)); };

    template<class R>
    concept push_back_range =
        requires(R& range, std::ranges::range_value_t<R> item) { range.push_back(std::move(item)); };

    template<class R>
    concept value_insert_range =
        requires(R& range, std::ranges::range_value_t<R> item) { range.insert(std::move(item)); };

    template<class R>
    concept insert_after_range = requires(R& range, std::ranges::range_value_t<R> item) {
        range.insert_after(range.before_begin(), std::move(item));
    };

    template<class R>
    concept end_insert_range =
        requires(R& range, std::ranges::range_value_t<R> item) { range.insert(range.end(), std::move(item)); };

    // 비우고 하나씩 더해 채우는 컨테이너.
    template<class R>
    concept growable_range = std::ranges::input_range<R> && requires(R& range) { range.clear(); } &&
                             (emplace_back_range<R> || push_back_range<R> || value_insert_range<R> ||
                              insert_after_range<R> || end_insert_range<R>);

    // std::valarray — clear 가 없고 resize 와 첨자가 있다.
    template<class R>
    concept resizable_range =
        std::ranges::input_range<R> && !requires(R& range) { range.clear(); } && requires(R& range) {
            range.resize(std::size_t{});
            range[std::size_t{}];
        };

    // std::array · C 배열 · std::span — 크기가 바뀌지 않는다. 입력의 원소 수가 정확히 맞아야 한다.
    template<class R>
    concept fixed_size_range = std::ranges::sized_range<R> && std::ranges::input_range<R> && !requires(R& range) {
        range.clear();
    } && !requires(R& range) { range.resize(std::size_t{}); };

    template<class R>
    std::size_t range_size(const R& range)
    {
        if constexpr (std::ranges::sized_range<const R>)
        {
            return static_cast<std::size_t>(std::ranges::size(range));
        }
        else
        {
            return static_cast<std::size_t>(std::ranges::distance(range));
        }
    }
} // namespace reflgen::detail

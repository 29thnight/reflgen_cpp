#pragma once
// 컨테이너 확장 지점.
//
// STL 과 같은 모양의 인터페이스를 가진 컨테이너는 특수화 없이 자동으로 잡힌다:
//   - 맵:     key_type · mapped_type · 반복 시 .first/.second · clear()
//             (insert_or_assign 이 있으면 마지막 키가 이긴다)
//   - 시퀀스: 입력 범위 · clear() · 그리고 emplace_back / push_back / insert(value) /
//             insert_after / insert(end, value) 중 하나
//   - 크기 고정 범위(std::array, C 배열, std::span) · std::valarray · 컨테이너 어댑터
// EASTL·Abseil·Boost.Container·plf::colony 같은 라이브러리의 컨테이너가 대개
// 여기에 걸린다.
//
// 인터페이스가 다른 컨테이너는 container_traits 를 특수화한다. 필요한 것만 채운다:
//
//   template<class T>
//   struct reflgen::container_traits<ring_buffer<T>>
//   {
//       using value_type = T;
//       static constexpr reflgen::container_kind kind = reflgen::container_kind::sequence;
//
//       static std::size_t size(const ring_buffer<T>& c);
//       template<class F>
//       static void for_each(const ring_buffer<T>& c, F&& f);   // f(const value_type&)
//
//       // 읽기 — 둘 중 하나:
//       static void clear(ring_buffer<T>& c);                    // ① 비우고 하나씩 더한다
//       static void add(ring_buffer<T>& c, value_type&& item);
//       static void reserve(ring_buffer<T>& c, std::size_t n);   //    (선택)
//       static void assign(ring_buffer<T>& c, std::vector<value_type>&& items);  // ② 한 번에 갈아 끼운다
//   };
//
//   맵이면 kind = container_kind::map 에 key_type · mapped_type · unique_keys 를 두고
//   for_each 는 f(const key_type&, const mapped_type&), add 는 add(c, key&&, mapped&&) 다.
//   unique_keys 가 true 이고 키가 문자열·정수·열거형이면 JSON 객체 모양으로 적힌다.
//
// 이것으로도 안 되는 타입(원소를 흩어 담는 구조 등)은 serializer<T> 를 직접 특수화한다.
namespace reflgen
{
    enum class container_kind : unsigned char
    {
        sequence,
        map,
    };

    template<class C>
    struct container_traits;
} // namespace reflgen

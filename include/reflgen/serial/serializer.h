#pragma once
// 직렬화 진입점.
//
//   reflgen::serialize(writer, value);        // 어떤 포맷이든 writer 구현이 정한다
//   reflgen::deserialize(reader, value);      // 제자리 읽기 — 입력에 없는 필드는 유지
//   auto value = reflgen::deserialize<T>(reader);
//
// 지원 범위(범주 판정은 detail/traits.h 의 category_of):
//   스칼라      bool · 문자 · 정수 · 실수 · 열거형 · std::byte · nullptr_t · monostate
//   문자열      basic_string(전 문자 타입) · string_view/C 문자열(쓰기) · 문자 배열
//   값 래퍼     optional · unique_ptr · shared_ptr(다형 포함) · atomic · reference_wrapper
//   합·곱 타입  variant · pair · tuple · 튜플 모양 사용자 타입 · complex
//   컨테이너    vector · deque · list · forward_list · (unordered_)(multi)set/map ·
//               array · C 배열 · span · valarray · stack · queue · priority_queue ·
//               basic_string · flat_map/flat_set(C++23, 모양으로 잡힌다) · 사용자 컨테이너
//   기타        bitset · chrono::duration/time_point · filesystem::path
//   서술된 클래스, 그리고 serializer<T> 를 특수화한 모든 타입
#include "reflgen/serial/container_traits.h"
#include "reflgen/serial/detail/container.h"
#include "reflgen/serial/detail/object.h"
#include "reflgen/serial/detail/scalar.h"
#include "reflgen/serial/detail/text.h"
#include "reflgen/serial/detail/traits.h"
#include "reflgen/serial/detail/utility.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/fwd.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/writer.h"
#include <concepts>
#include <type_traits>

namespace reflgen
{
namespace detail
{
// 정의는 pointer.h — 다형 포인터가 런타임 등록소를 필요로 해서 뒤로 뺐다.
template<class Pointer>
void write_pointer(writer& out, const Pointer& value);

template<class Pointer>
void read_pointer(reader& in, Pointer& value);
} // namespace detail

template<class T>
struct serializer
{
    // 기본 구현의 표지. 사용자 특수화에는 없다 — 그것으로 사용자 특수화를 알아본다.
    using primary_template = void;

    static void write(writer& out, const T& value)
    {
        using category = detail::value_category;
        constexpr category kind = detail::category_of<T>();

        if constexpr (kind == category::boolean)
        {
            out.write_bool(value);
        }
        else if constexpr (kind == category::character)
        {
            detail::write_character(out, value);
        }
        else if constexpr (kind == category::byte)
        {
            out.write_uint(static_cast<unsigned char>(value));
        }
        else if constexpr (kind == category::integer)
        {
            detail::write_integer(out, value);
        }
        else if constexpr (kind == category::floating)
        {
            out.write_float(static_cast<double>(value));
        }
        else if constexpr (kind == category::enumeration)
        {
            detail::write_enum(out, value);
        }
        else if constexpr (kind == category::null)
        {
            out.write_null();
        }
        else if constexpr (kind == category::object)
        {
            detail::write_object(out, value);
        }
        else if constexpr (kind == category::string)
        {
            detail::write_string(out, value);
        }
        else if constexpr (kind == category::string_view)
        {
            detail::write_text(out, value);
        }
        else if constexpr (kind == category::c_string)
        {
            detail::write_c_string(out, value);
        }
        else if constexpr (kind == category::char_array)
        {
            detail::write_char_array(out, value);
        }
        else if constexpr (kind == category::optional)
        {
            detail::write_optional(out, value);
        }
        else if constexpr (kind == category::pointer)
        {
            detail::write_pointer(out, value);
        }
        else if constexpr (kind == category::variant)
        {
            detail::write_variant(out, value);
        }
        else if constexpr (kind == category::complex)
        {
            detail::write_complex(out, value);
        }
        else if constexpr (kind == category::bitset)
        {
            detail::write_bitset(out, value);
        }
        else if constexpr (kind == category::duration)
        {
            detail::write_duration(out, value);
        }
        else if constexpr (kind == category::time_point)
        {
            detail::write_time_point(out, value);
        }
        else if constexpr (kind == category::path)
        {
            detail::write_path(out, value);
        }
        else if constexpr (kind == category::adapter)
        {
            detail::write_adapter(out, value);
        }
        else if constexpr (kind == category::atomic)
        {
            detail::write_atomic(out, value);
        }
        else if constexpr (kind == category::reference)
        {
            serialize(out, value.get());
        }
        else if constexpr (kind == category::bytes)
        {
            detail::write_bytes(out, value);
        }
        else if constexpr (kind == category::container)
        {
            detail::write_container(out, value);
        }
        else if constexpr (kind == category::fixed_range || kind == category::range)
        {
            detail::write_range(out, value);
        }
        else if constexpr (kind == category::tuple)
        {
            detail::write_tuple(out, value);
        }
        else
        {
            static_assert(detail::dependent_false<T>,
                          "reflgen: this type is not serializable; describe it (reflect() or reflgen::reflection<T>), "
                          "specialize reflgen::container_traits<T> for a container, or specialize "
                          "reflgen::serializer<T>");
        }
    }

    static void read(reader& in, T& value)
    {
        using category = detail::value_category;
        constexpr category kind = detail::category_of<T>();

        if constexpr (kind == category::boolean)
        {
            value = in.read_bool();
        }
        else if constexpr (kind == category::character)
        {
            value = detail::read_character<T>(in);
        }
        else if constexpr (kind == category::byte)
        {
            value = static_cast<T>(detail::read_integer<unsigned char>(in));
        }
        else if constexpr (kind == category::integer)
        {
            value = detail::read_integer<T>(in);
        }
        else if constexpr (kind == category::floating)
        {
            value = detail::read_floating<T>(in);
        }
        else if constexpr (kind == category::enumeration)
        {
            value = detail::read_enum<T>(in);
        }
        else if constexpr (kind == category::null)
        {
            in.read_null();
            value = T{};
        }
        else if constexpr (kind == category::object)
        {
            detail::read_object(in, value);
        }
        else if constexpr (kind == category::string)
        {
            detail::read_string(in, value);
        }
        else if constexpr (kind == category::string_view || kind == category::c_string || kind == category::range)
        {
            static_assert(detail::dependent_false<T>,
                          "reflgen: this type does not own its contents and cannot be deserialized into "
                          "(string_view, C string, view); read into an owning type instead");
        }
        else if constexpr (kind == category::char_array)
        {
            detail::read_char_array(in, value);
        }
        else if constexpr (kind == category::optional)
        {
            detail::read_optional(in, value);
        }
        else if constexpr (kind == category::pointer)
        {
            detail::read_pointer(in, value);
        }
        else if constexpr (kind == category::variant)
        {
            detail::read_variant(in, value);
        }
        else if constexpr (kind == category::complex)
        {
            detail::read_complex(in, value);
        }
        else if constexpr (kind == category::bitset)
        {
            detail::read_bitset(in, value);
        }
        else if constexpr (kind == category::duration)
        {
            detail::read_duration(in, value);
        }
        else if constexpr (kind == category::time_point)
        {
            detail::read_time_point(in, value);
        }
        else if constexpr (kind == category::path)
        {
            detail::read_path(in, value);
        }
        else if constexpr (kind == category::adapter)
        {
            detail::read_adapter(in, value);
        }
        else if constexpr (kind == category::atomic)
        {
            detail::read_atomic(in, value);
        }
        else if constexpr (kind == category::reference)
        {
            deserialize(in, value.get());
        }
        else if constexpr (kind == category::bytes)
        {
            detail::read_bytes(in, value);
        }
        else if constexpr (kind == category::container)
        {
            detail::read_container(in, value);
        }
        else if constexpr (kind == category::fixed_range)
        {
            detail::read_fixed_range(in, value);
        }
        else if constexpr (kind == category::tuple)
        {
            detail::read_tuple(in, value);
        }
        else
        {
            static_assert(detail::dependent_false<T>,
                          "reflgen: this type is not deserializable; describe it (reflect() or "
                          "reflgen::reflection<T>), specialize reflgen::container_traits<T> for a container, or "
                          "specialize reflgen::serializer<T>");
        }
    }
};

template<class T>
void serialize(writer& out, const T& value)
{
    serializer<std::remove_cv_t<T>>::write(out, value);
}

template<class T>
void deserialize(reader& in, T& value)
{
    static_assert(!std::is_const_v<T>, "reflgen::deserialize: cannot read into a const object");
    serializer<T>::read(in, value);
}

template<class T>
    requires std::default_initializable<T>
T deserialize(reader& in)
{
    T value{};
    deserialize(in, value);
    return value;
}
} // namespace reflgen

// 꼬리 include — 순서의 이유는 runtime/type_descriptor.h 끝의 설명을 볼 것.
#include "reflgen/serial/detail/pointer.h"

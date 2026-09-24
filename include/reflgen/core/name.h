#pragma once
// 컴파일타임 이름 추출 — 타입 이름, 멤버 이름, 열거자 이름.
//
// std::source_location 이 돌려주는 함수 시그니처에서 템플릿 인자 구간을 잘라낸다.
// 표기는 컴파일러마다 다르다 (VS 18 / clang 22 실측, GCC 는 문서 기준):
//
//   MSVC  : "... __cdecl reflgen::detail::type_signature<double>(void)"
//   Clang : "... reflgen::detail::type_signature(void) [T = double]"
//   GCC   : "... reflgen::detail::type_signature() [with T = double; ...]"
//
// 컴파일러별 분기(전처리기)를 두지 않으려고, 이미 아는 인자(double, nullptr)를 넣은
// 시그니처에서 앞뒤 고정부의 길이를 재고, 그 틀을 모든 인자에 그대로 적용한다.
// 틀을 못 찾는 구현(함수 이름만 주는 표준 라이브러리)에서는 추출이 빈 문자열을
// 돌려준다. 이름이 실제로 필요한 자리(reflgen::schema)가 그때 명시 이름(.named)을
// 요구한다 — 이름을 쓰지 않는 사용자까지 컴파일이 깨지지 않게 하려는 것이다.
#include <array>
#include <cstddef>
#include <source_location>
#include <string_view>

namespace reflgen
{
namespace detail
{
template<class T>
consteval std::string_view type_signature() noexcept
{
    return std::source_location::current().function_name();
}

template<auto V>
consteval std::string_view value_signature() noexcept
{
    return std::source_location::current().function_name();
}

struct signature_frame
{
    std::size_t prefix = 0;
    std::size_t suffix = 0;
    bool valid = false;
};

constexpr signature_frame make_signature_frame(std::string_view signature, std::string_view probe) noexcept
{
    const std::size_t position = signature.find(probe);
    if (position == std::string_view::npos)
    {
        return {};
    }
    return {position, signature.size() - position - probe.size(), true};
}

// 탐침 인자는 시그니처의 다른 곳에 우연히 나타나지 않고, 컴파일러마다 철자가 같아야
// 한다. 정수는 안 된다 — MSVC 는 정수 NTTP 를 16진으로 적는다(12345 → "0x3039").
// nullptr 은 세 컴파일러 모두 "nullptr" 이다.
inline constexpr signature_frame type_frame = make_signature_frame(type_signature<double>(), "double");
inline constexpr signature_frame value_frame = make_signature_frame(value_signature<nullptr>(), "nullptr");

constexpr std::string_view cut_frame(std::string_view signature, const signature_frame& frame) noexcept
{
    if (!frame.valid || signature.size() < frame.prefix + frame.suffix)
    {
        return {};
    }
    return signature.substr(frame.prefix, signature.size() - frame.prefix - frame.suffix);
}

template<class T>
consteval std::string_view raw_type_name() noexcept
{
    return cut_frame(type_signature<T>(), type_frame);
}

template<auto V>
consteval std::string_view raw_value_name() noexcept
{
    return cut_frame(value_signature<V>(), value_frame);
}

constexpr bool is_identifier_char(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

constexpr bool is_identifier(std::string_view text) noexcept
{
    if (text.empty() || (text.front() >= '0' && text.front() <= '9'))
    {
        return false;
    }
    for (const char c : text)
    {
        if (!is_identifier_char(c))
        {
            return false;
        }
    }
    return true;
}

// 타입 이름 정규화. 이름은 등록 키와 다형 태그로 쓰이므로 같은 타입이 컴파일러에
// 따라 다른 문자열이 되는 것을 줄인다:
//   - MSVC 가 붙이는 class/struct/enum/union 키워드 (템플릿 인자 안쪽까지)
//   - 쉼표 뒤·"> >" 사이 공백 — 두 식별자 사이(`unsigned int`)의 공백만 뜻이 있다
//   - MSVC 의 `__int64` 는 `long long` 으로
// 기본 템플릿 인자(MSVC 는 std::allocator 를 적고 Clang 은 생략한다)까지는 맞추지
// 않는다. 파일에 남는 다형 태그는 .named() 로 이름을 못 박는 것이 정답이다.
//
// out 이 nullptr 이면 길이만 잰다 — 정적 저장소 크기를 먼저 알아야 해서 두 번 돈다.
constexpr std::size_t normalize_type_name(std::string_view raw, char* out) noexcept
{
    constexpr std::string_view keywords[] = {"class ", "struct ", "enum ", "union "};
    constexpr std::string_view msvc_int64 = "__int64";
    constexpr std::string_view long_long = "long long";

    std::size_t length = 0;
    char last = '\0';
    const auto emit = [&](char c) {
        if (out != nullptr)
        {
            out[length] = c;
        }
        ++length;
        last = c;
    };

    std::size_t i = 0;
    while (i < raw.size())
    {
        const bool at_token_start = i == 0 || !is_identifier_char(raw[i - 1]);
        if (at_token_start)
        {
            bool skipped = false;
            for (const std::string_view keyword : keywords)
            {
                if (raw.substr(i).starts_with(keyword))
                {
                    i += keyword.size();
                    skipped = true;
                    break;
                }
            }
            if (skipped)
            {
                continue;
            }

            const std::string_view rest = raw.substr(i);
            if (rest.starts_with(msvc_int64) &&
                (rest.size() == msvc_int64.size() || !is_identifier_char(rest[msvc_int64.size()])))
            {
                if (is_identifier_char(last))
                {
                    emit(' ');
                }
                for (const char c : long_long)
                {
                    emit(c);
                }
                i += msvc_int64.size();
                continue;
            }
        }

        const char c = raw[i];
        if (c == ' ')
        {
            const bool keep = is_identifier_char(last) && i + 1 < raw.size() && is_identifier_char(raw[i + 1]);
            if (keep)
            {
                emit(' ');
            }
            ++i;
            continue;
        }
        emit(c);
        ++i;
    }
    return length;
}

// 멤버 포인터 표기에서 이름만 취한다. Clang/GCC 는 "&ns::t::m" 이고, MSVC 는 멤버
// 함수 포인터를 "void __cdecl ns::t::f(int)" 처럼 시그니처 전체로 적으므로 인자
// 목록 앞에서 먼저 자른다. 결과가 식별자가 아니면(operator() 같은 드문 표기) 빈
// 문자열을 돌려 .named() 를 요구하게 한다.
constexpr std::string_view member_name_from_raw(std::string_view raw) noexcept
{
    std::string_view name = raw;
    if (const std::size_t open = name.find('('); open != std::string_view::npos)
    {
        name = name.substr(0, open);
    }
    if (const std::size_t colons = name.rfind("::"); colons != std::string_view::npos)
    {
        name = name.substr(colons + 2);
    }
    // "operator()" 는 괄호에서 잘리면 "operator" 만 남아 식별자처럼 보인다.
    if (name.starts_with("operator") && (name.size() == 8 || !is_identifier_char(name[8])))
    {
        return {};
    }
    return is_identifier(name) ? name : std::string_view{};
}

// 유효한 열거자는 "ns::color::green"(비스코프드는 한정 없이 올 수도 있다), 이름 없는
// 값은 "(enum ns::color)0x7" 나 "(ns::color)7" 같은 캐스트 표기다.
constexpr std::string_view enumerator_name_from_raw(std::string_view raw) noexcept
{
    if (raw.empty() || raw.find('(') != std::string_view::npos)
    {
        return {};
    }
    std::string_view name = raw;
    if (const std::size_t colons = name.rfind("::"); colons != std::string_view::npos)
    {
        name = name.substr(colons + 2);
    }
    return is_identifier(name) ? name : std::string_view{};
}

// string_view 를 NUL 종단 정적 배열로 물질화한다 — C 문자열을 요구하는 소비자
// (로그, 서드파티 UI)가 .data() 를 그대로 쓸 수 있어야 한다.
template<std::size_t N>
constexpr std::array<char, N + 1> to_static_array(std::string_view text) noexcept
{
    std::array<char, N + 1> result{};
    for (std::size_t i = 0; i < N; ++i)
    {
        result[i] = text[i];
    }
    return result;
}

template<class T>
struct type_name_storage
{
    static constexpr std::string_view raw = raw_type_name<T>();
    static constexpr std::size_t length = normalize_type_name(raw, nullptr);
    static constexpr std::array<char, length + 1> buffer = [] {
        std::array<char, length + 1> result{};
        normalize_type_name(raw, result.data());
        return result;
    }();
};

template<auto Member>
struct member_name_storage
{
    static constexpr std::string_view raw = member_name_from_raw(raw_value_name<Member>());
    static constexpr std::array<char, raw.size() + 1> buffer = to_static_array<raw.size()>(raw);
};

template<auto Value>
struct enumerator_name_storage
{
    static constexpr std::string_view raw = enumerator_name_from_raw(raw_value_name<Value>());
    static constexpr std::array<char, raw.size() + 1> buffer = to_static_array<raw.size()>(raw);
};
} // namespace detail

// 정규화된 한정 타입 이름. NUL 종단이 보장된다.
template<class T>
constexpr std::string_view type_name_of() noexcept
{
    using storage = detail::type_name_storage<T>;
    return {storage::buffer.data(), storage::length};
}

// 멤버(데이터·함수) 포인터의 비한정 이름. 추출할 수 없으면 빈 문자열이다.
template<auto Member>
constexpr std::string_view member_name_of() noexcept
{
    using storage = detail::member_name_storage<Member>;
    return {storage::buffer.data(), storage::raw.size()};
}

// 이 툴체인에서 자동 이름 추출이 되는가. 안 되면 schema 가 .named() 를 요구한다.
inline constexpr bool name_extraction_supported = detail::type_frame.valid && detail::value_frame.valid;
} // namespace reflgen

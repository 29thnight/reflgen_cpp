#pragma once
// 매크로 없는 테스트 하네스. 라이브러리와 같은 원칙을 테스트에도 적용한다.
//
//   const reflgen_test::test case_name("영역: 무엇을 확인하는가", [] {
//       reflgen_test::check_equal(actual, expected);
//   });
//
// 중괄호 초기화가 아니라 괄호인 이유: clang-format 이 중괄호 안의 람다 본문을 여는
// 중괄호 열에 맞춰 깊게 밀어 넣는다. 괄호면 본문이 문장 들여쓰기를 따른다.
//
// 식 문자열을 찍는 매크로가 없는 대신 std::source_location 으로 파일·줄을 남긴다.
// check 는 실패를 기록하고 계속 가고, require 는 그 테스트를 중단한다.
#include "reflgen/serial/error.h"
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace reflgen_test
{
struct test_case
{
    std::string_view name;
    void (*body)();
};

std::vector<test_case>& registry();

struct test
{
    test(std::string_view name, void (*body)()) { registry().push_back({name, body}); }
};

// require 가 테스트를 멈출 때 던지는 표지.
struct abort_test
{
};

void report_failure(std::string_view message, const std::source_location& where);

template<class T>
std::string describe(const T& value)
{
    if constexpr (requires(std::ostream& stream) { stream << value; })
    {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }
    else
    {
        return "<unprintable>";
    }
}

inline void check(bool condition, std::string_view what = "condition is false",
                  std::source_location where = std::source_location::current())
{
    if (!condition)
    {
        report_failure(what, where);
    }
}

inline void require(bool condition, std::string_view what = "requirement is false",
                    std::source_location where = std::source_location::current())
{
    if (!condition)
    {
        report_failure(what, where);
        throw abort_test{};
    }
}

template<class A, class B>
void check_equal(const A& actual, const B& expected, std::source_location where = std::source_location::current())
{
    if (!(actual == expected))
    {
        report_failure("expected [" + describe(expected) + "] but got [" + describe(actual) + "]", where);
    }
}

// 기대한 예외 타입이 나오고, 메시지(what)에 fragment 가 들어 있는지 본다.
template<class Exception = reflgen::serialization_error, class F>
void check_throws(F&& action, std::string_view fragment = {},
                  std::source_location where = std::source_location::current())
{
    try
    {
        std::invoke(std::forward<F>(action));
    }
    catch (const Exception& error)
    {
        const std::string message = error.what();
        if (!fragment.empty() && message.find(fragment) == std::string::npos)
        {
            report_failure("exception message [" + message + "] does not contain [" + std::string(fragment) + "]",
                           where);
        }
        return;
    }
    catch (const std::exception& error)
    {
        report_failure(std::string("unexpected exception type: ") + error.what(), where);
        return;
    }
    report_failure("expected an exception, none was thrown", where);
}
} // namespace reflgen_test

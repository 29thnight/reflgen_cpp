#include "harness.h"
#include <cstdio>
#include <exception>
#include <string>

namespace reflgen_test
{
namespace
{
std::size_t failures_in_current_test = 0;
} // namespace

std::vector<test_case>& registry()
{
    static std::vector<test_case> cases;
    return cases;
}

void report_failure(std::string_view message, const std::source_location& where)
{
    ++failures_in_current_test;
    std::printf("    FAIL %s(%u): %.*s\n", where.file_name(), static_cast<unsigned>(where.line()),
                static_cast<int>(message.size()), message.data());
}
} // namespace reflgen_test

// 인자가 있으면 이름에 그 문자열이 든 테스트만 돈다.
int main(int argc, char** argv)
{
    using namespace reflgen_test;
    const std::string_view filter = argc > 1 ? argv[1] : "";

    std::size_t run = 0;
    std::size_t failed = 0;
    for (const test_case& item : registry())
    {
        if (!filter.empty() && item.name.find(filter) == std::string_view::npos)
        {
            continue;
        }
        ++run;
        failures_in_current_test = 0;
        // 테스트가 중단(abort)되면 마지막으로 찍힌 이름이 범인이다 — 먼저 찍고 비운다.
        std::printf("[ RUN  ] %.*s\n", static_cast<int>(item.name.size()), item.name.data());
        std::fflush(stdout);
        try
        {
            item.body();
        }
        catch (const abort_test&)
        {
        }
        catch (const std::exception& error)
        {
            report_failure(std::string("unhandled exception: ") + error.what(), std::source_location::current());
        }
        if (failures_in_current_test > 0)
        {
            ++failed;
            std::printf("[ FAIL ] %.*s\n", static_cast<int>(item.name.size()), item.name.data());
        }
    }
    std::printf("%zu tests, %zu passed, %zu failed\n", run, run - failed, failed);
    return failed == 0 && run > 0 ? 0 : 1;
}

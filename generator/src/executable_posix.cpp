#include "executable.h"
#include <system_error>

namespace reflgen::generator
{
    std::filesystem::path executable_path()
    {
        // Linux. 없는 체계에서는 빈 경로 — 생성기는 libclang 이 스스로 찾는 내장 header 로 파싱한다.
        std::error_code error;
        return std::filesystem::read_symlink("/proc/self/exe", error);
    }
} // namespace reflgen::generator

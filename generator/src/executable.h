#pragma once
// 실행 중인 생성기 파일의 경로 — 생성기 옆에 둔 clang 내장 header(clang/include)를 찾는다. PATH 로 불리면 argv[0] 은
// 경로가 아니므로 운영체제에 묻는다. 운영체제별 구현은 CMake 가 고른다(executable_windows.cpp, executable_posix.cpp).
#include <filesystem>

namespace reflgen::generator
{
    // 알 수 없으면 빈 경로.
    std::filesystem::path executable_path();
} // namespace reflgen::generator

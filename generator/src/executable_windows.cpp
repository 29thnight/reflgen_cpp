#include "executable.h"
#include <string>
#include <windows.h>

namespace reflgen::generator
{
    std::filesystem::path executable_path()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            // 버퍼가 모자라면 잘린 경로를 버퍼 크기만큼 채운다 — 늘려서 다시 묻는다.
            if (length < buffer.size())
            {
                buffer.resize(length);
                return std::filesystem::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
    }
} // namespace reflgen::generator

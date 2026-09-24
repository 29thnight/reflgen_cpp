#pragma once
// 진단 — MSVC 형식("file(line,col): error RG0001: …")으로 찍는다. MSBuild·VS Error List·
// Ninja 출력이 이 형식을 알아보고 원본 위치로 이동시킨다.
//
// 번호는 한 번 정하면 바꾸지 않는다(문서·검색의 기준이 된다):
//   RG0001  입력·옵션 오류          RG0100  Clang 이 보고한 컴파일 오류
//   RG0002  private 멤버에 friend 없음
//   RG0003  (폐지) header 가 생성 파일을 include 하지 않음 — 이제 빌드가 강제 include 한다
//   RG0004  반영할 수 없는 멤버(bit-field, 참조, 익명 union 등)
//   RG0005  지원하지 않는 선언(클래스 template, 익명 이름공간)
//   RG0006  오버로드된 메서드
//   RG0007  생성 파일 이름 충돌
#include "model.h"
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace reflgen::generator
{
    enum class severity
    {
        warning,
        error,
    };

    struct diagnostic
    {
        severity level = severity::error;
        std::string code;
        source_position position;
        std::string message;
    };

    class diagnostics
    {
      public:
        void report(severity level, std::string_view code, source_position position, std::string message)
        {
            entries_.push_back({level, std::string(code), std::move(position), std::move(message)});
        }

        void error(std::string_view code, source_position position, std::string message)
        {
            report(severity::error, code, std::move(position), std::move(message));
        }

        void warning(std::string_view code, source_position position, std::string message)
        {
            report(severity::warning, code, std::move(position), std::move(message));
        }

        bool has_errors() const noexcept
        {
            for (const diagnostic& entry : entries_)
            {
                if (entry.level == severity::error)
                {
                    return true;
                }
            }
            return false;
        }

        const std::vector<diagnostic>& entries() const noexcept { return entries_; }

        void print(std::FILE* stream) const
        {
            for (const diagnostic& entry : entries_)
            {
                const char* level = entry.level == severity::error ? "error" : "warning";
                if (entry.position.file.empty())
                {
                    std::fprintf(stream, "reflgen : %s %s: %s\n", level, entry.code.c_str(), entry.message.c_str());
                }
                else
                {
                    std::fprintf(stream, "%s(%u,%u): %s %s: %s\n", entry.position.file.c_str(), entry.position.line,
                                 entry.position.column, level, entry.code.c_str(), entry.message.c_str());
                }
            }
        }

      private:
        std::vector<diagnostic> entries_;
    };
} // namespace reflgen::generator

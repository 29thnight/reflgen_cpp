#pragma once
// 직렬화 실패. path() 는 실패 지점을 JSON Pointer(RFC 6901) 형식으로 담는다 —
// "/inventory/3/name".
//
// 경로는 예외가 되감기며 한 단계씩 앞에 붙는다(with_parent). 정상 경로에서 경로
// 스택을 유지하는 비용을 치르지 않으려는 것이다 — 예외가 없는 경로의 비용은 0이다.
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace reflgen
{
class serialization_error : public std::runtime_error
{
  public:
    explicit serialization_error(std::string message, std::string path = {})
        : std::runtime_error(compose(message, path)), message_(std::move(message)), path_(std::move(path))
    {
    }

    // 경로를 뺀 메시지.
    const std::string& message() const noexcept { return message_; }
    const std::string& path() const noexcept { return path_; }

    // segment 는 이스케이프 전 원문 키나 인덱스다.
    serialization_error with_parent(std::string_view segment) const
    {
        std::string prefix = "/";
        prefix.reserve(segment.size() + 1);
        for (const char c : segment)
        {
            if (c == '~')
            {
                prefix += "~0";
            }
            else if (c == '/')
            {
                prefix += "~1";
            }
            else
            {
                prefix += c;
            }
        }
        return serialization_error(message_, prefix + path_);
    }

  private:
    static std::string compose(const std::string& message, const std::string& path)
    {
        return path.empty() ? message : message + " (at " + path + ")";
    }

    std::string message_;
    std::string path_;
};
} // namespace reflgen

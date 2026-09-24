#pragma once
// 직렬화 실패. path() 는 실패 지점을 JSON Pointer(RFC 6901) 형식으로 담는다 —
// "/inventory/3/name".
//
// 경로는 직렬화가 내려가며 쌓는 조각 stack(detail::active_path)을 예외가 생성되는 순간
// 복사해 만든다. 그래서 백엔드·사용자 serializer 가 던진 오류에도 경로가 붙고, 예외는
// 한 번만 던져진다.
//
// 이미 깨져 본 길: 예전에는 단계마다 catch 해서 경로 조각을 붙여 다시 던졌다(정상 경로
// 비용 0 을 노렸다). MSVC 는 catch 블록을 아직 풀리지 않은 stack 위에서 실행하므로,
// 깊이 512 에서 난 실패가 512 번 연쇄로 재던져지며 예외 처리 frame 이 쌓여 stack overflow
// 로 죽었다(Debug 실측). 조각 stack 의 push/pop 은 할당이 없어 정상 경로 비용이 작다.
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reflgen
{
namespace detail
{
struct path_segment
{
    std::string_view key;
    std::size_t index = 0;
    bool is_index = false;
};

// 스레드마다 따로 — 여러 스레드가 동시에 직렬화해도 서로의 경로를 보지 않는다.
// 조각의 key 는 그 단계가 살아 있는 동안만 유효한 view 다(단계를 나가며 pop 된다).
inline thread_local std::vector<path_segment> active_path;

inline void append_escaped(std::string& out, std::string_view segment)
{
    out += '/';
    for (const char c : segment)
    {
        if (c == '~')
        {
            out += "~0";
        }
        else if (c == '/')
        {
            out += "~1";
        }
        else
        {
            out += c;
        }
    }
}

inline std::string current_path()
{
    std::string result;
    for (const path_segment& segment : active_path)
    {
        if (segment.is_index)
        {
            result += '/';
            result += std::to_string(segment.index);
        }
        else
        {
            append_escaped(result, segment.key);
        }
    }
    return result;
}
} // namespace detail

class serialization_error : public std::runtime_error
{
  public:
    // 경로는 지금 직렬화 중인 위치다.
    explicit serialization_error(std::string message) : serialization_error(message, detail::current_path()) {}

    serialization_error(std::string message, std::string path)
        : std::runtime_error(compose(message, path)), message_(std::move(message)), path_(std::move(path))
    {
    }

    // 경로를 뺀 메시지.
    const std::string& message() const noexcept { return message_; }
    const std::string& path() const noexcept { return path_; }

    // 경로 앞에 조각 하나를 붙인 사본. segment 는 이스케이프 전 원문 키나 인덱스다.
    serialization_error with_parent(std::string_view segment) const
    {
        std::string prefix;
        detail::append_escaped(prefix, segment);
        return serialization_error(message_, prefix + path_);
    }

    // 경로 끝에 조각 하나를 붙인 사본 — 없는 필드처럼 "지금 위치의 자식"을 가리킬 때 쓴다.
    serialization_error with_child(std::string_view segment) const
    {
        std::string path = path_;
        detail::append_escaped(path, segment);
        return serialization_error(message_, path);
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

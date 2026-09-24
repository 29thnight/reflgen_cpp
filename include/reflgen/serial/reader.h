#pragma once
// 역직렬화 입력 인터페이스 — 포맷 백엔드가 구현한다. 당겨 읽기(pull) 방식이다.
//
// 호출 순서 규약:
//   begin_array()  → while (next_element()) { 값 하나 읽기 } → end_array()
//   begin_object() → while (next_key(key)) { 값 하나 읽기 }  → end_object()
//
// begin_* 가 돌려주는 크기는 힌트다(텍스트 포맷은 모른다 → nullopt). 소비자는
// 이것으로 reserve 만 하고 반복의 끝은 next_* 로 판정한다. 방어는 두 겹이다:
//   - 백엔드는 신뢰할 수 없는 크기를 남은 입력 길이로 묶어서 넘긴다(몇 바이트짜리
//     입력이 2^60 개를 주장하는 경우).
//   - 직렬화 계층은 예약을 바이트 예산으로 다시 자른다(원소가 커서 "입력 바이트 수 ×
//     원소 크기"가 부풀어 오르는 경우 — serial/detail/container.h 의 bounded_reserve).
//
// 모든 실패는 serialization_error 다. 입력 위치(줄·열, 바이트 오프셋)는 메시지에
// 담고, 논리 경로(path)는 직렬화 계층이 채운다.
//
// 예외가 빠져나간 reader 는 다시 쓰지 않는다. 열린 배열·객체의 상태가 실패 지점에
// 멈춰 있어서, 이어 읽으면 틀린 자리에서 해석한다. 복구하려면 새 reader 로 다시 연다.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reflgen
{
enum class value_kind : unsigned char
{
    null,
    boolean,
    integer,
    floating,
    string,
    bytes,
    array,
    object,
};

class reader
{
  public:
    virtual ~reader() = default;

    // 다음 값의 종류. 소비하지 않는다.
    virtual value_kind peek() = 0;

    virtual void read_null() = 0;
    virtual bool read_bool() = 0;
    // 정수가 아니거나 범위를 넘으면 실패한다. read_float 는 정수도 받는다.
    virtual std::int64_t read_int() = 0;
    virtual std::uint64_t read_uint() = 0;
    virtual double read_float() = 0;
    virtual std::string read_string() = 0;
    // 텍스트 포맷은 인코딩된 문자열(base64 등)을 풀어서 돌려준다.
    virtual std::vector<std::byte> read_bytes() = 0;

    virtual std::optional<std::size_t> begin_array() = 0;
    virtual bool next_element() = 0;
    virtual void end_array() = 0;

    virtual std::optional<std::size_t> begin_object() = 0;
    // 다음 키를 key 에 담는다. 버퍼를 재사용하려고 반환값이 아니라 출력 인자다.
    virtual bool next_key(std::string& key) = 0;
    virtual void end_object() = 0;

    // 다음 값을 통째로 건너뛴다 — 모르는 키를 무시할 때 쓴다.
    virtual void skip_value() = 0;

  protected:
    reader() = default;
    reader(const reader&) = default;
    reader& operator=(const reader&) = default;
};
} // namespace reflgen

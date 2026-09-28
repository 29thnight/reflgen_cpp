#pragma once
// 직렬화 출력 인터페이스 — 포맷 백엔드가 구현한다.
//
// 데이터 모델은 JSON 과 같은 모양에 두 가지를 더했다:
//   - 정수를 부호 있음/없음으로 가른다. uint64 최댓값이 double 을 거치며 깨지지 않게.
//   - 바이트열(write_bytes)을 따로 둔다. 텍스트 포맷은 base64 등으로 적고, 바이너리
//     포맷은 원본 그대로 적는다.
//
// 호출 순서 규약:
//   begin_array(n)  → 값 n 개 → end_array()
//   begin_object(n) → (write_key → 값) n 번 → end_object()
// 크기는 항상 정확히 넘어온다. 텍스트 포맷은 무시해도 되고, 바이너리 포맷은 길이
// 접두로 쓴다. 규약 위반은 백엔드가 serialization_error 로 막는다.
//
// 순수 가상이 아닌 둘(write_float32·prefer_inline)은 텍스트 포맷을 위한 것이다 — 기본 구현이 있어 백엔드는
// 필요할 때만 덮어쓴다.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace reflgen
{
    class writer
    {
      public:
        virtual ~writer() = default;

        virtual void write_null() = 0;
        virtual void write_bool(bool value) = 0;
        virtual void write_int(std::int64_t value) = 0;
        virtual void write_uint(std::uint64_t value) = 0;
        virtual void write_float(double value) = 0;
        // float 값. 텍스트 포맷은 float 의 가장 짧은 왕복 표기로 적는다 — double 로 넓혀 적으면 0.1f 가
        // 0.10000000149011612 가 된다. 기본은 write_float 로 넘긴다(바이너리 포맷은 double 그대로다).
        virtual void write_float32(float value) { write_float(static_cast<double>(value)); }
        virtual void write_string(std::string_view value) = 0;
        virtual void write_bytes(std::span<const std::byte> value) = 0;

        // 표기 힌트 — 다음에 여는 컨테이너(begin_array·begin_object)는 짧다. 텍스트 포맷은 그것을 안쪽까지 한
        // 줄로 적어도 된다(YAML flow, 들여쓴 JSON 의 한 줄). 데이터는 바뀌지 않는다 — 무시해도 된다.
        virtual void prefer_inline() {}

        virtual void begin_array(std::size_t size) = 0;
        virtual void end_array() = 0;

        virtual void begin_object(std::size_t size) = 0;
        virtual void write_key(std::string_view key) = 0;
        virtual void end_object() = 0;

      protected:
        // 다형 기반 — 복사로 잘려 나가는 것(slicing)을 막으려 보호 영역에 둔다.
        writer() = default;
        writer(const writer&) = default;
        writer& operator=(const writer&) = default;
    };
} // namespace reflgen

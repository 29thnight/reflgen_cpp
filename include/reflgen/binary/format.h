#pragma once
// reflgen 바이너리 포맷 — 자기 서술형(태그 + 값), 리틀 엔디언.
//
//   태그  값
//   0x00  null
//   0x01  false
//   0x02  true
//   0x03  int     zigzag LEB128
//   0x04  uint    LEB128
//   0x05  float   IEEE-754 binary64, 8바이트
//   0x06  string  LEB128 길이 + UTF-8 바이트
//   0x07  bytes   LEB128 길이 + 바이트
//   0x08  array   LEB128 원소 수 + 원소들
//   0x09  object  LEB128 항목 수 + (LEB128 키 길이 + 키 + 값) 반복
//
// 자기 서술형으로 둔 이유: 모르는 키를 건너뛸 수 있어야 스키마가 바뀐 뒤에도 옛
// 데이터가 읽힌다. 스키마 고정 포맷(필드 순서만으로 읽는 것)은 더 작지만 그 성질을 잃는다.
#include <cstdint>

namespace reflgen::binary
{
enum class tag : std::uint8_t
{
    null = 0x00,
    false_value = 0x01,
    true_value = 0x02,
    signed_integer = 0x03,
    unsigned_integer = 0x04,
    floating = 0x05,
    string = 0x06,
    bytes = 0x07,
    array = 0x08,
    object = 0x09,
};
} // namespace reflgen::binary

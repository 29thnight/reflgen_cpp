#pragma once
// 두 백엔드를 한 번에 도는 왕복 도우미. 같은 값이 JSON 과 바이너리 양쪽에서 그대로
// 돌아와야 한다 — 데이터 모델이 포맷에 무관하다는 주장의 검증이다.
#include "harness.h"
#include "reflgen/binary.h"
#include "reflgen/json.h"
#include "reflgen/reflgen.h"
#include <source_location>
#include <string>

namespace reflgen_test
{
template<class T>
T json_round_trip(const T& value)
{
    return reflgen::json::from_string<T>(reflgen::json::to_string(value));
}

template<class T>
T binary_round_trip(const T& value)
{
    return reflgen::binary::from_bytes<T>(reflgen::binary::to_bytes(value));
}

template<class T>
void check_round_trip(const T& value, std::source_location where = std::source_location::current())
{
    check_equal(json_round_trip(value), value, where);
    check_equal(binary_round_trip(value), value, where);
}

// 비교 연산이 없거나 뜻이 다른 타입(valarray, priority_queue)용 — 비교 함수를 받는다.
template<class T, class Equal>
void check_round_trip_with(const T& value, Equal equal, std::source_location where = std::source_location::current())
{
    check(equal(json_round_trip(value), value), "json round trip differs", where);
    check(equal(binary_round_trip(value), value), "binary round trip differs", where);
}
} // namespace reflgen_test

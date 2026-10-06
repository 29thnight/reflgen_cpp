// interop 런타임 시험의 native 라이브러리 — 원본 구현과, 빌린 receiver 주소를 넘기는 시험 도우미.
// 생성기는 생성자·소유 함수를 만들지 않으므로 객체는 native 쪽이 만들고 주소만 빌려준다.
#include "interop_types.h"

#include <stdexcept>

#if defined(_WIN32)
#define INTEROP_RUNTIME_EXPORT __declspec(dllexport)
#else
#define INTEROP_RUNTIME_EXPORT __attribute__((visibility("default")))
#endif

double interop_tests::counter::scale(double factor) const
{
    if (factor < 0.0)
    {
        throw std::runtime_error("negative factor");
    }
    return value() * factor;
}

namespace
{
    interop_tests::counter shared_counter;
    interop_tests::native_counter shared_native_counter;
}

extern "C" INTEROP_RUNTIME_EXPORT void* interop_runtime_counter()
{
    return &shared_counter;
}

extern "C" INTEROP_RUNTIME_EXPORT void* interop_runtime_native_counter()
{
    return &shared_native_counter;
}

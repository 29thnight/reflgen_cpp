#pragma once
// 언어 모드와 실제 기능을 분리한다. MSVC는 /Zc:__cplusplus 없이도 정확한 모드를 읽는다.
#include <version>
#if defined(_MSVC_LANG)
#define REFLGEN_CPLUSPLUS _MSVC_LANG
#else
#define REFLGEN_CPLUSPLUS __cplusplus
#endif
#define REFLGEN_CXX20 (REFLGEN_CPLUSPLUS >= 202002L)
// 확정 전 draft 모드(-std=c++2b/2c)도 포함하되, 기능 사용은 아래 __cpp_* 검사로 제한한다.
#define REFLGEN_CXX23 (REFLGEN_CPLUSPLUS >= 202100L)
#define REFLGEN_CXX26 (REFLGEN_CPLUSPLUS >= 202400L)
#if !REFLGEN_CXX20
#error "reflgen requires C++20 or newer"
#endif
#if REFLGEN_CXX23 && defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
#define REFLGEN_HAS_IF_CONSTEVAL 1
#else
#define REFLGEN_HAS_IF_CONSTEVAL 0
#endif
#if REFLGEN_CXX23 && defined(__cpp_lib_byteswap) && __cpp_lib_byteswap >= 202110L
#define REFLGEN_HAS_BYTESWAP 1
#else
#define REFLGEN_HAS_BYTESWAP 0
#endif
#if REFLGEN_CXX26 && REFLGEN_HAS_IF_CONSTEVAL && defined(__cpp_impl_reflection) && __cpp_impl_reflection >= 202506L && \
    defined(__cpp_lib_reflection) && __cpp_lib_reflection >= 202506L && \
    defined(__cpp_lib_define_static) && __cpp_lib_define_static >= 202506L && __has_include(<meta>)
#define REFLGEN_HAS_STD_REFLECTION 1
#else
#define REFLGEN_HAS_STD_REFLECTION 0
#endif
// 빌드의 실제 컴파일 검사 결과를 우선한다. 버전 번호만으로 켜지 않는다.
#ifndef REFLGEN_USE_STD_REFLECTION
#define REFLGEN_USE_STD_REFLECTION REFLGEN_HAS_STD_REFLECTION
#endif
#if REFLGEN_USE_STD_REFLECTION != 0 && REFLGEN_USE_STD_REFLECTION != 1
#error "REFLGEN_USE_STD_REFLECTION must be 0 or 1"
#endif
#if REFLGEN_USE_STD_REFLECTION && !REFLGEN_HAS_STD_REFLECTION
#error "reflgen standard backend requires C++26 reflection, annotations and static strings; use AUTO or LEGACY"
#endif
#if REFLGEN_USE_STD_REFLECTION
#include <meta>
#endif
// GCC 모듈은 함수 내부 TLS, MSVC는 namespace TLS가 필요하다(__tlregdtor 링크 오류 회피).
#if defined(_MSC_VER)
#define REFLGEN_DETAIL_NAMESPACE_TLS 1
#else
#define REFLGEN_DETAIL_NAMESPACE_TLS 0
#endif
namespace reflgen { inline constexpr bool standard_reflection_enabled = REFLGEN_USE_STD_REFLECTION != 0; }

#pragma once
// 선택한 메서드의 최소 C ABI 와 선택적인 C# P/Invoke 선언. reflection 출력과 독립적이다.
#include "diagnostics.h"
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    struct interop_options
    {
        std::string module_name;
        std::string library_name; // 비면 module_name. 실제 공유 라이브러리의 로더 이름이다.
        std::string csharp_namespace = "Reflgen.Generated";
    };

    struct interop_output
    {
        std::string c_header;      // reflgen_<module>_interop.h
        std::string cpp_source;    // reflgen_<module>_interop.cpp
        std::string csharp_source; // reflgen_<module>_interop.cs; C# 대상으로 고른 타입이 없으면 안내 주석만 쓴다
        std::string fingerprint;   // 순서가 있는 ABI 계약의 FNV-1a 64-bit 값, 16자리 소문자 16진수
    };

    // exports 는 interop/lifetime 지시어와 선택된 메서드를 보존한다. 추출 단계는 선택되지 않은 메서드까지
    // 세어 is_overloaded 를 채우고 template/operator/deleted/잘못된 위치의 지시어를 진단한다.
    // borrowed handle 은 살아 있는 정확한 C++ 객체의 주소다. 수명·스레드 안전성·잘못된 주소를 검증하지 않는다.
    // 오류가 하나라도 있으면 출력을 모두 비운다. 파일 쓰기·동적 라이브러리 빌드·로딩은 호출자의 책임이다.
    interop_output emit_interop(const std::vector<class_model>& exports, const target_model& target,
                                const interop_options& options, diagnostics& report);
} // namespace reflgen::generator

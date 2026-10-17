# 리플렉션 백엔드와 C++ 버전별 동작

reflgen은 스키마를 만드는 경로를 선택하면서 런타임 등록·조회·JSON·binary API를 유지한다.
라이브러리를 `import reflgen.json;`으로 사용하는 것과 사용자 모듈에서 스키마를 생성하는 것은
별개의 기능이며 함께 사용할 수 있다.

## 생성 경로

| 입력/설정 | 경로 | 속성 표기 |
|---|---|---|
| 기존 헤더 | libclang 생성기 | `[[reflgen::reflect]]`, `[[reflgen::ignore]]` |
| MSVC 모듈의 export된 public 타입 | IFC 생성기 | 같은 표기; 속성은 전처리 sidecar 필요 |
| C++26 + 기능 검사 성공 + 타입 목록 | 표준 리플렉션 | `[[=reflgen::reflect{}]]`, `[[=reflgen::ignore{}]]` |

MSBuild에서는 기존 `reflgen.targets`를 import하면 혼합 프로젝트의 헤더와 모듈을 구분한다.
헤더 생성과 모듈 provider 컴파일 → IFC 생성 → 소비자 컴파일의 순서를 배선한다.
VS의 저장 시 생성도 같은 MSBuild 경로를 호출한다. CMake에서는 헤더에 `reflgen_generate()`,
모듈에 `reflgen_generate_ifc()`를 명시한다. 구체적인 프로젝트 설정은
[README의 IFC 사용법](../README.md#msvc-c20-module-reflection-from-ifc)을 참고한다.

IFC는 타입·가시성 정보의 원천이다. 기존 사용자 어트리뷰트는 IFC만으로 얻지 못하므로 같은 컴파일
설정으로 만든 `/P` 전처리 sidecar를 결합한다. `SOURCE` 또는 `METADATA`를 지정하고, 별도 모듈의
사용자 어트리뷰트에는 `ATTRIBUTE_SCOPES`와 `ATTRIBUTE_MODULES`를 지정한다.
`ignore`는 생성 스키마에서 타입/멤버를 제외하며 `transient`는 멤버를 유지하고 직렬화에서 제외한다.
모듈에서 export한 사용자 어트리뷰트도 테스트에 포함한다.

## 언어 버전과 기능 감지

`include/reflgen/core/features.h`가 내부 분기를 모은다. 언어 모드만으로 표준 리플렉션을 활성화하지 않는다.

| 매크로 | 역할 |
|---|---|
| `REFLGEN_CPLUSPLUS` | `_MSVC_LANG` 우선, 그 외 `__cplusplus` |
| `REFLGEN_CXX20/23/26` | 언어 모드 판정; 23/26의 draft 값도 수용 |
| `REFLGEN_HAS_IF_CONSTEVAL`, `REFLGEN_HAS_BYTESWAP` | 해당 표준 기능 감지 |
| `REFLGEN_HAS_STD_REFLECTION` | C++26, reflection/static-string 기능 매크로 및 `<meta>` 검사 |
| `REFLGEN_USE_STD_REFLECTION` | 최종 0/1 선택; 미지원 환경에서 1 강제 시 컴파일 오류 |
| `REFLGEN_DETAIL_NAMESPACE_TLS` | MSVC/GCC 모듈 컴파일러에 맞는 내부 TLS 저장 위치 |

빌드는 reflection, typed annotation, member splicing, static string을 실제로 컴파일해 지원을 판정한다.
MSVC의 C++23 preview/C++26 latest 플래그도 컴파일 검사하므로 구형 CMake가 요청한 26을 23으로
낮추는 일을 방지한다. `/Zc:__cplusplus`가 없어도 `_MSVC_LANG`로 감지한다.

binary 실수 I/O는 C++20에서 바이트 조합, C++23 byteswap 지원 시 고정 폭 복사 경로를 사용한다.
두 경로의 wire 형식은 동일한 little-endian 8바이트다. 성능 향상률을 측정한 결과는 아니다.

## 백엔드 선택

CMake의 `BACKEND AUTO/STD/LEGACY`, MSBuild의 `ReflgenReflectionBackend=Auto/Std/Legacy`를 사용한다.

- AUTO는 C++26과 명시적 타입 목록(`TYPES` / `ReflgenReflectType`)이 있을 때 표준 백엔드를 검사한다.
  실패하거나 타입 목록이 없으면 기존 경로를 유지한다.
- STD는 지원 여부를 검사하고 실패 시 구성/빌드를 중단한다. MSBuild의 미지원 진단은 RG0261이다.
- LEGACY는 리플렉션 지원 컴파일러에서도 기존 경로를 선택한다.

선언 JSON/C ABI·C# interop 출력은 헤더 생성기 기능이다. `DECLARATIONS_JSON`/`INTEROP`
(MSBuild의 `ReflgenDeclarationsJson`/`ReflgenInterop`)를 요청하면 AUTO는 LEGACY를 선택한다.
이 출력과 STD를 함께 강제하면 명시적으로 거부한다(MSBuild RG0263).

표준 리플렉션은 namespace/module 전체 타입을 열거하지 못하므로 등록할 클래스 목록이 필요하다.
생성된 등록 코드는 기존 `register_module(registry&)`와 `register_module()` 형태를 유지한다.
named library module은 기본 LEGACY이며 `reflgen_add_modules(... BACKEND STD)`로 선택할 수 있다.
모듈과 소비자의 언어 모드 및 백엔드를 일치시켜야 한다.

AUTO는 소스 표기를 변환하지 않는다. 표준 annotation과 기존 어트리뷰트를 한 소스에서 지원하려면
`REFLGEN_USE_STD_REFLECTION`으로 선언을 분기한다. 인자 없는 annotation 값에는 `{}`를 붙인다.
사용자 annotation은 structural type으로 작성하고 문자열에는 `reflgen::static_string`을 사용한다.
표준 백엔드는 public 필드, 명시적으로 annotation한 public 메서드, enum 및 사용자 annotation을 처리한다.
수동/외부 스키마가 우선한다. private 상태, public 상속, 제외하지 않은 bit-field/reference 필드는
수동 스키마가 필요하다.

## Visual Studio

확장은 활성 구성의 C++ 버전을 읽어 표준 어트리뷰트를 필터링하면서 `[[reflgen::` 완성을 제공한다.
C++20에는 `assume`을 표시하지 않고, C++26/latest에서는 `indeterminate`를 제공하며
`carries_dependency`를 제외한다. C++26/latest의 `[[=` 입력은 annotation 목록으로 전환하고,
`reflgen::` 확정 후 멤버 목록을 연다. 인자 없는 값은 `ignore{}`, 인자가 필요한 값은 `range(`로 삽입한다.
편집기 완성 지원과 컴파일러의 표준 리플렉션 지원은 별도로 판단한다.

저장 시 생성 진단은 Error List에 연결하고 생성 후 IntelliSense를 갱신한다.
현재 지원 대상은 `.vcxproj`이며 CMake Open Folder의 저장 시 생성은 지원하지 않는다.

검증 환경·결과 및 재현 경로는 [검증 기록](verification-2026-10-17.md)에 정리했다.

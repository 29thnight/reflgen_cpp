# 리플렉션 백엔드와 C++ 버전별 동작

reflgen은 스키마를 만드는 경로를 선택하면서 런타임 등록·조회·JSON·binary API를 유지한다.
라이브러리를 `import reflgen.json;`으로 사용하는 것과 사용자 모듈에서 스키마를 생성하는 것은
별개의 기능이며 함께 사용할 수 있다.

## 생성 경로

| 입력/설정 | 경로 | 속성 표기 |
|---|---|---|
| 기존 헤더 | libclang 생성기 | `[[reflgen::reflect]]`, `[[reflgen::ignore]]` |
| MSVC 모듈의 export된 타입과 public 중첩 타입 | IFC 생성기 | 같은 표기; 속성은 전처리 sidecar 필요 |
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

## IFC 상속·접근·등록

IFC 경로는 같은 primary IFC 안의 public 상속과 public 중첩 class·struct·enum을 처리한다.
미반영 public 중간 클래스·CRTP를 지나 가장 가까운 반영 부모를 연결하므로, 부모의 필드·메서드는 기존
`base<T>` 스키마를 통해 런타임 조회와 직렬화에 포함된다. 비반영 private/protected 보조 부모는 제외하며,
접근할 수 없는 반영 부모와 public virtual·pack-expanded 상속 경로는 `RG0200`으로 거부한다.

기본 export 타입은 friend가 없으면 public 멤버만 반영한다. 전역 `::reflgen::access`를 friend로 선언하면
private/protected 필드·메서드도 포함하며, `ignore`를 먼저 적용한다. 명시적 `reflect` 타입에 접근할 수 없는
미제외 멤버가 있으면 friend가 필요하다는 오류를 낸다. global module fragment에서 `reflgen/core/hook.h`를
include해 라이브러리의 실제 선언을 사용한다. named module 안에서 같은 이름을 새로 선언하는 방식은 사용할 수 없다.

오버로드 메서드는 IFC 시그니처에서 정확한 멤버 포인터 타입을 만들어 구분한다. cv/ref 한정자와 지원하는
`noexcept` 명세를 유지하며, 타입을 해석할 수 없으면 임의로 추정하지 않는다. 런타임 호출 대상의 기존 한정자
제약은 바뀌지 않는다. 외부 IFC 참조, partition·재export 수집, 템플릿 자체의 스키마 등은 아직 지원하지 않는다.
특히 `import reflgen.core;`를 통해 얻은 friend가 외부 IFC 참조로 기록되면 현재 reader로는 해석할 수 없다.

등록 함수가 소비자와 같은 사용자 serializer를 보도록 `REGISTRATION_HEADERS`와
`REGISTRATION_MODULES "import.name=path.ifc"`를 `reflgen_generate_ifc()`에 지정한다.
MSBuild는 `ReflgenRegistrationHeaders`·`ReflgenRegistrationImports` 속성을 전달하며, 별도 IFC 프로젝트는
`ReflgenRegistrationModule` 항목으로 `/reference`도 연결한다. 이 옵션은 생성된 등록 코드의 의존성만
설정하고 reader가 다른 IFC를 탐색하게 하지는 않는다. 생성 스키마와 사용자 serializer를 포함한 다음
등록 서술자를 인스턴스화하므로, 다른 번역 단위와 직렬화 동작이 달라지는 것을 막는다.

재현 예제는 [tests/ifc/engine](../tests/ifc/engine)에 있다. CI의 `ifc (Debug)`·`ifc (Release)` 작업이
CMake/MSBuild 생성·컴파일, 상속·비공개 상태·오버로드 호출, JSON/binary 왕복과 거부 진단을 실행한다.

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

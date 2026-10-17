# 구현 검증 기록 — 2026-10-17

검증 환경은 Windows x64 / VS Community 2026 18.10.2, MSVC 19.51.36260과
WSL Ubuntu 24.04 / GCC 16.2.0 / CMake 4.2.3이다. 아래 수치는 이 환경에서 수행한 로컬 검증이며
CI 또는 다른 컴파일러의 지원을 뜻하지 않는다. 이후 원격 변경 통합 후 검증은 별도 표에 기록한다.

## 버전·백엔드 행렬

| 요청 모드 | 실제 언어 값 | MSVC AUTO | GCC AUTO | binary 실수 경로 |
|---|---:|---|---|---|
| C++20 | 202002 | LEGACY | LEGACY | 바이트 조합 |
| C++23 | 202302 | LEGACY | LEGACY | 고정 폭 복사 |
| C++26 | 202400 | LEGACY | STD | 고정 폭 복사 |

두 컴파일러에서 AUTO/LEGACY 각각 세 언어 모드의 구성·빌드·실행을 확인했다. JSON/binary 왕복과
정확한 바이트를 검사한다. MSVC는 `/Zc:__cplusplus` 없이도 올바른 언어 모드를 확인했다.
GCC C++26 forced STD와 기능이 켜진 상태의 LEGACY(`HAS_STD=1 USE_STD=0`)가 실행됐다.
MSVC forced STD는 지원 검사에서 거부됐다. 두 컴파일러의 C++20/23 매크로 강제 활성화도 거부됐다.

재현은 [행렬 스크립트](../tests/standard/check_matrix.cmake)와
[표준 백엔드 테스트 안내](../tests/standard/README.md)를 사용한다. VS 개발자 PowerShell 예:

```powershell
cmake "-DCOMPILER=$((Get-Command cl).Source)" "-DWORK=$PWD/build/version-msvc-matrix" `
  -DSUPPORTS_STD=OFF -P tests/standard/check_matrix.cmake
```

GCC에는 컴파일러 절대 경로와 `-DSUPPORTS_STD=ON`을 전달한다. 스크립트는 실제 기능 값을
`matrix-results.txt`, 빌드 출력을 `matrix-build.log`에 남긴다.

## 런타임·생성·모듈

| 검증 경로 | 결과 |
|---|---|
| MSVC 런타임 C++20 / 23 / 26 | 133/133, 135/135, 135/135 |
| MSVC 헤더 생성기·MSBuild 포함 CTest C++20 / 23 / 26 | 각각 16/16 |
| MSVC named modules C++20 Debug / C++23 Release / C++26 Release | 각각 8/8 |
| MSVC IFC·사용자 속성·헤더/모듈 혼합 | 7/7 |
| GCC C++26 표준 annotation·등록·직렬화·`import reflgen.json` | 4/4 |
| GCC 표준 백엔드 Release 및 설치된 패키지 검증 | 각각 4/4 |
| VS 확장 순수 로직 | 137/137 |

테스트는 중복되므로 수를 합산하지 않는다. 일부 MSBuild/IFC 하위 fixture는 C++20을 명시하며,
세 언어 모드에서 모든 하위 fixture까지 동일한 모드로 바뀌었다는 의미는 아니다.

표준 테스트에는 모듈에서 export한 사용자 annotation, ignore/transient, 메서드 호출과 매개변수 이름,
enum 범위, 수동 스키마 우선순위, 오류 경로 및 이후 정상 읽기가 포함된다. binary 검증은 독립적인
바이트 기준으로 ±0, subnormal, 최대 유한값, 무한대, NaN payload, 비정렬 및 잘린 입력을 검사한다.
big-endian 실제 하드웨어 실행이나 성능 향상률 측정은 수행하지 않았다.

관련 재현 소스는 [IFC fixture](../tests/ifc), [named modules](../tests/modules),
[standard backend](../tests/standard), [MSBuild writer 검사](../tests/msbuild/check_standard_writer.cmake)에 있다.

## 실제 Visual Studio 검증

최신 빌드 VSIX를 일반 VS 프로필에 재설치했다. 제거·설치 종료 코드는 0이며 설치된 두 DLL의
SHA256이 빌드 출력과 일치했다. 다음 동작을 실제 편집기에서 확인했다.

- C++20/C++26의 `[[` 표준 목록과 `reflgen::` 멤버 완성, 입력 및 Tab 확정.
- `[[` → `=`에서 annotation 목록 전환, namespace 확정 후 멤버 목록 열림,
  `ignore{}` 및 `range(` 삽입.
- 헤더/모듈 혼합 fixture 저장 시 해당 경로 재생성. ignored `cache`는 스키마에서 제외되고
  추가한 transient 필드는 스키마에 남음.
- 생성 후 IntelliSense rescan 로그, Release F5 빌드 성공 및 실행 종료 코드 0,
  마지막 Error List 오류 0 / 경고 0.

로컬 MSVC는 필요한 `<meta>`를 제공하지 않아 위 VS 실행은 AUTO의 기존 경로다.
표준 백엔드의 실제 실행은 GCC에서 확인했다. MSBuild 표준 등록 writer는 출력 배선 검사이며
MSVC에서 표준 리플렉션이 컴파일됐다는 증거가 아니다.

로컬 원시 로그는 무시된 `build/version-*.log`, `build/version-msvc-matrix/`,
`build/std-verification.md`, `build/std-vs-live/`에 보관했으며 Git에 포함하지 않는다.

## 원격 main 통합 후 재검증

커밋 전 원격의 선언 메타데이터·C/C# interop 변경 9개를 통합했다. CMake/MSBuild의 새 옵션과
모듈·표준 백엔드 옵션을 함께 유지하고, interop의 선택 출력 소유권 기록 및 Clean 동작을 보존했다.
선언 JSON/interop 요청은 AUTO에서 LEGACY를 유지하며 강제 STD는 명확히 거부한다.
CMake 구성 검사와 MSBuild request 회귀 검사로 이 선택을 확인했다.

| 최종 통합 경로 | 결과 |
|---|---|
| MSVC 클린 빌드·전체 CTest (선언 JSON, C/C# 호출, MSBuild 포함) | 20/20 |
| MSVC IFC·혼합·사용자 속성 + 표준 writer | 8/8 |
| GCC 표준 백엔드·모듈·JSON import | 4/4 |

MSVC 첫 증분 빌드의 생성기 단위 테스트가 비정상 종료해 전체 클린 빌드를 수행했고, 해당 테스트는
재검사에서 통과했다. 위의 실제 VS 검증은 원격 통합 전 결과이며, 통합 후 VS UI를 재실행한 결과는 아니다.
통합 후 로그는 로컬 `build/publish-*.log`에 보관한다.

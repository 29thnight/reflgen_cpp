# reflgen

매크로 없는 C++20 리플렉션과, 포맷을 갈아 끼울 수 있는 직렬화 라이브러리.

- **매크로 0개** — 공개 API 는 물론 라이브러리 내부에도 `#define`·`#if` 가 없다(`#pragma once` 와 `#include` 뿐).
  컴파일러 차이는 전처리기가 아니라 **행동 탐지**로 흡수한다.
- **C++20 기반, C++23 지원** — 표준 라이브러리만 쓴다. C++26 정식 리플렉션(P2996)이 오면 서술을 만드는
  방법만 바뀌고 질의·직렬화 API 는 그대로 둔다(아래 로드맵).
- **STL 전반 직렬화** — 모든 표준 컨테이너·어댑터와 값 래퍼, 그리고 STL 모양의 사용자 컨테이너를 자동으로.
- **포맷은 인터페이스** — `reflgen::writer` / `reflgen::reader` 를 구현하면 새 포맷이 된다. JSON 과 바이너리
  백엔드가 기본으로 들어 있다.
- 헤더 전용. 검증: MSVC 19.51(VS 18)·clang-cl 22 × C++20·C++23, `/W4 /WX`·`-Wextra -Wpedantic -Werror`.

## 빠른 시작

```cpp
#include "reflgen/reflgen.h"
#include "reflgen/json.h"

struct item
{
    std::string name;
    int count = 1;

    static consteval auto reflect()
    {
        return reflgen::schema<item>(
            reflgen::field<&item::name>,
            reflgen::field<&item::count>.with(reflgen::range(1, 99)));
    }
};

std::string text = reflgen::json::to_string(item{ "potion", 3 }, 4);
item loaded = reflgen::json::from_string<item>(text);
```

전체 예제는 [examples/quick_start.cpp](examples/quick_start.cpp).

## 타입 서술

서술의 단일 창구는 `reflgen::schema_of<T>` 이고, 서술을 공급하는 길은 둘이다.

```cpp
// ① 클래스 안 레시피 — private 필드도 된다(friend struct reflgen::access; 를 두면 reflect() 도 private 가능)
class player
{
    friend struct reflgen::access;
    int hp_ = 100;

    static consteval auto reflect()
    {
        return reflgen::schema<player>(
            reflgen::base<entity>,                                     // 직계 부모(다중 상속 가능)
            reflgen::field<&player::hp_>.named("hp")                   // 이름 바꾸기
                .with(reflgen::range(0, 100), tooltip("health")),      // 속성 여러 개
            reflgen::method<&player::fire>.parameters("shots"))
            .named("game.player")                                      // 등록 키·다형 태그
            .with(reflgen::display_name("Player"));                    // 타입 속성
    }
};

// ② 외부 특수화 — 손댈 수 없는 타입, 그리고 코드 생성기의 출력이 들어갈 자리
template<>
struct reflgen::reflection<vendor::config>
{
    static constexpr auto value = reflgen::schema<vendor::config>(
        reflgen::field<&vendor::config::port>, reflgen::field<&vendor::config::host>);
};
```

레시피는 **로컬**이다 — 자기 필드와 직계 부모만 적는다. 부모 필드까지 합친 순회는 질의가 계산한다
(`reflgen::for_each_field<T>(f)`, `reflgen::for_each_field(object, f)`, 부모 우선).

### 속성

속성은 **생성자 호출식**이다. 사용자 정의 속성도 `constexpr` 생성자가 있는 구조체 하나면 된다.

```cpp
struct tooltip
{
    std::string_view text;
    constexpr explicit tooltip(std::string_view value) : text(value) {}
};
```

| 기본 속성 | 뜻 |
|---|---|
| `display_name("…")`, `description("…")` | 편집기 표시용 |
| `range(min, max)` | 값 구간(검증기·편집기용, 직렬화는 검사하지 않음) |
| `serialized_name("…")` | 직렬화 키를 멤버 이름과 다르게 |
| `transient()` | 직렬화 제외 |
| `required()` | 역직렬화 입력에 반드시 있어야 함 |
| `hidden()`, `readonly()` | 편집기 힌트 |

조회: 컴파일 타임은 `field.has_attribute<A>()` / `field.attribute<A>()`, 런타임은
`field_info::attributes().find<A>()`.

이 표기가 그대로 C++26 으로 이어진다 — 코드 생성기는 C++20/23 에서 `[[reflgen::range(0, 1)]]` 의 인자 토큰을
옮겨 위 식을 만들고, C++26 에서는 같은 식이 주석(annotation) `[[=reflgen::range(0, 1)]]` 의 값이 된다.

### 열거형

`reflgen::enum_entries<E>`, `enum_name(e)`, `enum_cast<E>("name")`. 기본은 값 범위 [-128, 128] 스캔이며
`enum_range<E>` 특수화로 넓히거나, `reflection<E>` 로 정확한 표를 준다(비트 플래그 등).

## 코드 생성기 (reflgen)

손으로 `reflect()` 를 쓰는 대신 attribute 만 달면, 빌드 때 생성기가 서술을 만든다. 매크로는 없다 —
`[[…]]` 는 표준 attribute 문법이고, 컴파일러는 모르는 attribute 를 무시한다(경고는 `reflgen_generate()` 가 끈다).

```cpp
// player.h
#pragma once
#include "reflgen/reflgen.h"

namespace game
{
constexpr int max_level = 99;

enum class [[reflgen::reflect]] element { fire, water, wind = 1 << 10 };   // 스캔 범위 밖 값도 정확한 표

class [[reflgen::reflect("game.player")]] player : public entity             // 인자 = 등록 키·다형 태그
{
    friend struct reflgen::access;                                           // private 멤버를 반영하려면

  public:
    static constexpr float max_hp = 999.0f;

    [[reflgen::range(1, max_level)]] int level = 1;                          // 이름공간의 이름도,
    [[reflgen::range(0.0f, max_hp), game::tooltip("HP")]] float hp = 100.0f; // 클래스 멤버 이름도 그대로
    [[reflgen::ignore]] int cache = 0;                                       // 반영에서 제외
    [[reflgen::transient]] int frame = 0;                                    // 반영하되 저장하지 않음
    [[reflgen::reflect]] void level_up(int amount);                          // 메서드는 표시한 것만

  private:
    int secret_ = 7;                                                         // 표시가 없어도 반영된다
};
} // namespace game

#include "player.reflgen.h"                                                  // 끝에 생성 파일을 포함
```

```cmake
reflgen_generate(my_game
    HEADERS include/game/player.h include/game/item.h
    MODULE game                       # 등록 함수: reflgen::generated::register_game()
    ATTRIBUTE_SCOPES game)            # reflgen 외에 옮길 attribute 이름공간
```

- **반영 범위**: `[[reflgen::reflect]]` 클래스의 non-static data member 전부(C++26 native reflection 과 같은 결과).
  `[[reflgen::ignore]]` 로 뺀다. 메서드는 `[[reflgen::reflect]]` 를 단 것만. `public` 부모는 `base<>` 로 이어진다.
- **attribute 옮기기**: `reflgen::` 과 `ATTRIBUTE_SCOPES` 의 이름공간만 스키마로 간다. 인자 없는
  attribute(`[[reflgen::transient]]`)는 표지 타입으로 보고 `{}` 를 붙인다. 선언 맨 앞의 attribute 는 그 선언의
  모든 멤버에, 이름 뒤의 attribute(`int x [[reflgen::ignore]], y;`)는 그 멤버에만 붙는다.
- **인자의 이름**: 클래스 안에서 쓴 것처럼 풀린다. 생성 코드는 클래스 밖에 있으므로, 자기 클래스(부모 포함)와
  감싸는 클래스의 멤버 이름은 생성기가 `::game::player::max_hp` 처럼 한정하고, 이름공간의 이름은
  `using namespace` 로 보이게 한다. 인자 안의 주석은 옮기지 않는다.
- **오류 위치**: attribute 인자에 오타가 있으면 컴파일 오류가 생성 파일이 아니라 원본 header 의 그 줄을
  가리킨다(생성 코드가 `#line` 으로 원본을 가리킨다).
- **등록**: 다형 포인터로 읽고 쓸 타입은 시작할 때 `reflgen::generated::register_<module>()` 를 한 번 부른다.
- **빌드 연동**: 생성물은 구성(config)마다 `<OUTPUT_DIRECTORY>/<config>/` 에 따로 둔다. HEADERS 가 include 하는
  파일이 바뀌어도 다시 생성한다(depfile). 파싱 표준은 `CXX_STANDARD` 와 `cxx_std_NN` compile feature 중 높은
  것이다. 생성 디렉터리는 `BUILD_INTERFACE` 로만 include 경로에 붙는다 — 설치(install)할 라이브러리라면 생성
  header 도 직접 설치한다. CMake 3.25 이상.
- **요구 사항**: libclang. Visual Studio 는 동봉본(`VC/Tools/Llvm`)을 자동으로 찾는다. 그 밖은
  `REFLGEN_LIBCLANG_DIR` 로 준다. C API header 는 `third_party/clang-c`(LLVM 22.1.3, Apache-2.0 WITH LLVM-exception).

진단은 MSVC 형식(`file(line,col): error RG0002: …`)이라 VS Error List 에서 원본으로 바로 간다.

| 코드 | 뜻 |
|---|---|
| RG0001 | 입력·옵션 오류(없는 header, 같은 header 두 번 등) |
| RG0002 | 반영할 private 멤버가 있는데 `friend struct reflgen::access;` 가 없다 |
| RG0003 | header 가 자기 생성 파일을 include 하지 않는다 |
| RG0004 | 반영할 수 없는 멤버(bit-field, 참조, 익명 union, static 멤버, 생성자·소멸자) |
| RG0005 | 지원하지 않는 선언(클래스·멤버 함수 template, union, 익명 이름공간) |
| RG0006 | 오버로드된 메서드 |
| RG0007 | 생성 파일 이름 충돌(같은 이름의 header 둘) |
| RG0100 | Clang 이 보고한 컴파일 오류 |

## 직렬화

```cpp
reflgen::serialize(writer, value);              // 포맷은 writer 구현이 정한다
reflgen::deserialize(reader, value);            // 제자리 읽기
auto value = reflgen::deserialize<T>(reader);

reflgen::json::to_string(value, indent) / reflgen::json::from_string<T>(text)
reflgen::binary::to_bytes(value)        / reflgen::binary::from_bytes<T>(bytes)
```

읽기 의미론 — 파일 형식이 코드보다 오래 산다는 전제다.

- 입력에 없는 필드는 **기존 값을 유지**한다(`required` 가 붙은 필드는 실패).
- 모르는 키는 **건너뛴다**.
- 실패는 `reflgen::serialization_error` 이고, `path()` 에 실패 지점이 JSON Pointer 로 담긴다
  (`"/inventory/1/count"`). 입력 위치(줄·열 / 바이트 오프셋)는 메시지에 있다.
- 정수는 대상 타입 범위를 검사한다 — 300 이 `uint8_t` 로 조용히 잘리지 않는다.
- 순환 참조(`shared_ptr` A→B→A, 자기 자신을 가리키는 `reference_wrapper`)는 쓰는 시점에
  `serialization_error("cyclic reference")` 로 멈춘다. 같은 객체를 두 경로에서 가리키는 공유는 순환이
  아니라서 각 경로에 값으로 적힌다.

### 지원 타입

| 범주 | 타입 | 모양 |
|---|---|---|
| 스칼라 | `bool`, 정수 전 폭, 실수, `std::byte`, `nullptr_t`, `monostate` | 값 |
| 문자 | `char`·`wchar_t`·`char8/16/32_t` | 한 글자 문자열 |
| 열거형 | 모든 열거형 | 이름(없으면 정수) |
| 문자열 | `basic_string` 전 문자 타입, 문자 배열 | UTF-8 문자열 |
| 쓰기 전용 | `string_view`, C 문자열, 범위 뷰 | |
| 시퀀스 | `vector`·`deque`·`list`·`forward_list`·`(unordered_)(multi)set`·`valarray` | 배열 |
| 크기 고정 | `array`·C 배열·`span` | 배열(원소 수 일치 필요) |
| 맵 | 유일 키 + 문자열·정수·열거형 키 | 객체 `{"3": …}` |
| 맵 | multimap, 구조체 키 | `[[k, v], …]` |
| 어댑터 | `stack`·`queue`·`priority_queue` | 속 컨테이너 순서의 배열 |
| 바이트열 | 연속 `std::byte` 범위 | base64(JSON) / 원본(바이너리) |
| 래퍼 | `optional`, `atomic`, `reference_wrapper` | null 또는 값 |
| 포인터 | `unique_ptr`, `shared_ptr` | null 또는 값, 다형이면 `{"type","value"}` |
| 합·곱 | `variant` → `[index, value]`, `pair`/`tuple`/튜플 모양 → 배열, `complex` → `[re, im]` | |
| 기타 | `bitset` → `"0101"`, `chrono` → 틱 수, `filesystem::path` → 일반형 UTF-8 | |
| C++23 | `std::expected` (`reflgen/serial/expected.h` 포함 시) → `{"value"}`/`{"error"}` | |
| C++23 | `flat_map`·`flat_set` — 모양으로 자동 인식 | |

### 사용자 컨테이너

STL 과 같은 모양(`begin/end` + `clear` + `emplace_back`·`push_back`·`insert`·`insert_after` 중 하나, 맵은
`key_type/mapped_type`)이면 **특수화 없이 자동**이다. 인터페이스가 다르면 `container_traits` 를 특수화한다:

```cpp
template<class T>
struct reflgen::container_traits<ring_buffer<T>>
{
    using value_type = T;
    static constexpr reflgen::container_kind kind = reflgen::container_kind::sequence;
    static std::size_t size(const ring_buffer<T>& c);
    template<class F> static void for_each(const ring_buffer<T>& c, F&& f);
    static void clear(ring_buffer<T>& c);
    static void add(ring_buffer<T>& c, T&& item);      // 또는 assign(c, std::vector<T>&&)
    static void reserve(ring_buffer<T>& c, std::size_t n);   // 선택
};
```

맵 규약과 자세한 설명은 [container_traits.h](include/reflgen/serial/container_traits.h). 모양 자체가 다른
타입(예: 색을 `"#rrggbb"` 로)은 `reflgen::serializer<T>` 를 특수화한다(`write`/`read` 두 함수).

## 새 포맷 백엔드

`reflgen::writer` / `reflgen::reader` 의 순수 가상 함수를 구현한다.
데이터 모델은 JSON 모양에 부호 구분 정수와 바이트열을 더한 것이다.

```
writer: write_null/bool/int/uint/float/string/bytes, begin_array(n)/end_array, begin_object(n)/write_key/end_object
reader: peek, read_*, begin_array → while(next_element) … → end_array,
        begin_object → while(next_key(key)) … → end_object, skip_value
```

- `begin_*` 의 크기는 쓰기에서는 정확한 값, 읽기에서는 힌트(reserve 용)다. 신뢰할 수 없는 입력의 크기는
  남은 입력 길이로 상한을 검사해 넘긴다.
- 중첩 깊이 상한을 둔다. JSON·바이너리 백엔드는 reader·writer 모두 기본 512 이고 생성자 인자
  (`to_string(value, indent, max_depth)` 등)로 바꾼다. writer 상한은 순환이 아닌 아주 깊은 구조가
  stack overflow 대신 오류로 끝나게 한다.

[json/](include/reflgen/json) 과 [binary/](include/reflgen/binary) 가 참고 구현이다.

## 런타임 서술자와 등록소

```cpp
const reflgen::type_descriptor& type = reflgen::type_descriptor_of<player>();
for (const reflgen::field_info& field : type.fields())   // 상속 필드 포함, 부모 우선
{
    field.name(); field.key(); field.type_name(); field.attributes().find<reflgen::range<int>>();
    void* address = field.address(&object);
}
reflgen::register_type<fireball>();                      // 다형 포인터로 읽고 쓸 파생 타입
```

- 표는 전부 `constexpr` 이다 — 정적 초기화 순서 문제가 없다.
- 등록은 명시 호출이다. 정적 라이브러리의 자동 등록 객체는 링커가 버릴 수 있어서 쓰지 않는다.
- 다형 직렬화는 동적 타입을 RTTI 로 찾는다. RTTI 는 다형 타입에서만 쓴다(`-fno-rtti` 빌드도 나머지는 동작).

## 빌드와 시험

```powershell
./scripts/test.ps1                       # MSVC·clang-cl × C++20·C++23 네 조합
./scripts/test.ps1 -Presets msvc-cpp20   # 하나만
```

CMake 소비자는 `find_package(reflgen)` 후 `reflgen::reflgen` 을 링크한다. 테스트 하네스도 매크로 없이
`std::source_location` 으로 만들었다([tests/harness.h](tests/harness.h)).

### 인코딩

주석이 한글(UTF-8)이다. 공개 헤더는 **UTF-8 BOM** 을 달고 있어, `/utf-8` 없이 CP949 로 빌드하는 MSVC
프로젝트에서도 한글 주석이 코드를 삼키지 않는다. CMake 타깃은 `/utf-8` 도 전파한다.

## 코딩 컨벤션

이름은 STL 컨벤션(snake_case, 멤버 후치 `_`, 템플릿 매개변수 PascalCase, 상수도 snake_case), 중괄호·공백·
들여쓰기는 CreatorEngine 의 [.clang-format](.clang-format) 이다. 주석은 한글로, 무엇이 아니라 왜를 적는다.

## 알려진 제약

- 컴파일러가 만든 타입 이름은 표준 라이브러리 타입에서 구현마다 다르다(MSVC 는 기본 템플릿 인자를 적는다).
  파일에 남는 다형 태그는 `.named()` 로 못 박을 것.
- `variant` 는 인덱스로 적는다 — 대안의 순서가 파일 형식의 일부다.
- `shared_ptr` 의 공유 관계는 보존하지 않는다(읽으면 사본이 된다). 순환은 오류로 막는다.
- raw pointer·`weak_ptr` 는 직렬화하지 않는다(소유인지 참조인지 알 수 없다). ID·handle 로 적으려면
  `reflgen::serializer<T*>` 를 특수화한다.
- `default_registry()` 는 모듈(DLL)마다 하나다.
- 생성기는 클래스 template·멤버 함수 template·오버로드된 메서드·열거자 attribute·union 을 다루지 않는다(진단으로
  알린다). 이름 없는 매개변수는 빈 이름으로 남는다.
- attribute 인자의 이름을 감싸는 이름공간들에서 찾을 때는 `using namespace` 를 바깥부터 모두 여는 방식이라,
  바깥과 안쪽 이름공간에 같은 이름이 있으면 모호하다 — 그때는 한정해서 쓴다.
- 자동 이름 추출은 `std::source_location` 이 템플릿 인자를 포함한 시그니처를 주는 구현에서 된다
  (MSVC STL, libstdc++). 그렇지 않은 구현(libc++)에서는 `.named()` 또는 코드 생성기가 필요하다 —
  `reflgen::name_extraction_supported` 로 확인한다. GCC·libc++ 는 아직 CI 로 검증하지 않았다.

## 라이선스

[MIT](LICENSE) © 2026 29thnight

## 로드맵

1. **코드 생성기** — 첫 판 완료(CMake 연동). 남은 것: MSBuild(.props/.targets) 연동, 클래스 template,
   오버로드된 메서드, 열거자 attribute.
2. **Visual Studio 확장** — 저장 시 생성, Error List 진단, 어트리뷰트 자동완성, C++26 이행 codemod.
3. **C++26 네이티브 백엔드** — `std::meta::nonstatic_data_members_of` + `annotations_of` 로 `schema_of<T>`
   를 만든다. 질의·직렬화 API 는 바뀌지 않는다.
4. GCC·libc++ CI.

#pragma once
// 생성기가 header 에서 읽어 낸 것 — 출력(emit)은 이것만 본다. libclang 에 대한 의존은
// 추출(extract) 단계에서 끝난다.
#include <memory>
#include <string>
#include <vector>

namespace reflgen::generator
{
    struct source_position
    {
        std::string file; // '/' 구분자로 정규화한 절대 경로
        unsigned line = 0;
        unsigned column = 0;
    };

    struct target_model
    {
        std::string triple;
        int pointer_width = 0; // bit 수, 파싱하지 않은 빈 모듈은 0
    };

    // .with(...) 에 들어갈 식 하나 — 사용자가 쓴 attribute 를 그대로 옮긴 것.
    struct attribute_use
    {
        std::string expression; // "reflgen::range(0, max_hp)", "mygame::editable{}"
        source_position position;
        std::string scope = {};
        std::string name = {};
        bool has_arguments = false;
        // 원본 token 철자 — expression 의 T:: 한정이나 C++ 식 평가를 적용하지 않는다. 주석은 제외한다.
        std::vector<std::string> argument_tokens = {};
    };

    // 선언 메타데이터용 타입. C++ 철자와 정규 타입을 함께 남기며 소비자의 ABI·언어 매핑은 결정하지 않는다.
    // pointee/element_type 은 한 단계씩 이어지므로 const T* 와 T* const, T& 와 T&& 를 구별한다.
    struct declaration_type
    {
        std::string spelling;
        std::string canonical_spelling;
        std::string kind = "unsupported";
        std::string declaration; // record/enum 의 완전한 선언 이름, 나머지는 빈 문자열
        bool is_const = false;
        bool is_volatile = false;
        long long size_bytes = -1; // 불완전하거나 크기가 없는 타입은 -1
        std::shared_ptr<declaration_type> pointee;
        std::shared_ptr<declaration_type> element_type;
        long long array_size = -1;
    };

    struct parameter_model
    {
        std::string name;
        declaration_type type;
        source_position position;
    };

    struct field_model
    {
        std::string name;
        source_position position;
        std::vector<attribute_use> attributes;
        declaration_type type = {};
        std::string owner = {};
        std::string access = {};
    };

    struct method_model
    {
        std::string name;
        std::vector<std::string> parameters;
        source_position position;
        std::vector<attribute_use> attributes;
        declaration_type return_type = {};
        std::vector<parameter_model> parameter_declarations = {};
        std::string owner = {};
        std::string access = {};
        bool is_static = false;
        bool is_const = false;
        bool is_volatile = false;
        bool is_variadic = false;
        std::string ref_qualifier = "none";
        std::string signature = {};
        std::string calling_convention = {};
        std::string exception_specification = {};
        bool is_overloaded = false;
        bool is_deleted = false;
        bool qualifiers_known = true;
        bool is_consteval = false;
        bool declaration_specifiers_known = true;
    };

    // 다른 클래스를 가리키는 것(부모) — 생성 header 가 전방 선언할 수 있게 이름공간과 키를 함께 둔다.
    struct type_reference
    {
        std::string qualified_name; // "engine::transform"
        std::string name;           // "transform"
        std::string class_key = "struct";
        std::string enclosing_namespace; // "engine", "a::b" — 비면 전역
        bool nested = false;             // 클래스 안에 선언됐다 — 전방 선언할 수 없다
        bool templated = false;          // 템플릿 특수화다 — 전방 선언하지 않는다
    };

    struct class_model
    {
        std::string qualified_name;          // "game::player"
        std::string name;                    // "player" — 전방 선언에 쓴다
        std::string class_key = "struct";    // "class" 또는 "struct" — 전방 선언이 원본과 같은 키를 쓴다
        std::string schema_name;             // 등록 키·다형 태그 — 기본은 qualified_name
        std::vector<std::string> namespaces; // 감싸는 이름공간들, 바깥부터: "game", "game::detail"
        // 가장 가까운 반영된 조상들. 반영하지 않는 public 부모(CRTP 중간층 등)는 건너 그 위를 본다.
        std::vector<type_reference> bases;
        std::vector<field_model> fields;
        std::vector<method_model> methods;
        std::vector<attribute_use> attributes;
        source_position position;
        bool nested = false; // 클래스 안에 선언됐다 — 전방 선언할 수 없어 원본 header 가 필요하다
        // attribute 인자가 쓰는 멤버·reflgen·std 밖의 이름 — 있으면 원본 header 가 필요하다.
        std::vector<std::string> external_names;
    };

    struct enum_model
    {
        std::string qualified_name;
        std::string name;                    // 전방 선언에 쓴다
        std::vector<std::string> namespaces; // class_model 과 같다
        std::vector<std::string> enumerators;
        source_position position;
        bool nested = false;
        bool scoped = false;         // enum class
        std::string underlying_type; // 적힌 기반 타입("unsigned char") — 비면 적지 않았다
    };

    // 편집기 자동완성에 내보내는 attribute 하나 — 생성기 지시어(reflect, ignore)와 스키마로 옮겨지는
    // attribute 타입.
    struct attribute_info
    {
        std::string kind;      // "directive" 또는 "attribute"
        std::string scope;     // "reflgen", "game"
        std::string name;      // "range"
        std::string signature; // "range(T min_value, T max_value)" — 인자 없는 표지는 이름만
        std::string summary;   // 선언 앞 주석, 한 줄로
    };

    // 입력 header 하나에서 나온 것들.
    struct header_model
    {
        std::string path; // 정규화한 절대 경로
        std::vector<class_model> classes;
        std::vector<enum_model> enums;
    };
} // namespace reflgen::generator

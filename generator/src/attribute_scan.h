#pragma once
// [[…]] attribute 찾기 — token 목록만 다루는 순수 코드(libclang 없이 시험할 수 있다).
//
// Clang 은 모르는 attribute 를 AST 에서 버린다. 그래서 파일의 token 을 다시 훑어 그룹을 찾고,
// 선언의 위치(offset)로 그 선언의 몫을 고른다. 선언의 범위(extent)만 훑으면 안 된다 — 다중
// declarator 문(`int a [[x]], b;`)에서 b 의 범위가 a 뒤의 [[x]] 를 품고, a 의 범위는 [[x]] 를
// 빠뜨린다(clang 22 실측).
//
// 인식하는 모양:
//   [[ns::name]]            [[ns::name(args)]]            [[ns::a, ns::b(x)]]
//   [[using ns: a, b(x)]]   괄호 안은 (), [], {} 짝을 맞춰 통째로 인자로 본다.
// 인자는 원문 구간(offset)으로 돌려주고, 생성 코드에 옮길 때는 argument_text() 가 그 구간의
// token 으로 식을 다시 짓는다.
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reflgen::generator
{
enum class token_kind
{
    punctuation,
    keyword,
    identifier,
    literal,
    comment,
};

struct token
{
    token_kind kind = token_kind::punctuation;
    std::string spelling;
    std::size_t begin = 0; // 파일 안 byte offset
    std::size_t end = 0;
};

struct scanned_attribute
{
    std::string scope; // "reflgen", "mygame" — 이름공간이 없으면 빈 문자열
    std::string name;
    bool has_arguments = false;
    std::size_t arguments_begin = 0; // 여는 괄호 바로 뒤
    std::size_t arguments_end = 0;   // 닫는 괄호 바로 앞
    std::size_t begin = 0;           // attribute 이름의 시작(진단 위치)
};

struct attribute_group
{
    std::size_t begin = 0; // 첫 '[' 의 offset
    std::size_t end = 0;   // 마지막 ']' 의 끝 offset
    std::vector<scanned_attribute> attributes;
};

// 모양이 맞지 않는 [[…]] 는 건너뛴다(배열 첨자처럼 우연히 '[[' 가 된 경우).
std::vector<attribute_group> scan_attribute_groups(std::span<const token> tokens);

// ASCII C++ 식별자인가 — module 이름, 한정할 멤버 이름을 가린다.
bool is_identifier(std::string_view text) noexcept;

// 아래는 파일 전체의 token·그룹(offset 순)에서 선언 하나의 몫을 고르는 도구다.

// offset 에서 시작하는 token 의 바로 다음 token 이 시작하는 곳. 없으면 nullopt.
std::optional<std::size_t> next_token_offset(std::span<const token> tokens, std::size_t offset);

// offset 에서 시작해 빈틈없이 이어진 그룹들. 선언 맨 앞의 attribute(그 선언의 모든 declarator 에
// 붙는다)와 declarator 이름 바로 뒤의 attribute(그 declarator 에만 붙는다)가 이 모양이다 —
// `int a [[x]], b;` 에서 [[x]] 는 a 의 것이지 b 의 것이 아니다.
std::vector<attribute_group> groups_run_at(std::span<const token> tokens, std::span<const attribute_group> groups,
                                           std::size_t offset);

// [begin, end) 안에 온전히 들어 있는 그룹들.
std::vector<attribute_group> groups_between(std::span<const attribute_group> groups, std::size_t begin,
                                            std::size_t end);

// 식별자 하나 앞에 붙일 한정자("::game::player::")를 돌려준다. 붙일 것이 없으면 빈 문자열.
using qualifier_lookup = std::function<std::string(std::string_view identifier)>;

// [begin, end) 의 token 으로 식을 다시 짓는다. 주석은 버리고 token 사이 공백은 한 칸으로 줄인다
// (`//` 주석을 그대로 옮기면 뒤에 붙는 ')' 가 주석에 먹힌다). '::', '.', '->' 뒤가 아닌
// 식별자에는 qualifier_of 가 주는 한정자를 붙인다 — 클래스 멤버 이름은 클래스 밖에 있는 생성
// 코드에서 한정 없이는 보이지 않는다.
std::string argument_text(std::span<const token> tokens, std::size_t begin, std::size_t end,
                          const qualifier_lookup& qualifier_of);
} // namespace reflgen::generator

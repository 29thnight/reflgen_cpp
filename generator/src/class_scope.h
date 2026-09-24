#pragma once
// 클래스 범위의 사실 — 감싸는 이름공간, friend 선언, attribute 인자의 이름 한정.
#include "attribute_scan.h"
#include <clang-c/Index.h>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace reflgen::generator
{
    // 감싸는 이름공간들 — 바깥부터 "a", "a::b". 익명 이름공간이 끼면 nullopt.
    std::optional<std::vector<std::string>> enclosing_namespaces(CXCursor cursor);

    // 클래스 자신이 friend struct reflgen::access; 를 선언했는가(중첩 클래스의 것은 세지 않는다).
    bool befriends_access(CXCursor record);

    // attribute 인자에서 한정 없이 쓴 이름을 찾을 클래스 하나. 생성 코드는 클래스 밖에 있어서
    // 클래스 멤버 이름은 한정해 줘야 보인다(이름공간의 이름은 using namespace 로 보인다).
    struct class_scope
    {
        std::string qualifier; // "::game::player::"
        std::set<std::string, std::less<>> names;
    };

    // 클래스 하나 — 멤버, 중첩 타입, 비스코프드 열거자, 부모 클래스의 것까지.
    class_scope scope_of(CXCursor record);

    // 감싸는 클래스들, 안쪽부터(자신은 빼고).
    std::vector<class_scope> enclosing_class_scopes(CXCursor cursor);

    // 안쪽 클래스부터 찾는다 — C++ 의 이름 찾기 순서와 같다. scopes 는 돌려준 함수보다 오래 살아야 한다.
    qualifier_lookup lookup_in(const std::vector<class_scope>& scopes);
} // namespace reflgen::generator

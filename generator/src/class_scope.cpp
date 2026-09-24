#include "class_scope.h"
#include "clang_api.h"
#include <algorithm>
#include <utility>

namespace reflgen::generator
{
namespace
{
// 클래스 안에서 한정 없이 쓸 수 있는 이름 — 멤버, 중첩 타입, 비스코프드 열거자, 부모 클래스의 것.
void collect_member_names(CXCursor record, std::set<std::string, std::less<>>& names, int depth)
{
    constexpr int max_base_depth = 32; // 비정상적으로 깊은 상속 사슬에서 멈춘다
    if (depth > max_base_depth)
    {
        return;
    }
    visit_children(record, [&](CXCursor child, CXCursor) {
        const CXCursorKind kind = clang_getCursorKind(child);
        if (kind == CXCursor_CXXBaseSpecifier)
        {
            const CXCursor base = clang_getTypeDeclaration(clang_getCursorType(child));
            if (!clang_Cursor_isNull(base))
            {
                collect_member_names(base, names, depth + 1);
            }
            return CXChildVisit_Continue;
        }
        if (kind == CXCursor_EnumDecl && !clang_EnumDecl_isScoped(child))
        {
            visit_children(child, [&](CXCursor constant, CXCursor) {
                names.insert(cursor_spelling(constant));
                return CXChildVisit_Continue;
            });
        }
        const bool named_member = kind == CXCursor_FieldDecl || kind == CXCursor_VarDecl ||
                                  kind == CXCursor_CXXMethod || kind == CXCursor_FunctionTemplate || is_record(kind) ||
                                  kind == CXCursor_EnumDecl || kind == CXCursor_ClassTemplate ||
                                  kind == CXCursor_TypedefDecl || kind == CXCursor_TypeAliasDecl ||
                                  kind == CXCursor_TypeAliasTemplateDecl;
        if (std::string name = cursor_spelling(child); named_member && is_identifier(name))
        {
            names.insert(std::move(name));
        }
        return CXChildVisit_Continue;
    });
}
} // namespace

std::optional<std::vector<std::string>> enclosing_namespaces(CXCursor cursor)
{
    std::vector<std::string> names;
    for (CXCursor parent = clang_getCursorSemanticParent(cursor);
         !clang_Cursor_isNull(parent) && clang_getCursorKind(parent) != CXCursor_TranslationUnit;
         parent = clang_getCursorSemanticParent(parent))
    {
        if (clang_getCursorKind(parent) == CXCursor_Namespace)
        {
            if (clang_Cursor_isAnonymous(parent))
            {
                return std::nullopt;
            }
            names.push_back(cursor_spelling(parent));
        }
    }
    std::reverse(names.begin(), names.end());
    std::vector<std::string> cumulative;
    std::string current;
    for (const std::string& name : names)
    {
        current = current.empty() ? name : current + "::" + name;
        cumulative.push_back(current);
    }
    return cumulative;
}

// 클래스 자신의 friend 선언만 본다. 텍스트로 찾으면 중첩 클래스의 friend 까지 걸린다.
bool befriends_access(CXCursor record)
{
    bool found = false;
    visit_children(record, [&](CXCursor child, CXCursor) {
        if (clang_getCursorKind(child) == CXCursor_FriendDecl)
        {
            visit_children(child, [&](CXCursor target, CXCursor) {
                const CXType type = clang_getCanonicalType(clang_getCursorType(target));
                found = found || strip_tag(take_string(clang_getTypeSpelling(type))) == "reflgen::access";
                return CXChildVisit_Continue;
            });
        }
        return found ? CXChildVisit_Break : CXChildVisit_Continue;
    });
    return found;
}

class_scope scope_of(CXCursor record)
{
    class_scope scope{"::" + type_name_of(record) + "::", {}};
    collect_member_names(record, scope.names, 0);
    return scope;
}

std::vector<class_scope> enclosing_class_scopes(CXCursor cursor)
{
    std::vector<class_scope> scopes;
    for (CXCursor parent = clang_getCursorSemanticParent(cursor); is_record(clang_getCursorKind(parent));
         parent = clang_getCursorSemanticParent(parent))
    {
        scopes.push_back(scope_of(parent));
    }
    return scopes;
}

qualifier_lookup lookup_in(const std::vector<class_scope>& scopes)
{
    return [&scopes](std::string_view name) -> std::string {
        for (const class_scope& scope : scopes)
        {
            if (scope.names.contains(name))
            {
                return scope.qualifier;
            }
        }
        return {};
    };
}
} // namespace reflgen::generator

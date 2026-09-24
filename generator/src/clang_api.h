#pragma once
// libclang C API 의 얇은 RAII 포장. 이 파일 밖에서는 clang_dispose* 를 직접 부르지 않는다.
#include "attribute_scan.h"
#include "model.h"
#include <clang-c/Index.h>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace reflgen::generator
{
    // CXString 을 받아 복사하고 해제한다.
    std::string take_string(CXString text);

    std::string cursor_spelling(CXCursor cursor);

    // "struct game::player" → "game::player"
    std::string strip_tag(std::string name);

    // 커서가 선언한 타입의 완전한 이름("game::player").
    std::string type_name_of(CXCursor cursor);

    // class, struct, union.
    bool is_record(CXCursorKind kind);

    // '\' 를 '/' 로 바꾸고 절대 경로로 만든다 — 같은 파일을 한 문자열로 비교하려는 것이다.
    std::string normalize_path(const std::string& path);

    source_position position_of(CXSourceLocation location);
    std::size_t offset_of(CXSourceLocation location);
    std::string file_of(CXCursor cursor);

    struct index_deleter
    {
        void operator()(void* index) const noexcept { clang_disposeIndex(index); }
    };
    using index_handle = std::unique_ptr<void, index_deleter>;

    struct translation_unit_deleter
    {
        void operator()(CXTranslationUnitImpl* unit) const noexcept { clang_disposeTranslationUnit(unit); }
    };
    using translation_unit_handle = std::unique_ptr<CXTranslationUnitImpl, translation_unit_deleter>;

    // 위치가 가리키는 파일(없으면 nullptr).
    CXFile file_handle_of(CXSourceLocation location);

    // 파일 안 offset 의 위치 — 진단이 원본을 가리키게 한다.
    source_position position_in(CXTranslationUnit unit, CXFile file, std::size_t offset);

    // clang 이 읽은 파일 전체를 attribute_scan 이 다루는 token 으로 옮긴다.
    std::vector<token> tokenize_file(CXTranslationUnit unit, CXFile file);

    // 파싱에 쓰인 파일 전부(주 파일 제외), 정규화한 경로.
    std::vector<std::string> included_files(CXTranslationUnit unit);

    // clang_visitChildren 을 람다로 — 콜백이 CXChildVisitResult 를 돌려준다.
    template<class Visitor>
    void visit_children(CXCursor cursor, Visitor&& visitor)
    {
        clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor parent, CXClientData data) -> CXChildVisitResult {
                return (*static_cast<std::remove_reference_t<Visitor>*>(data))(child, parent);
            },
            &visitor);
    }
} // namespace reflgen::generator

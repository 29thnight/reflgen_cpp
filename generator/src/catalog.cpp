#include "catalog.h"
#include "attribute_scan.h"
#include "clang_api.h"
#include <map>
#include <string_view>
#include <utility>

namespace reflgen::generator
{
    namespace
    {
        constexpr std::string_view library_scope = "reflgen";
        constexpr std::string_view library_attributes_header = "/reflgen/core/attributes.h";

        std::string_view trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        // "// 값의 허용 구간.\n// 직렬화는 …" → "값의 허용 구간. 직렬화는 …"
        std::string comment_summary(CXCursor cursor)
        {
            const std::string raw = take_string(clang_Cursor_getRawCommentText(cursor));
            std::string summary;
            std::size_t begin = 0;
            while (begin < raw.size())
            {
                const std::size_t newline = raw.find('\n', begin);
                const std::size_t end = newline == std::string::npos ? raw.size() : newline;
                std::string_view line = trim(std::string_view(raw).substr(begin, end - begin));
                for (const std::string_view marker : {"///", "//", "/**", "/*", "*"})
                {
                    if (line.starts_with(marker))
                    {
                        line.remove_prefix(marker.size());
                        break;
                    }
                }
                if (line.ends_with("*/"))
                {
                    line.remove_suffix(2);
                }
                line = trim(line);
                if (!line.empty())
                {
                    summary += summary.empty() ? "" : " ";
                    summary += line;
                }
                begin = end + 1;
            }
            return summary;
        }

        std::string parameter_list(CXCursor function)
        {
            std::string text;
            const int count = clang_Cursor_getNumArguments(function);
            for (int i = 0; i < count; ++i)
            {
                const CXCursor parameter = clang_Cursor_getArgument(function, static_cast<unsigned>(i));
                std::string item = take_string(clang_getTypeSpelling(clang_getCursorType(parameter)));
                if (const std::string name = cursor_spelling(parameter); !name.empty())
                {
                    item += " " + name;
                }
                text += (i == 0 ? "" : ", ") + item;
            }
            return text;
        }

        // 복사·이동이 아닌 public 생성자마다 한 줄. 사용자 생성자가 없으면 이름만 — 표지 타입이다.
        std::string signatures_of(CXCursor record, const std::string& name)
        {
            std::string signatures;
            visit_children(record, [&](CXCursor child, CXCursor) {
                const CXCursorKind kind = clang_getCursorKind(child);
                const bool constructor =
                    kind == CXCursor_Constructor ||
                    (kind == CXCursor_FunctionTemplate && clang_getTemplateCursorKind(child) == CXCursor_Constructor);
                if (constructor && clang_getCXXAccessSpecifier(child) == CX_CXXPublic &&
                    !clang_CXXConstructor_isCopyConstructor(child) && !clang_CXXConstructor_isMoveConstructor(child))
                {
                    signatures += signatures.empty() ? "" : "\n";
                    signatures += name + "(" + parameter_list(child) + ")";
                }
                return CXChildVisit_Continue;
            });
            return signatures.empty() ? name : signatures;
        }

        bool is_struct(CXCursor cursor)
        {
            const CXCursorKind kind = clang_getCursorKind(cursor);
            return kind == CXCursor_StructDecl ||
                   (kind == CXCursor_ClassTemplate && clang_getTemplateCursorKind(cursor) == CXCursor_StructDecl);
        }

        bool is_attribute_candidate(CXCursor cursor, std::string_view scope)
        {
            const CXCursorKind kind = clang_getCursorKind(cursor);
            const bool record =
                kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl || kind == CXCursor_ClassTemplate;
            if (!record || !clang_isCursorDefinition(cursor) || !is_identifier(cursor_spelling(cursor)))
            {
                return false;
            }
            // reflgen 이름공간에는 descriptor·registry 같은 타입도 있다 — attribute 는 attributes.h 의 struct 뿐이다.
            return scope != library_scope ||
                   (is_struct(cursor) && file_of(cursor).ends_with(library_attributes_header));
        }

        struct candidate
        {
            attribute_info info;
            bool marked = false; // [[reflgen::attribute]]
        };

        using catalog_map = std::map<std::pair<std::string, std::string>, candidate>;

        void collect_scope(CXCursor name_space, const std::string& scope, const directive_query& declares,
                           catalog_map& found)
        {
            visit_children(name_space, [&](CXCursor child, CXCursor) {
                // 반영하는 데이터 타입은 attribute 가 아니다.
                if (is_attribute_candidate(child, scope) && !declares(child, "reflect"))
                {
                    const std::string name = cursor_spelling(child);
                    // 같은 이름공간이 여러 번 열려도 처음 본 정의 하나만.
                    found.try_emplace({scope, name}, candidate{{"attribute", scope, name, signatures_of(child, name),
                                                                comment_summary(child)},
                                                               declares(child, "attribute")});
                }
                return CXChildVisit_Continue;
            });
        }

        void visit_namespaces(CXCursor parent, const std::set<std::string>& scopes, const directive_query& declares,
                              catalog_map& found)
        {
            visit_children(parent, [&](CXCursor child, CXCursor) {
                const CXCursorKind kind = clang_getCursorKind(child);
                if (kind == CXCursor_LinkageSpec)
                {
                    visit_namespaces(child, scopes, declares, found);
                }
                else if (kind == CXCursor_Namespace)
                {
                    // attribute 이름공간은 식별자 하나다([[a::b::c]] 는 문법이 아니다) — 최상위만 본다.
                    if (const std::string name = cursor_spelling(child); scopes.contains(name))
                    {
                        collect_scope(child, name, declares, found);
                    }
                }
                return CXChildVisit_Continue;
            });
        }

        void append_escaped(std::string& out, std::string_view value)
        {
            for (const char c : value)
            {
                switch (c)
                {
                case '\\':
                    out += "\\\\";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    break;
                default:
                    out += c;
                    break;
                }
            }
        }
    } // namespace

    std::vector<attribute_info> collect_attribute_catalog(CXTranslationUnit unit, const std::set<std::string>& scopes,
                                                          const directive_query& declares)
    {
        std::vector<attribute_info> catalog = {
            {"directive", std::string(library_scope), "reflect", "reflect\nreflect(\"schema.name\")",
             "클래스·열거형을 반영 대상으로, 메서드를 스키마에 넣을 것으로 표시한다. 클래스에 준 인자는 등록 키·다형 "
             "태그가 된다."},
            {"directive", std::string(library_scope), "ignore", "ignore", "이 멤버를 반영에서 뺀다."},
            {"directive", std::string(library_scope), "attribute", "attribute",
             "이 타입을 편집기 자동완성의 attribute 로 내보낸다. 이름공간에 하나라도 있으면 표시한 타입만 내보낸다."},
        };
        catalog_map found;
        visit_namespaces(clang_getTranslationUnitCursor(unit), scopes, declares, found);
        // 표지를 쓴 이름공간은 표지 단 것만 — 표지는 "이 이름공간에는 attribute 가 아닌 타입도 있다"는 뜻이다.
        std::set<std::string> marked_scopes;
        for (const auto& [key, entry] : found)
        {
            if (entry.marked)
            {
                marked_scopes.insert(key.first);
            }
        }
        for (auto& [key, entry] : found)
        {
            if (entry.marked || !marked_scopes.contains(key.first))
            {
                catalog.push_back(std::move(entry.info));
            }
        }
        return catalog;
    }

    std::string format_attribute_catalog(const std::vector<attribute_info>& attributes)
    {
        std::string out = "reflgen-attributes\t1\n";
        for (const attribute_info& info : attributes)
        {
            for (const std::string* field : {&info.kind, &info.scope, &info.name, &info.signature})
            {
                append_escaped(out, *field);
                out += '\t';
            }
            append_escaped(out, info.summary);
            out += '\n';
        }
        return out;
    }
} // namespace reflgen::generator

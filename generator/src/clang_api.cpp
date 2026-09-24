#include "clang_api.h"
#include <filesystem>
#include <set>

namespace reflgen::generator
{
    std::string take_string(CXString text)
    {
        const char* raw = clang_getCString(text);
        std::string result = raw != nullptr ? raw : "";
        clang_disposeString(text);
        return result;
    }

    std::string cursor_spelling(CXCursor cursor)
    {
        return take_string(clang_getCursorSpelling(cursor));
    }

    std::string strip_tag(std::string name)
    {
        for (const std::string_view tag : {"struct ", "class ", "enum ", "union "})
        {
            if (name.starts_with(tag))
            {
                return name.substr(tag.size());
            }
        }
        return name;
    }

    std::string type_name_of(CXCursor cursor)
    {
        return strip_tag(take_string(clang_getTypeSpelling(clang_getCursorType(cursor))));
    }

    bool is_record(CXCursorKind kind)
    {
        return kind == CXCursor_ClassDecl || kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl;
    }

    std::string normalize_path(const std::string& path)
    {
        if (path.empty())
        {
            return path;
        }
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path(path), error);
        std::string result = (error ? std::filesystem::path(path) : absolute).lexically_normal().generic_string();
        // "C:/out/" 처럼 끝의 구분자가 남으면 뒤에 붙이는 이름과 사이에 '//' 가 생긴다. 루트("C:/", "/")는 둔다.
        while (result.size() > 1 && result.back() == '/' && !result.ends_with(":/"))
        {
            result.pop_back();
        }
        return result;
    }

    source_position position_of(CXSourceLocation location)
    {
        CXFile file = nullptr;
        unsigned line = 0;
        unsigned column = 0;
        unsigned offset = 0;
        clang_getSpellingLocation(location, &file, &line, &column, &offset);
        source_position position;
        position.file = file != nullptr ? normalize_path(take_string(clang_getFileName(file))) : std::string();
        position.line = line;
        position.column = column;
        return position;
    }

    std::size_t offset_of(CXSourceLocation location)
    {
        CXFile file = nullptr;
        unsigned line = 0;
        unsigned column = 0;
        unsigned offset = 0;
        clang_getSpellingLocation(location, &file, &line, &column, &offset);
        return offset;
    }

    std::string file_of(CXCursor cursor)
    {
        return position_of(clang_getCursorLocation(cursor)).file;
    }

    CXFile file_handle_of(CXSourceLocation location)
    {
        CXFile file = nullptr;
        clang_getSpellingLocation(location, &file, nullptr, nullptr, nullptr);
        return file;
    }

    source_position position_in(CXTranslationUnit unit, CXFile file, std::size_t offset)
    {
        return position_of(clang_getLocationForOffset(unit, file, static_cast<unsigned>(offset)));
    }

    namespace
    {
        std::vector<token> tokenize(CXTranslationUnit unit, CXSourceRange range)
        {
            CXToken* raw = nullptr;
            unsigned count = 0;
            clang_tokenize(unit, range, &raw, &count);

            std::vector<token> tokens;
            tokens.reserve(count);
            for (unsigned i = 0; i < count; ++i)
            {
                token item;
                switch (clang_getTokenKind(raw[i]))
                {
                case CXToken_Punctuation:
                    item.kind = token_kind::punctuation;
                    break;
                case CXToken_Keyword:
                    item.kind = token_kind::keyword;
                    break;
                case CXToken_Identifier:
                    item.kind = token_kind::identifier;
                    break;
                case CXToken_Literal:
                    item.kind = token_kind::literal;
                    break;
                case CXToken_Comment:
                    item.kind = token_kind::comment;
                    break;
                }
                item.spelling = take_string(clang_getTokenSpelling(unit, raw[i]));
                const CXSourceRange extent = clang_getTokenExtent(unit, raw[i]);
                item.begin = offset_of(clang_getRangeStart(extent));
                item.end = offset_of(clang_getRangeEnd(extent));
                tokens.push_back(std::move(item));
            }
            if (raw != nullptr)
            {
                clang_disposeTokens(unit, raw, count);
            }
            return tokens;
        }
    } // namespace

    std::vector<token> tokenize_file(CXTranslationUnit unit, CXFile file)
    {
        std::size_t size = 0;
        if (file == nullptr || clang_getFileContents(unit, file, &size) == nullptr)
        {
            return {};
        }
        const CXSourceRange whole = clang_getRange(clang_getLocationForOffset(unit, file, 0),
                                                   clang_getLocationForOffset(unit, file, static_cast<unsigned>(size)));
        return tokenize(unit, whole);
    }

    std::vector<std::string> included_files(CXTranslationUnit unit)
    {
        std::set<std::string> files;
        clang_getInclusions(
            unit,
            [](CXFile file, CXSourceLocation*, unsigned depth, CXClientData data) {
                if (depth > 0) // 0 은 주 파일(생성기가 메모리에 만든 것)이다
                {
                    static_cast<std::set<std::string>*>(data)->insert(
                        normalize_path(take_string(clang_getFileName(file))));
                }
            },
            &files);
        return {files.begin(), files.end()};
    }
} // namespace reflgen::generator

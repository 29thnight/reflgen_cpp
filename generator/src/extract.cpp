#include "extract.h"
#include "attribute_scan.h"
#include "catalog.h"
#include "clang_api.h"
#include "class_scope.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace reflgen::generator
{
    namespace
    {
        constexpr std::string_view library_scope = "reflgen";

        // 생성기가 해석하고 스키마에는 옮기지 않는 지시어.
        bool is_directive(const scanned_attribute& attribute)
        {
            return attribute.scope == library_scope &&
                   (attribute.name == "reflect" || attribute.name == "ignore" || attribute.name == "attribute");
        }

        const scanned_attribute* find_directive(const std::vector<attribute_group>& groups, std::string_view name)
        {
            for (const attribute_group& group : groups)
            {
                for (const scanned_attribute& attribute : group.attributes)
                {
                    if (attribute.scope == library_scope && attribute.name == name)
                    {
                        return &attribute;
                    }
                }
            }
            return nullptr;
        }

        void append(std::vector<attribute_group>& target, std::vector<attribute_group> more)
        {
            target.insert(target.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        }

        // 파일 하나의 token 과 [[…]] 그룹. attribute 는 AST 에 남지 않으므로 offset 으로 선언과 잇는다.
        struct source_file
        {
            CXFile handle = nullptr;
            std::vector<token> tokens;
            std::vector<attribute_group> groups;
        };

        // 반영 선언이 차지하는 원본 구간(매크로를 편 자리 기준) — clang 오류가 그 안에 있는지 가른다.
        struct declaration_extent
        {
            std::string file;
            std::size_t begin = 0;
            std::size_t end = 0;
        };

        // clang 이 보고한 오류 하나.
        struct clang_error
        {
            source_position position; // 진단에 찍는 원본 위치
            file_offset expansion;    // 반영 선언 구간과 견주는 위치
            std::string message;
            bool fatal = false; // 파싱이 거기서 멈췄다
        };

        // 클래스 하나의 멤버를 도는 동안 쓰는 문맥.
        struct member_context
        {
            class_model& result;
            const qualifier_lookup& lookup;
            std::map<std::string, int> method_counts;
            std::string first_hidden_member; // friend 가 필요한지 가리려고 첫 비공개 멤버를 기억한다
        };

        class extractor
        {
          public:
            extractor(CXTranslationUnit unit, const extract_options& options, diagnostics& report)
                : unit_(unit), report_(report)
            {
                for (const std::string& header : options.headers)
                {
                    index_.emplace(header, models_.size());
                    models_.push_back({header, {}, {}});
                }
                scopes_.insert(std::string(library_scope));
                for (const std::string& scope : options.attribute_scopes)
                {
                    scopes_.insert(scope);
                }
                has_attribute_headers_ = !options.attribute_headers.empty();
            }

            void run() { visit_scope(clang_getTranslationUnitCursor(unit_)); }

            const std::vector<declaration_extent>& reflected_extents() const noexcept { return extents_; }

            // 선언 이름 앞에 [[reflgen::<directive>]] 가 있는가 — 카탈로그가 attribute 타입을 가를 때 쓴다.
            bool declares(CXCursor cursor, std::string_view directive)
            {
                return find_directive(groups_before_name(cursor), directive) != nullptr;
            }

            std::vector<header_model> take() { return std::move(models_); }

          private:
            header_model* model_for(CXCursor cursor)
            {
                const auto found = index_.find(file_of(cursor));
                return found != index_.end() ? &models_[found->second] : nullptr;
            }

            const source_file& source_of(CXCursor cursor)
            {
                const std::string path = file_of(cursor);
                auto found = sources_.find(path);
                if (found == sources_.end())
                {
                    source_file source;
                    source.handle = file_handle_of(clang_getCursorLocation(cursor));
                    source.tokens = tokenize_file(unit_, source.handle);
                    source.groups = scan_attribute_groups(source.tokens);
                    found = sources_.emplace(path, std::move(source)).first;
                }
                return found->second;
            }

            // 클래스·열거형·메서드: 선언 시작부터 이름 앞까지 — class [[x]] name, [[x]] int f().
            std::vector<attribute_group> groups_before_name(CXCursor cursor)
            {
                const source_file& source = source_of(cursor);
                return groups_between(source.groups, offset_of(clang_getRangeStart(clang_getCursorExtent(cursor))),
                                      offset_of(clang_getCursorLocation(cursor)));
            }

            // 이름 바로 뒤 — int x [[x]], enumerator [[x]].
            std::vector<attribute_group> groups_after_name(CXCursor cursor)
            {
                const source_file& source = source_of(cursor);
                const std::optional<std::size_t> next =
                    next_token_offset(source.tokens, offset_of(clang_getCursorLocation(cursor)));
                return next ? groups_run_at(source.tokens, source.groups, *next) : std::vector<attribute_group>{};
            }

            // 선언 범위 바로 앞의 그룹 — libclang 20 은 `[[a]] int x;` 의 범위를 int 부터 잡는다(22 는 [[ 부터).
            // 범위 안에서 찾는 쪽(groups_run_at·groups_before_name)과 겹치지 않으므로 둘 다 더하면 두 판 모두 된다.
            std::vector<attribute_group> groups_before_extent(CXCursor cursor)
            {
                const source_file& source = source_of(cursor);
                return groups_run_before(source.tokens, source.groups,
                                         offset_of(clang_getRangeStart(clang_getCursorExtent(cursor))));
            }

            // 필드: 선언 맨 앞(모든 declarator 공통)과 이름 바로 뒤(이 declarator 만).
            std::vector<attribute_group> field_groups(CXCursor field)
            {
                const source_file& source = source_of(field);
                std::vector<attribute_group> groups = groups_before_extent(field);
                append(groups, groups_run_at(source.tokens, source.groups,
                                             offset_of(clang_getRangeStart(clang_getCursorExtent(field)))));
                append(groups, groups_after_name(field));
                return groups;
            }

            std::vector<attribute_group> function_groups(CXCursor function)
            {
                std::vector<attribute_group> groups = groups_before_extent(function);
                append(groups, groups_before_name(function));
                append(groups, groups_after_name(function));
                return groups;
            }

            // 원본 header 없이는 생성 코드가 찾지 못하는 이름을 기록한다(한 번씩).
            static void note_external(const std::vector<std::string>& names, std::vector<std::string>& external)
            {
                for (const std::string& name : names)
                {
                    if (std::ranges::find(external, name) == external.end())
                    {
                        external.push_back(name);
                    }
                }
            }

            std::vector<attribute_use> uses_of(const std::vector<attribute_group>& groups, const source_file& source,
                                               const qualifier_lookup& lookup, std::vector<std::string>& external)
            {
                std::vector<attribute_use> uses;
                for (const attribute_group& group : groups)
                {
                    for (const scanned_attribute& attribute : group.attributes)
                    {
                        if (attribute.scope.empty() || !scopes_.contains(attribute.scope) || is_directive(attribute))
                        {
                            continue;
                        }
                        // 사용자 이름공간의 attribute 타입은 attribute header 로만 생성 코드에 보인다.
                        if (attribute.scope != library_scope && !has_attribute_headers_)
                        {
                            note_external({attribute.scope + "::" + attribute.name}, external);
                        }
                        if (attribute.has_arguments)
                        {
                            note_external(external_identifiers(source.tokens, attribute.arguments_begin,
                                                               attribute.arguments_end, lookup),
                                          external);
                        }
                        attribute_use use;
                        use.expression = attribute.scope + "::" + attribute.name;
                        // 인자 없는 attribute 는 표지 타입으로 본다 — reflgen::transient 처럼.
                        use.expression += attribute.has_arguments
                                              ? "(" +
                                                    argument_text(source.tokens, attribute.arguments_begin,
                                                                  attribute.arguments_end, lookup) +
                                                    ")"
                                              : "{}";
                        use.position = position_in(unit_, source.handle, attribute.begin);
                        uses.push_back(std::move(use));
                    }
                }
                return uses;
            }

            // 이름공간·클래스 안을 돌며 반영 대상을 찾는다.
            void visit_scope(CXCursor scope)
            {
                visit_children(scope, [&](CXCursor cursor, CXCursor) {
                    const CXCursorKind kind = clang_getCursorKind(cursor);
                    if (kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec)
                    {
                        visit_scope(cursor);
                    }
                    else if (clang_isCursorDefinition(cursor) && model_for(cursor) != nullptr)
                    {
                        visit_definition(cursor, kind);
                    }
                    return CXChildVisit_Continue;
                });
            }

            void visit_definition(CXCursor cursor, CXCursorKind kind)
            {
                switch (kind)
                {
                case CXCursor_ClassDecl:
                case CXCursor_StructDecl:
                    visit_class(cursor);
                    break;
                case CXCursor_EnumDecl:
                    visit_enum(cursor);
                    break;
                case CXCursor_UnionDecl:
                    visit_scope(cursor);
                    warn_if_reflected(cursor, "RG0005",
                                      "unions are not supported (a serializer cannot tell which member is active); "
                                      "describe '" +
                                          cursor_spelling(cursor) + "' with a custom serializer instead");
                    break;
                case CXCursor_ClassTemplate:
                    warn_if_reflected(cursor, "RG0005",
                                      "class templates are not supported by the generator yet; describe '" +
                                          cursor_spelling(cursor) + "' with an in-class reflect() instead");
                    break;
                default:
                    break;
                }
            }

            // [[reflgen::reflect]] 를 붙였지만 반영할 수 없는 선언 — 조용히 버리지 않고 알린다.
            void warn_if_reflected(CXCursor cursor, std::string_view code, const std::string& message)
            {
                if (find_directive(function_groups(cursor), "reflect") != nullptr)
                {
                    report_.warning(code, position_of(clang_getCursorLocation(cursor)), message + "; skipped");
                }
            }

            void visit_class(CXCursor cursor)
            {
                // 중첩 타입은 반영 여부와 무관하게 따로 찾는다.
                visit_scope(cursor);

                const std::vector<attribute_group> class_groups = groups_before_name(cursor);
                const scanned_attribute* reflect = find_directive(class_groups, "reflect");
                if (reflect == nullptr)
                {
                    return;
                }
                const source_position position = position_of(clang_getCursorLocation(cursor));
                const std::optional<std::vector<std::string>> namespaces = enclosing_namespaces(cursor);
                if (!namespaces)
                {
                    report_.warning("RG0005", position,
                                    "'" + cursor_spelling(cursor) +
                                        "' is in an anonymous namespace; its reflection cannot be specialized from a "
                                        "generated header; skipped");
                    return;
                }
                note_declaration(cursor, position);

                class_model result;
                result.qualified_name = type_name_of(cursor);
                result.name = cursor_spelling(cursor);
                result.class_key = clang_getCursorKind(cursor) == CXCursor_ClassDecl ? "class" : "struct";
                result.namespaces = *namespaces;
                result.position = position;
                result.bases = reflected_ancestors(clang_getCursorType(cursor));

                // 클래스 attribute 는 클래스 이름 앞에 있어 자기 멤버를 볼 수 없다 — 감싸는 클래스만 찾는다.
                const std::vector<class_scope> outer_scopes = enclosing_class_scopes(cursor);
                result.nested = !outer_scopes.empty();
                const qualifier_lookup outer_lookup = lookup_in(outer_scopes);
                const source_file& source = source_of(cursor);
                result.attributes = uses_of(class_groups, source, outer_lookup, result.external_names);
                if (reflect->has_arguments)
                {
                    result.schema_name =
                        argument_text(source.tokens, reflect->arguments_begin, reflect->arguments_end, outer_lookup);
                    note_external(external_identifiers(source.tokens, reflect->arguments_begin, reflect->arguments_end,
                                                       outer_lookup),
                                  result.external_names);
                }
                else
                {
                    result.schema_name = "\"" + result.qualified_name + "\"";
                }

                // 자기 멤버는 T:: 로 한정한다 — 생성 코드의 서술은 T 에 의존하는 템플릿이라, 의존 이름이어야 T 가
                // 완전해지는 사용 자리에서 찾는다(생성 header 는 원본 header 없이 전방 선언만 본다).
                std::vector<class_scope> member_scopes{scope_of(cursor)};
                member_scopes.front().qualifier = "T::";
                member_scopes.insert(member_scopes.end(), outer_scopes.begin(), outer_scopes.end());
                const qualifier_lookup member_lookup = lookup_in(member_scopes);
                member_context context{result, member_lookup, count_functions(cursor), {}};
                visit_members(cursor, context);

                if (!context.first_hidden_member.empty() && !befriends_access(cursor))
                {
                    report_.error("RG0002", position,
                                  "'" + result.qualified_name + "' reflects the non-public member '" +
                                      context.first_hidden_member +
                                      "'; add `friend struct reflgen::access;` to the class");
                }
                model_for(cursor)->classes.push_back(std::move(result));
            }

            // 이름별 멤버 함수 수 — 둘 이상이면 &T::name 이 모호하다(template 오버로드 포함).
            static std::map<std::string, int> count_functions(CXCursor cursor)
            {
                std::map<std::string, int> counts;
                visit_children(cursor, [&](CXCursor child, CXCursor) {
                    const CXCursorKind kind = clang_getCursorKind(child);
                    if (kind == CXCursor_CXXMethod || kind == CXCursor_ConversionFunction ||
                        kind == CXCursor_FunctionTemplate)
                    {
                        ++counts[cursor_spelling(child)];
                    }
                    return CXChildVisit_Continue;
                });
                return counts;
            }

            void visit_members(CXCursor cursor, member_context& context)
            {
                visit_children(cursor, [&](CXCursor child, CXCursor) {
                    switch (clang_getCursorKind(child))
                    {
                    case CXCursor_FieldDecl:
                        add_field(child, context);
                        break;
                    case CXCursor_CXXMethod:
                    case CXCursor_ConversionFunction:
                        add_method(child, context);
                        break;
                    case CXCursor_FunctionTemplate:
                        warn_if_reflected(child, "RG0005",
                                          "member function template '" + cursor_spelling(child) +
                                              "' cannot be reflected yet");
                        break;
                    case CXCursor_Constructor:
                    case CXCursor_Destructor:
                        warn_if_reflected(child, "RG0004", "constructors and destructors cannot be reflected");
                        break;
                    case CXCursor_VarDecl:
                        warn_if_reflected(child, "RG0004",
                                          "static data member '" + cursor_spelling(child) +
                                              "' is not reflected (only non-static data members are)");
                        break;
                    default:
                        break;
                    }
                    return CXChildVisit_Continue;
                });
            }

            void add_field(CXCursor field, member_context& context)
            {
                const std::vector<attribute_group> groups = field_groups(field);
                if (find_directive(groups, "ignore") != nullptr)
                {
                    return;
                }
                const std::string name = cursor_spelling(field);
                const source_position position = position_of(clang_getCursorLocation(field));
                if (name.empty() || clang_Cursor_isAnonymousRecordDecl(field))
                {
                    report_.warning("RG0004", position, "anonymous members cannot be reflected; skipped");
                    return;
                }
                const CXTypeKind kind = clang_getCursorType(field).kind;
                const std::string_view problem = clang_Cursor_isBitField(field) ? "bit-field"
                                                 : kind == CXType_LValueReference || kind == CXType_RValueReference
                                                     ? "reference member"
                                                     : "";
                if (!problem.empty())
                {
                    report_.warning("RG0004", position,
                                    std::string(problem) + " '" + name +
                                        "' cannot be reflected (no pointer to member); mark it [[reflgen::ignore]] to "
                                        "silence this warning");
                    return;
                }
                if (clang_getCXXAccessSpecifier(field) != CX_CXXPublic && context.first_hidden_member.empty())
                {
                    context.first_hidden_member = name;
                }

                field_model model;
                model.name = name;
                model.position = position_of(clang_getRangeStart(clang_getCursorExtent(field)));
                model.attributes = uses_of(groups, source_of(field), context.lookup, context.result.external_names);
                context.result.fields.push_back(std::move(model));
            }

            void add_method(CXCursor method, member_context& context)
            {
                const std::vector<attribute_group> groups = function_groups(method);
                if (find_directive(groups, "reflect") == nullptr)
                {
                    return;
                }
                const std::string name = cursor_spelling(method);
                const source_position position = position_of(clang_getCursorLocation(method));
                if (clang_CXXMethod_isStatic(method))
                {
                    report_.warning("RG0004", position,
                                    "static member function '" + name + "' cannot be reflected; skipped");
                    return;
                }
                if (context.method_counts.at(name) > 1)
                {
                    report_.error("RG0006", position,
                                  "'" + name + "' is overloaded; overloaded member functions cannot be reflected yet");
                    return;
                }
                if (clang_getCXXAccessSpecifier(method) != CX_CXXPublic && context.first_hidden_member.empty())
                {
                    context.first_hidden_member = name;
                }

                method_model model;
                model.name = name;
                model.position = position_of(clang_getRangeStart(clang_getCursorExtent(method)));
                model.attributes = uses_of(groups, source_of(method), context.lookup, context.result.external_names);
                // 이름 없는 매개변수는 빈 이름으로 남는다 — 선언에 없는 이름을 지어내지 않는다.
                const int count = clang_Cursor_getNumArguments(method);
                for (int i = 0; i < count; ++i)
                {
                    model.parameters.push_back(
                        cursor_spelling(clang_Cursor_getArgument(method, static_cast<unsigned>(i))));
                }
                context.result.methods.push_back(std::move(model));
            }

            void visit_enum(CXCursor cursor)
            {
                if (find_directive(groups_before_name(cursor), "reflect") == nullptr)
                {
                    return;
                }
                const source_position position = position_of(clang_getCursorLocation(cursor));
                if (!enclosing_namespaces(cursor))
                {
                    report_.warning("RG0005", position,
                                    "'" + cursor_spelling(cursor) + "' is in an anonymous namespace; skipped");
                    return;
                }
                note_declaration(cursor, position);

                enum_model result;
                result.qualified_name = type_name_of(cursor);
                result.name = cursor_spelling(cursor);
                result.namespaces = *enclosing_namespaces(cursor);
                result.position = position;
                result.nested = !enclosing_class_scopes(cursor).empty();
                result.scoped = clang_EnumDecl_isScoped(cursor) != 0;
                // 전방 선언은 원본과 같은 enum-base 를 적는다 — 적지 않은 비스코프드 열거형은 전방 선언할 수 없다.
                if (writes_enum_base(cursor))
                {
                    // std::uint8_t 같은 별칭은 전방 선언 자리에서 보이지 않을 수 있다 — 표준 타입 이름으로 적는다.
                    result.underlying_type = take_string(
                        clang_getTypeSpelling(clang_getCanonicalType(clang_getEnumDeclIntegerType(cursor))));
                }
                const qualifier_lookup no_lookup = [](std::string_view) { return std::string(); };
                std::vector<std::string> ignored_names;
                visit_children(cursor, [&](CXCursor child, CXCursor) {
                    if (clang_getCursorKind(child) == CXCursor_EnumConstantDecl)
                    {
                        result.enumerators.push_back(cursor_spelling(child));
                        if (!uses_of(groups_after_name(child), source_of(child), no_lookup, ignored_names).empty())
                        {
                            report_.warning("RG0004", position_of(clang_getCursorLocation(child)),
                                            "attributes on enumerators are not supported yet; ignored");
                        }
                    }
                    return CXChildVisit_Continue;
                });
                model_for(cursor)->enums.push_back(std::move(result));
            }

            // 반영 선언의 구간을 적는다(그 안의 clang 오류는 넘기지 않는다). clang 이 선언을 무효로 보면 밖의 오류(부모
            // 클래스, 멤버 타입) 때문에 잘못 읽었을 수 있다 — 부모를 건너뛰면 부모의 필드가 서술에서 빠진다.
            void note_declaration(CXCursor cursor, const source_position& position)
            {
                const CXSourceRange range = clang_getCursorExtent(cursor);
                const file_offset begin = expansion_of(clang_getRangeStart(range));
                const file_offset end = expansion_of(clang_getRangeEnd(range));
                extents_.push_back({begin.file, begin.offset, end.offset});
                if (clang_isInvalidDeclaration(cursor) != 0)
                {
                    report_.error("RG0102", position,
                                  "clang could not read '" + cursor_spelling(cursor) +
                                      "': an error in a declaration it depends on (a base class, a member type) made "
                                      "it invalid; the clang errors follow as notes");
                }
            }

            // enum 이름 뒤, 본문('{')이나 끝(';') 앞에 ':' 가 있으면 enum-base 를 적은 것이다.
            bool writes_enum_base(CXCursor cursor)
            {
                const source_file& source = source_of(cursor);
                const std::size_t name = offset_of(clang_getCursorLocation(cursor));
                for (const token& item : source.tokens)
                {
                    if (item.begin <= name || item.kind != token_kind::punctuation)
                    {
                        continue;
                    }
                    if (item.spelling == ":")
                    {
                        return true;
                    }
                    if (item.spelling == "{" || item.spelling == ";")
                    {
                        return false;
                    }
                }
                return false;
            }

            // 반영된 클래스인가 — [[reflgen::reflect]] 를 달았거나 reflgen 스키마를 돌려주는 static reflect() 레시피가
            // 있다. 다른 라이브러리의 reflect()(흔한 이름이다)는 라이브러리처럼 레시피로 치지 않는다.
            bool is_reflected(CXCursor record)
            {
                if (declares(record, "reflect"))
                {
                    return true;
                }
                bool has_recipe = false;
                visit_children(record, [&](CXCursor child, CXCursor) {
                    if (clang_getCursorKind(child) == CXCursor_CXXMethod && clang_CXXMethod_isStatic(child) != 0 &&
                        cursor_spelling(child) == "reflect")
                    {
                        const std::string result = take_string(
                            clang_getTypeSpelling(clang_getCanonicalType(clang_getCursorResultType(child))));
                        has_recipe = result.find("reflgen::type_schema<") != std::string::npos;
                        return CXChildVisit_Break;
                    }
                    return CXChildVisit_Continue;
                });
                return has_recipe;
            }

            // public 부모 가운데 반영된 것은 그대로, 반영하지 않은 것(CRTP 중간층, 서드파티 베이스)은 그 부모들로
            // 거슬러 올라가 가장 가까운 반영된 조상을 모은다. 타입으로 훑으므로 템플릿 특수화 부모도 치환된 모습으로
            // 본다. 반영된 조상은 이름공간 범위의 클래스라 생성 header 가 전방 선언할 수 있다.
            static void add_unique(std::vector<type_reference>& target, type_reference reference)
            {
                const auto same = [&](const type_reference& known) {
                    return known.qualified_name == reference.qualified_name;
                };
                if (std::ranges::find_if(target, same) == target.end())
                {
                    target.push_back(std::move(reference));
                }
            }

            static type_reference reference_to(CXCursor record, CXType type)
            {
                type_reference reference;
                reference.qualified_name = strip_tag(take_string(clang_getTypeSpelling(type)));
                reference.name = cursor_spelling(record);
                reference.class_key = clang_getCursorKind(record) == CXCursor_ClassDecl ? "class" : "struct";
                const std::optional<std::vector<std::string>> namespaces = enclosing_namespaces(record);
                if (namespaces && !namespaces->empty())
                {
                    reference.enclosing_namespace = namespaces->back();
                }
                reference.nested = !enclosing_class_scopes(record).empty();
                reference.templated = !clang_Cursor_isNull(clang_getSpecializedCursorTemplate(record));
                return reference;
            }

            std::vector<type_reference> reflected_ancestors(CXType type, int depth = 0)
            {
                std::vector<type_reference> result;
                if (depth > 32)
                {
                    return result;
                }
                struct visit_state
                {
                    extractor* self;
                    std::vector<type_reference>* result;
                    int depth;
                } state{this, &result, depth};
                clang_visitCXXBaseClasses(
                    type,
                    [](CXCursor base, CXClientData data) -> CXVisitorResult {
                        auto& state = *static_cast<visit_state*>(data);
                        if (clang_getCXXAccessSpecifier(base) != CX_CXXPublic)
                        {
                            return CXVisit_Continue;
                        }
                        const CXType base_type = clang_getCanonicalType(clang_getCursorType(base));
                        const CXCursor declaration = clang_getTypeDeclaration(base_type);
                        // 표준 라이브러리 등 시스템 header 의 부모는 반영 대상이 아니고 그 위도 볼 것이 없다.
                        if (clang_Location_isInSystemHeader(clang_getCursorLocation(declaration)) != 0)
                        {
                            return CXVisit_Continue;
                        }
                        if (state.self->is_reflected(declaration))
                        {
                            add_unique(*state.result, reference_to(declaration, base_type));
                        }
                        else
                        {
                            for (type_reference& ancestor : state.self->reflected_ancestors(base_type, state.depth + 1))
                            {
                                add_unique(*state.result, std::move(ancestor));
                            }
                        }
                        return CXVisit_Continue;
                    },
                    &state);
                return result;
            }

            CXTranslationUnit unit_;
            diagnostics& report_;
            std::vector<header_model> models_;
            std::map<std::string, std::size_t> index_;
            std::map<std::string, source_file> sources_;
            std::set<std::string> scopes_;
            bool has_attribute_headers_ = false;
            std::vector<declaration_extent> extents_;
        };

        std::vector<clang_error> clang_errors_of(CXTranslationUnit unit)
        {
            std::vector<clang_error> errors;
            const unsigned count = clang_getNumDiagnostics(unit);
            for (unsigned i = 0; i < count; ++i)
            {
                CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
                const CXDiagnosticSeverity level = clang_getDiagnosticSeverity(diagnostic);
                if (level >= CXDiagnostic_Error)
                {
                    const CXSourceLocation location = clang_getDiagnosticLocation(diagnostic);
                    errors.push_back({position_of(location), expansion_of(location),
                                      take_string(clang_getDiagnosticSpelling(diagnostic)),
                                      level == CXDiagnostic_Fatal});
                }
                clang_disposeDiagnostic(diagnostic);
            }
            return errors;
        }

        // clang 오류를 반영 선언과 가른다. MSVC 로만 빌드하는 코드를 clang 으로 읽으면 반영과 무관한 곳에서 오류가
        // 난다(__FUNCSIG__ 표기를 못 박은 static_assert, clang 이 상수 식으로 받지 않는 enum 캐스트 등). 그것으로 생성을
        // 멈추면 그런 코드베이스에서는 생성기를 쓸 수 없다 — 생성 코드는 반영 선언의 이름과 attribute 만 옮기고,
        // 컴파일은 사용자의 컴파일러가 원본을 읽어 한다. 반영 선언 안의 오류는 clang 이 그 선언을 잘못 읽었을 수
        // 있으므로(멤버가 빠지거나 바뀐다) 오류다. 치명 오류는 파싱이 멈췄다는 뜻이라 늘 오류다.
        //
        // 생성이 실패하면(반영 선언이 무효였거나 안에 오류가 있으면) 넘긴 오류도 하나씩 알린다 — 원인이 거기 있을 수
        // 있다. 성공하면 수와 첫 오류만 알린다.
        void report_clang_errors(const std::vector<clang_error>& errors, const std::vector<declaration_extent>& extents,
                                 diagnostics& report)
        {
            std::vector<const clang_error*> ignored;
            for (const clang_error& error : errors)
            {
                const bool inside = std::ranges::any_of(extents, [&](const declaration_extent& extent) {
                    return extent.file == error.expansion.file && extent.begin <= error.expansion.offset &&
                           error.expansion.offset <= extent.end;
                });
                if (error.fatal || inside)
                {
                    report.error("RG0100", error.position, "clang: " + error.message);
                }
                else
                {
                    ignored.push_back(&error);
                }
            }
            if (ignored.empty())
            {
                return;
            }
            if (report.has_errors())
            {
                for (const clang_error* error : ignored)
                {
                    report.note("RG0101", error->position, "clang: " + error->message);
                }
                return;
            }
            const source_position& where = ignored.front()->position;
            report.note("RG0101", {},
                        std::to_string(ignored.size()) + (ignored.size() == 1 ? " clang error" : " clang errors") +
                            " outside the reflected declarations did not affect the generated code (first: " +
                            where.file + "(" + std::to_string(where.line) + "): " + ignored.front()->message + ")");
        }

        // "-std=c++23" 의 순위. 알 수 없는 표기는 0.
        int standard_rank(std::string_view argument)
        {
            const std::size_t plus = argument.find("++");
            if (plus == std::string_view::npos)
            {
                return 0;
            }
            const std::string_view version = argument.substr(plus + 2);
            for (const auto& [alias, rank] : {std::pair{"2a", 20}, std::pair{"2b", 23}, std::pair{"2c", 26}})
            {
                if (version == alias)
                {
                    return rank;
                }
            }
            int value = 0;
            for (const char c : version)
            {
                if (c < '0' || c > '9')
                {
                    return 0;
                }
                value = value * 10 + (c - '0');
            }
            constexpr int century = 100;
            return value >= 98 ? value - century : value; // 98 은 03 보다 낮다
        }

        // 빌드 시스템은 표준을 여러 곳(CXX_STANDARD, compile features, 사용자 인자)에서 가져와 -std= 를
        // 여러 번 줄 수 있다. CMake 가 target 의 표준을 그중 최댓값으로 정하듯 가장 높은 것 하나만 남긴다.
        std::vector<std::string> clang_arguments_for(const extract_options& options)
        {
            // -fparse-all-comments: 카탈로그가 `//` 주석도 attribute 설명으로 쓴다(기본은 doc 주석만 붙는다).
            // -ferror-limit=0: 오류가 많아도 끝까지 파싱한다 — 반영 선언 밖의 오류는 넘기므로(report_clang_errors)
            // 한도에서 멈추면 뒤에 오는 반영 선언을 놓친다.
            // -fbracket-depth=4096: 식 중첩 한도는 MSVC 에 없고 clang 은 넘으면 치명 오류로 파싱을 멈춘다. 기본값이
            // libclang 판마다 달라서(20 은 256, 22 는 2048) 생성기를 빌드한 VS 에 따라 같은 코드가 갈렸다 — 값 257 개를
            // 한 fold 로 펴는 enum 스캔이 20 에서 멈췄다. 판과 무관하게 넉넉히 준다(사용자 인자가 뒤에 와서 덮는다).
            std::vector<std::string> arguments = {"-x",
                                                  "c++",
                                                  "-Wno-unknown-attributes",
                                                  "-fparse-all-comments",
                                                  "-ferror-limit=0",
                                                  "-fbracket-depth=4096"};
            if (!options.resource_directory.empty())
            {
                // 내장 header 를 빌드 시스템이 주는 -isystem(MSVC·Windows SDK include)보다 먼저 찾는다 — clang-cl 의
                // 순서다. clang 은 -isystem 을 내장 header 보다 먼저 찾으므로 두지 않으면 MSVC 의 xmmintrin.h 가 이겨
                // __m128 이 clang 이 아는 타입과 달라진다.
                arguments.push_back("-resource-dir");
                arguments.push_back(options.resource_directory);
                arguments.push_back("-isystem");
                arguments.push_back(options.resource_directory + "/include");
            }
            std::string standard = "-std=c++20";
            bool has_standard = false;
            for (const std::string& argument : options.clang_arguments)
            {
                if (!argument.starts_with("-std="))
                {
                    arguments.push_back(argument);
                }
                else if (!has_standard || standard_rank(argument) > standard_rank(standard))
                {
                    standard = argument;
                    has_standard = true;
                }
            }
            arguments.push_back(standard);
            return arguments;
        }
    } // namespace

    std::string generated_header_name(const std::string& header_path)
    {
        return std::filesystem::path(header_path).stem().string() + ".reflgen.h";
    }

    extract_result extract(const extract_options& options, diagnostics& report)
    {
        std::set<std::string> seen;
        for (const std::string& header : options.headers)
        {
            if (!seen.insert(header).second)
            {
                report.error("RG0001", {header, 1, 1}, "header is listed more than once");
                return {};
            }
        }

        // 모든 header 를 include 하는 가상의 TU 하나로 한 번에 파싱한다(메모리에만 있는 파일). 원본 header 는
        // 생성 파일을 include 하지 않으므로 생성 결과가 파싱에 끼지 않는다.
        const std::string main_path = options.output_directory + "/reflgen_" + options.module_name + ".parse.cpp";
        std::string main_text;
        for (const std::string& header : options.headers)
        {
            main_text += "#include \"" + header + "\"\n";
        }
        std::vector<CXUnsavedFile> unsaved;
        unsaved.push_back({main_path.c_str(), main_text.c_str(), static_cast<unsigned long>(main_text.size())});

        const std::vector<std::string> arguments = clang_arguments_for(options);
        std::vector<const char*> argv;
        for (const std::string& argument : arguments)
        {
            argv.push_back(argument.c_str());
        }

        const index_handle index(clang_createIndex(0, 0));
        CXTranslationUnit raw_unit = nullptr;
        const CXErrorCode code = clang_parseTranslationUnit2(
            index.get(), main_path.c_str(), argv.data(), static_cast<int>(argv.size()), unsaved.data(),
            static_cast<unsigned>(unsaved.size()), CXTranslationUnit_SkipFunctionBodies, &raw_unit);
        const translation_unit_handle unit(raw_unit);
        if (code != CXError_Success || !unit)
        {
            report.error("RG0001", {},
                         "libclang could not parse the headers (error code " + std::to_string(static_cast<int>(code)) +
                             ")");
            return {};
        }

        extractor walker(unit.get(), options, report);
        walker.run();
        report_clang_errors(clang_errors_of(unit.get()), walker.reflected_extents(), report);

        std::set<std::string> scopes(options.attribute_scopes.begin(), options.attribute_scopes.end());
        scopes.insert("reflgen");
        const directive_query declares = [&walker](CXCursor cursor, std::string_view directive) {
            return walker.declares(cursor, directive);
        };
        extract_result result{walker.take(), included_files(unit.get()),
                              collect_attribute_catalog(unit.get(), scopes, declares)};
        // 메모리에만 있는 주 파일은 디스크에 없다. 이 실행의 출력이 의존에 끼면 빌드 도구가 순환 의존으로 멈춘다.
        std::set<std::string> outputs = {main_path};
        for (const std::string& header : options.headers)
        {
            outputs.insert(options.output_directory + "/" + generated_header_name(header));
        }
        std::erase_if(result.dependencies, [&](const std::string& path) { return outputs.contains(path); });
        return result;
    }
} // namespace reflgen::generator

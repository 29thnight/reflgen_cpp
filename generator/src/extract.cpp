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
            }

            void run() { visit_scope(clang_getTranslationUnitCursor(unit_)); }

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

            // 필드: 선언 맨 앞(모든 declarator 공통)과 이름 바로 뒤(이 declarator 만).
            std::vector<attribute_group> field_groups(CXCursor field)
            {
                const source_file& source = source_of(field);
                std::vector<attribute_group> groups = groups_run_at(
                    source.tokens, source.groups, offset_of(clang_getRangeStart(clang_getCursorExtent(field))));
                append(groups, groups_after_name(field));
                return groups;
            }

            std::vector<attribute_group> function_groups(CXCursor function)
            {
                std::vector<attribute_group> groups = groups_before_name(function);
                append(groups, groups_after_name(function));
                return groups;
            }

            std::vector<attribute_use> uses_of(const std::vector<attribute_group>& groups, const source_file& source,
                                               const qualifier_lookup& lookup)
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

                class_model result;
                result.qualified_name = type_name_of(cursor);
                result.namespaces = *namespaces;
                result.position = position;

                // 클래스 attribute 는 클래스 이름 앞에 있어 자기 멤버를 볼 수 없다 — 감싸는 클래스만 찾는다.
                const std::vector<class_scope> outer_scopes = enclosing_class_scopes(cursor);
                const qualifier_lookup outer_lookup = lookup_in(outer_scopes);
                const source_file& source = source_of(cursor);
                result.attributes = uses_of(class_groups, source, outer_lookup);
                result.schema_name = reflect->has_arguments ? argument_text(source.tokens, reflect->arguments_begin,
                                                                            reflect->arguments_end, outer_lookup)
                                                            : "\"" + result.qualified_name + "\"";

                std::vector<class_scope> member_scopes{scope_of(cursor)};
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
                    case CXCursor_CXXBaseSpecifier:
                        if (clang_getCXXAccessSpecifier(child) == CX_CXXPublic)
                        {
                            context.result.bases.push_back(strip_tag(take_string(
                                clang_getTypeSpelling(clang_getCanonicalType(clang_getCursorType(child))))));
                        }
                        break;
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
                model.attributes = uses_of(groups, source_of(field), context.lookup);
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
                model.attributes = uses_of(groups, source_of(method), context.lookup);
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

                enum_model result;
                result.qualified_name = type_name_of(cursor);
                result.position = position;
                const qualifier_lookup no_lookup = [](std::string_view) { return std::string(); };
                visit_children(cursor, [&](CXCursor child, CXCursor) {
                    if (clang_getCursorKind(child) == CXCursor_EnumConstantDecl)
                    {
                        result.enumerators.push_back(cursor_spelling(child));
                        if (!uses_of(groups_after_name(child), source_of(child), no_lookup).empty())
                        {
                            report_.warning("RG0004", position_of(clang_getCursorLocation(child)),
                                            "attributes on enumerators are not supported yet; ignored");
                        }
                    }
                    return CXChildVisit_Continue;
                });
                model_for(cursor)->enums.push_back(std::move(result));
            }

            CXTranslationUnit unit_;
            diagnostics& report_;
            std::vector<header_model> models_;
            std::map<std::string, std::size_t> index_;
            std::map<std::string, source_file> sources_;
            std::set<std::string> scopes_;
        };

        void report_clang_diagnostics(CXTranslationUnit unit, diagnostics& report)
        {
            const unsigned count = clang_getNumDiagnostics(unit);
            for (unsigned i = 0; i < count; ++i)
            {
                CXDiagnostic diagnostic = clang_getDiagnostic(unit, i);
                const CXDiagnosticSeverity level = clang_getDiagnosticSeverity(diagnostic);
                if (level >= CXDiagnostic_Error)
                {
                    report.error("RG0100", position_of(clang_getDiagnosticLocation(diagnostic)),
                                 "clang: " + take_string(clang_getDiagnosticSpelling(diagnostic)));
                }
                clang_disposeDiagnostic(diagnostic);
            }
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
        std::vector<std::string> clang_arguments_for(const extract_options& options, const std::string& stub_directory)
        {
            // -fparse-all-comments: 카탈로그가 `//` 주석도 attribute 설명으로 쓴다(기본은 doc 주석만 붙는다).
            std::vector<std::string> arguments = {"-x", "c++", "-Wno-unknown-attributes", "-fparse-all-comments",
                                                  "-I" + stub_directory};
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

        // 생성 파일은 빈 stub 으로 대신한 채 파싱한다. 옛 생성물이 지금의 header 와 어긋나면 파싱이 깨져
        // 다시 생성할 수 없게 되는 것(닭과 달걀)을 막고, 처음 실행이라 파일이 아직 없어도 include 가
        // 풀린다. stub 디렉터리는 include 경로 맨 앞이라 진짜 생성물보다 먼저 찾아진다.
        // 이미 깨져 본 길: 메모리상 빈 파일(unsaved file)로 대신하면 Windows 에서 include 탐색이 그 가상
        // 파일을 찾지 못했다('file not found').
        std::string write_stubs(const extract_options& options)
        {
            const std::string stub_directory = options.output_directory + "/.reflgen-stubs";
            std::filesystem::create_directories(stub_directory);
            for (const std::string& header : options.headers)
            {
                std::ofstream stub(std::filesystem::path(stub_directory + "/" + generated_header_name(header)),
                                   std::ios::binary | std::ios::trunc);
            }
            return stub_directory;
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

        // 모든 header 를 include 하는 가상의 TU 하나로 한 번에 파싱한다. 이 주 파일은 stub 디렉터리에
        // 둔다 — MSVC 호환 모드의 clang 은 "…" include 를 -I 보다 먼저 include 스택에 있는 모든 파일의
        // 디렉터리에서 찾아서, 주 파일이 출력 디렉터리에 있으면 stub 대신 옛 생성 파일이 잡힌다.
        const std::string stub_directory = write_stubs(options);
        const std::string main_path = stub_directory + "/reflgen_" + options.module_name + ".parse.cpp";
        std::string main_text;
        for (const std::string& header : options.headers)
        {
            main_text += "#include \"" + header + "\"\n";
        }
        std::vector<CXUnsavedFile> unsaved;
        unsaved.push_back({main_path.c_str(), main_text.c_str(), static_cast<unsigned long>(main_text.size())});

        const std::vector<std::string> arguments = clang_arguments_for(options, stub_directory);
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

        report_clang_diagnostics(unit.get(), report);
        extractor walker(unit.get(), options, report);
        walker.run();

        std::set<std::string> scopes(options.attribute_scopes.begin(), options.attribute_scopes.end());
        scopes.insert("reflgen");
        const directive_query declares = [&walker](CXCursor cursor, std::string_view directive) {
            return walker.declares(cursor, directive);
        };
        extract_result result{walker.take(), included_files(unit.get()),
                              collect_attribute_catalog(unit.get(), scopes, declares)};
        // stub 은 실행마다 새로 쓰이므로 의존에 넣으면 빌드가 매번 다시 돈다. 이 실행의 출력이 의존에
        // 끼면 빌드 도구가 순환 의존으로 멈춘다.
        std::set<std::string> outputs;
        for (const std::string& header : options.headers)
        {
            outputs.insert(options.output_directory + "/" + generated_header_name(header));
        }
        std::erase_if(result.dependencies, [&](const std::string& path) {
            return path.starts_with(stub_directory + "/") || outputs.contains(path);
        });
        return result;
    }
} // namespace reflgen::generator

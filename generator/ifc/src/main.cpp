// IFC supplies semantic declarations; optional MSVC /P metadata preserves tool attributes.
#include "emit.h"
#include "metadata.h"
#include "prsm.h"
#include <ifc/reader.hxx>
#include <ifc/tooling.hxx>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

void ifc_assert(const char* text, const char*, int)
{
    throw std::runtime_error(std::string("invalid IFC: ") + text);
}

namespace
{
    using namespace ifc;
    using namespace ifc::symbolic;
    using namespace reflgen::generator;

    struct diagnostic_failure : std::runtime_error
    {
        source_position source;
        diagnostic_failure(source_position location, const std::string& message)
            : std::runtime_error(message), source(std::move(location))
        {
        }
    };

    bool has(BasicSpecifiers flags, BasicSpecifiers bit)
    {
        return (to_underlying(flags) & to_underlying(bit)) != 0;
    }

    bool identifier(std::string_view text)
    {
        if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_'))
            return false;
        return std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }

    struct extractor
    {
        Reader& reader;
        header_model model;
        module_metadata* metadata = nullptr;
        std::set<std::string> scopes{"reflgen"};
        bool prsm = false;
        std::vector<class_model> exports;

        bool binding_groups(const std::vector<attribute_group>& groups)
        {
            if (!metadata)
                return false;
            for (const auto name : {"interop", "lifetime", "property_get", "property_set", "effect", "owner"})
                if (metadata->directive(groups, name))
                    return true;
            return false;
        }

        void reject_binding_location(DeclIndex index)
        {
            reader.visit_with_index(index, [&](auto, const auto& declaration) {
                if constexpr (requires {
                                  declaration.identity.locus;
                                  declaration.identity.name;
                              })
                {
                    std::string spelling;
                    if constexpr (std::is_same_v<decltype(declaration.identity.name), NameIndex>)
                        spelling = name(declaration.identity.name);
                    else if constexpr (std::is_same_v<decltype(declaration.identity.name), TextOffset>)
                        spelling = reader.get(declaration.identity.name);
                    if (binding_groups(attributes(declaration.identity.locus, spelling, true)))
                        fail(declaration.identity.locus,
                             "PrSM directives require a class and ordinary nonstatic method");
                }
            });
        }

        declaration_type scalar_type(TypeIndex index, unsigned depth = 0)
        {
            declaration_type result;
            if (depth > 32 || index_like::null(index))
                return result;
            if (const auto* alias = reader.get_if<DesignatedType>(index))
            {
                if (alias->decl.sort() == DeclSort::Alias)
                    return scalar_type(reader.get<AliasDecl>(alias->decl).aliasee, depth + 1);
                return result;
            }
            if (const auto* qualified = reader.get_if<QualifiedType>(index))
            {
                if ((to_underlying(qualified->qualifiers) & ~0x03) != 0)
                    return result;
                result = scalar_type(qualified->unqualified_type, depth + 1);
                result.is_const = (to_underlying(qualified->qualifiers) & to_underlying(Qualifier::Const)) != 0;
                result.is_volatile = (to_underlying(qualified->qualifiers) & to_underlying(Qualifier::Volatile)) != 0;
                return result;
            }
            const auto* fundamental = reader.get_if<FundamentalType>(index);
            if (!fundamental)
                return result;
            const auto& f = *fundamental;
            if (f.sign != TypeSign::Plain && f.sign != TypeSign::Signed && f.sign != TypeSign::Unsigned)
                return result;
            if (f.basis == TypeBasis::Void)
            {
                result.kind = "void";
                result.canonical_spelling = "void";
                result.size_bytes = 0;
            }
            else if (f.basis == TypeBasis::Bool)
            {
                result.kind = "bool";
                result.canonical_spelling = "bool";
                result.size_bytes = 1;
            }
            else if (f.basis == TypeBasis::Float && f.precision == TypePrecision::Default)
            {
                result.kind = "floating_point";
                result.canonical_spelling = "float";
                result.size_bytes = 4;
            }
            else if (f.basis == TypeBasis::Double && f.precision == TypePrecision::Default)
            {
                result.kind = "floating_point";
                result.canonical_spelling = "double";
                result.size_bytes = 8;
            }
            else if (f.basis == TypeBasis::Int || (f.basis == TypeBasis::Char && f.sign != TypeSign::Plain))
            {
                std::string name;
                long long size = -1;
                if (f.basis == TypeBasis::Char || f.precision == TypePrecision::Bit8)
                {
                    name = "char";
                    size = 1;
                }
                else if (f.precision == TypePrecision::Short || f.precision == TypePrecision::Bit16)
                {
                    name = "short";
                    size = 2;
                }
                else if (f.precision == TypePrecision::Default || f.precision == TypePrecision::Bit32)
                {
                    name = "int";
                    size = 4;
                }
                else if (f.precision == TypePrecision::Long)
                {
                    name = "long";
                    size = 4;
                }
                else if (f.precision == TypePrecision::Bit64)
                {
                    name = "long long";
                    size = 8;
                }
                if (size > 0)
                {
                    result.kind = f.sign == TypeSign::Unsigned ? "unsigned_integer" : "signed_integer";
                    result.canonical_spelling =
                        f.sign == TypeSign::Unsigned ? "unsigned " + name : (size == 1 ? "signed char" : name);
                    result.size_bytes = size;
                }
            }
            result.spelling = result.canonical_spelling;
            return result;
        }

        void prsm_members(const ScopeDecl& scope, class_model& owner)
        {
            const auto* body = reader.try_get(scope.initializer);
            if (!body)
                return;
            std::map<std::string, unsigned> counts;
            for (const auto& entry : reader.sequence(*body))
                if (entry.index.sort() == DeclSort::Method)
                    ++counts[name(reader.get<NonStaticMemberFunctionDecl>(entry.index).identity.name)];
                else if (entry.index.sort() == DeclSort::Function)
                    ++counts[name(reader.get<FunctionDecl>(entry.index).identity.name)];
            for (const auto& entry : reader.sequence(*body))
            {
                if (entry.index.sort() != DeclSort::Method)
                {
                    reject_binding_location(entry.index);
                    continue;
                }
                const auto& method = reader.get<NonStaticMemberFunctionDecl>(entry.index);
                const auto member_name = name(method.identity.name);
                const auto groups = attributes(method.identity.locus, member_name, true);
                if (!binding_groups(groups))
                    continue;
                if (!identifier(member_name))
                    fail(method.identity.locus, "unsupported PrSM operator/conversion");
                const auto* type = reader.get_if<MethodType>(method.type);
                if (!type)
                    fail(method.identity.locus, "unknown PrSM IFC method type");
                method_model result;
                result.name = member_name;
                result.owner = owner.qualified_name;
                result.position = position(method.identity.locus);
                result.access = method.access == Access::Public ? "public" : "private";
                result.attributes = metadata->uses(groups, scopes, {}, true);
                result.is_const = (to_underlying(type->traits) & to_underlying(FunctionTypeTraits::Const)) != 0;
                result.qualifiers_known = (to_underlying(type->traits) & ~0x0f) == 0;
                result.is_volatile = (to_underlying(type->traits) & to_underlying(FunctionTypeTraits::Volatile)) != 0;
                result.ref_qualifier = (to_underlying(type->traits) & (to_underlying(FunctionTypeTraits::Lvalue) |
                                                                       to_underlying(FunctionTypeTraits::Rvalue)))
                                           ? "unsupported"
                                           : "none";
                result.calling_convention =
                    type->convention == CallingConvention::Cdecl || type->convention == CallingConvention::This
                        ? "c"
                        : "unsupported";
                result.exception_specification = type->eh_spec.sort == NoexceptSort::None   ? "none"
                                                 : type->eh_spec.sort == NoexceptSort::True ? "basic_noexcept"
                                                                                            : "unsupported";
                result.is_deleted = (to_underlying(method.traits) & to_underlying(FunctionTraits::Deleted)) != 0;
                result.is_consteval = (to_underlying(method.traits) & to_underlying(FunctionTraits::Immediate)) != 0;
                result.declaration_specifiers_known =
                    (to_underlying(method.traits) &
                     ~(to_underlying(FunctionTraits::Inline) | to_underlying(FunctionTraits::Constexpr) |
                       to_underlying(FunctionTraits::Virtual) | to_underlying(FunctionTraits::NoReturn) |
                       to_underlying(FunctionTraits::PureVirtual) | to_underlying(FunctionTraits::Deleted) |
                       to_underlying(FunctionTraits::Immediate) | to_underlying(FunctionTraits::Final) |
                       to_underlying(FunctionTraits::Override))) == 0;
                result.is_overloaded = counts[member_name] > 1;
                result.return_type = scalar_type(type->target);
                const auto parameter = [&](TypeIndex parameter_type) {
                    result.parameter_declarations.push_back({{}, scalar_type(parameter_type), result.position});
                };
                if (!index_like::null(type->source))
                {
                    if (const auto* tuple = reader.get_if<TupleType>(type->source))
                        for (const auto item : reader.sequence(*tuple))
                            parameter(item);
                    else if (scalar_type(type->source).kind != "void")
                        parameter(type->source);
                }
                owner.methods.push_back(std::move(result));
            }
        }

        std::vector<attribute_group> attributes(SourceLocation locus, std::string_view member, bool type = false)
        {
            if (!metadata)
                return {};
            try
            {
                return metadata->declaration(position(locus), member, type);
            }
            catch (const std::exception& e)
            {
                fail(locus, e.what());
            }
        }

        bool ignored(const std::vector<attribute_group>& groups)
        {
            return metadata && metadata->directive(groups, "ignore");
        }

        std::vector<attribute_use> uses(const std::vector<attribute_group>& groups, const qualifier_lookup& lookup = {})
        {
            return metadata ? metadata->uses(groups, scopes, lookup) : std::vector<attribute_use>{};
        }

        std::string name(NameIndex index)
        {
            if (index.sort() == NameSort::Identifier)
                return reader.get(TextOffset(to_underlying(index.index())));
            return {}; // operators, conversions and specializations are outside this first backend.
        }

        source_position position(SourceLocation locus)
        {
            if (index_like::null(locus.line))
                return {};
            const auto& line = reader.get(locus.line);
            std::string file;
            if (line.file.sort() == NameSort::SourceFile)
                file = reader.get(reader.get<SourceFileName>(line.file).name);
            std::replace(file.begin(), file.end(), '\\', '/');
            return {file, static_cast<unsigned>(to_underlying(line.line)),
                    static_cast<unsigned>(to_underlying(locus.column))};
        }

        [[noreturn]] void fail(SourceLocation locus, const std::string& message)
        {
            throw diagnostic_failure(position(locus), message);
        }

        void members(const ScopeDecl& scope, class_model& result)
        {
            const auto* body = reader.try_get(scope.initializer);
            if (!body)
                return;
            std::map<std::string, int> method_counts;
            std::set<std::string> constants;
            for (const auto& entry : reader.sequence(*body))
                if (entry.index.sort() == DeclSort::Variable)
                    constants.insert(name(reader.get<VariableDecl>(entry.index).identity.name));
            const qualifier_lookup lookup = [&constants](std::string_view value) {
                return constants.contains(std::string(value)) ? "T::" : "";
            };
            for (const auto& entry : reader.sequence(*body))
            {
                if (entry.index.sort() == DeclSort::Field)
                {
                    const auto& field = reader.get<FieldDecl>(entry.index);
                    if (field.access != Access::Public)
                        continue;
                    const std::string field_name = reader.get(field.identity.name);
                    const auto groups = attributes(field.identity.locus, field_name);
                    if (ignored(groups))
                        continue;
                    if (field.type.sort() == TypeSort::LvalueReference ||
                        field.type.sort() == TypeSort::RvalueReference)
                        fail(field.identity.locus, "reference field is unsupported: " + result.qualified_name);
                    if (!identifier(field_name))
                        fail(field.identity.locus, "anonymous field is unsupported: " + result.qualified_name);
                    result.fields.push_back({field_name, position(field.identity.locus), uses(groups, lookup)});
                }
                else if (entry.index.sort() == DeclSort::Bitfield)
                {
                    const auto& bit = reader.get<BitfieldDecl>(entry.index);
                    if (bit.access == Access::Public &&
                        !ignored(attributes(bit.identity.locus, reader.get(bit.identity.name))))
                        fail(bit.identity.locus, "public bit-field is unsupported: " + result.qualified_name);
                }
                else if (entry.index.sort() == DeclSort::Method)
                {
                    const auto& method = reader.get<NonStaticMemberFunctionDecl>(entry.index);
                    const std::string method_name = name(method.identity.name);
                    if (!identifier(method_name))
                        continue;
                    const auto groups = attributes(method.identity.locus, method_name, true);
                    if (ignored(groups))
                        continue;
                    if (identifier(method_name))
                        ++method_counts[method_name];
                    if (method.access == Access::Public && identifier(method_name))
                    {
                        result.methods.push_back(
                            {method_name, {}, position(method.identity.locus), uses(groups, lookup)});
                    }
                }
                else if (entry.index.sort() == DeclSort::Scope)
                {
                    const auto& nested = reader.get<ScopeDecl>(entry.index);
                    if (nested.access == Access::Public)
                        fail(nested.identity.locus, "public nested type is unsupported: " + result.qualified_name);
                }
                else if (entry.index.sort() == DeclSort::Enumeration)
                {
                    const auto& nested = reader.get<EnumerationDecl>(entry.index);
                    if (nested.access == Access::Public)
                        fail(nested.identity.locus, "public nested enum is unsupported: " + result.qualified_name);
                }
            }
            for (const auto& [method, count] : method_counts)
                if (count > 1 &&
                    std::ranges::any_of(result.methods, [&method](const auto& item) { return item.name == method; }))
                    fail(scope.identity.locus,
                         "overloaded method is unsupported: " + result.qualified_name + "::" + method);
        }

        const ScopeDecl* crtp_reflected_base(TypeIndex index, const ScopeDecl& derived)
        {
            const auto* syntax = reader.get_if<SyntacticType>(index);
            const auto* template_id = syntax ? reader.get_if<TemplateIdExpr>(syntax->expr) : nullptr;
            const auto* named = template_id ? reader.get_if<NamedDeclExpr>(template_id->primary_template) : nullptr;
            const auto* declaration = named ? reader.get_if<TemplateDecl>(named->decl) : nullptr;
            const auto* arguments = template_id ? reader.get_if<TupleExpr>(template_id->arguments) : nullptr;
            const auto* chart = declaration ? reader.get_if<UnilevelChart>(declaration->chart) : nullptr;
            const auto* generic = declaration ? reader.get_if<ScopeDecl>(declaration->entity.decl) : nullptr;
            if (!declaration || !arguments || !chart || !generic || declaration->home_scope != derived.home_scope ||
                position(declaration->identity.locus).file != position(derived.identity.locus).file)
                return nullptr;
            // A specialization can select a different parent than the primary template. Do not guess.
            for (const auto& specialization : reader.partition<PartialSpecializationDecl>())
                if (index_like::null(specialization.specialization_form) ||
                    reader.get(specialization.specialization_form).template_decl == named->decl)
                    return nullptr;
            for (const auto& specialization : reader.partition<SpecializationDecl>())
                if (specialization.sort != SpecializationSort::Implicit &&
                    (index_like::null(specialization.specialization_form) ||
                     reader.get(specialization.specialization_form).template_decl == named->decl))
                    return nullptr;
            const auto* generic_base = reader.get_if<BaseType>(generic->base);
            const auto* parameter_type = generic_base ? reader.get_if<DesignatedType>(generic_base->type) : nullptr;
            const auto* parameter = parameter_type ? reader.get_if<ParameterDecl>(parameter_type->decl) : nullptr;
            if (!generic_base || generic_base->access != Access::Public || generic_base->traits != BaseClassTraits::None ||
                !parameter || parameter->position == 0)
                return nullptr;
            const auto params = reader.sequence(*chart);
            const auto args = reader.sequence(*arguments);
            if (args.size() != params.size() || parameter->position > args.size()) return nullptr;
            const auto& expected = params[parameter->position - 1];
            if (expected.identity.name != parameter->identity.name || expected.level != parameter->level ||
                expected.position != parameter->position)
                return nullptr;
            const auto* actual = reader.get_if<TypeExpr>(args[parameter->position - 1]);
            const auto* actual_type = actual ? reader.get_if<DesignatedType>(actual->denotation) : nullptr;
            return actual_type ? reader.get_if<ScopeDecl>(actual_type->decl) : nullptr;
        }

        void walk(ScopeIndex index, const std::string& prefix = {})
        {
            const auto* scope = reader.try_get(index);
            if (!scope)
                return;
            for (const auto& entry : reader.sequence(*scope))
            {
                if (entry.index.sort() == DeclSort::Enumeration)
                {
                    if (prsm)
                        reject_binding_location(entry.index);
                    const auto& type = reader.get<EnumerationDecl>(entry.index);
                    if (has(type.basic_spec, BasicSpecifiers::NonExported) ||
                        has(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule))
                        continue;
                    enum_model result;
                    result.name = reader.get(type.identity.name);
                    if (!identifier(result.name))
                        continue;
                    result.qualified_name = prefix + result.name;
                    result.position = position(type.identity.locus);
                    if (ignored(attributes(type.identity.locus, result.name, true)))
                        continue;
                    for (const auto& value : reader.sequence(type.initializer))
                        result.enumerators.emplace_back(reader.get(value.identity.name));
                    model.enums.push_back(std::move(result));
                }
                else if (entry.index.sort() == DeclSort::Scope)
                {
                    const auto& type = reader.get<ScopeDecl>(entry.index);
                    const auto* fundamental = reader.get_if<FundamentalType>(type.type);
                    if (!fundamental)
                        continue;
                    const std::string short_name = name(type.identity.name);
                    if (!identifier(short_name) || has(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule))
                        continue;
                    if (fundamental->basis == TypeBasis::Namespace)
                    {
                        if (prsm && binding_groups(attributes(type.identity.locus, short_name, true)))
                            fail(type.identity.locus, "PrSM namespace directives are unsupported");
                        walk(type.initializer, prefix + short_name + "::");
                    }
                    else if (fundamental->basis == TypeBasis::Struct || fundamental->basis == TypeBasis::Class)
                    {
                        const auto groups = attributes(type.identity.locus, short_name, true);
                        if (has(type.basic_spec, BasicSpecifiers::NonExported) || !reader.try_get(type.initializer))
                        {
                            if (prsm && binding_groups(groups))
                                fail(type.identity.locus, "PrSM types must be exported complete definitions");
                            continue;
                        }
                        class_model result;
                        result.name = short_name;
                        result.qualified_name = prefix + short_name;
                        result.schema_name = "\"" + result.qualified_name + "\"";
                        result.position = position(type.identity.locus);
                        result.attributes = uses(groups);
                        if (prsm)
                        {
                            class_model selected = result;
                            selected.attributes = metadata->uses(groups, scopes, {}, true);
                            prsm_members(type, selected);
                            if (binding_groups(groups) || !selected.methods.empty())
                                exports.push_back(std::move(selected));
                        }
                        if (ignored(groups) || (metadata && metadata->directive(groups, "attribute")))
                            continue;
                        if (!index_like::null(type.base))
                        {
                            const auto* base = reader.get_if<BaseType>(type.base);
                            const auto* designated = base ? reader.get_if<DesignatedType>(base->type) : nullptr;
                            const auto* base_scope = designated ? reader.get_if<ScopeDecl>(designated->decl) : nullptr;
                            if (base && !base_scope)
                            {
                                const auto* syntax = reader.get_if<SyntacticType>(base->type);
                                const auto* id = syntax ? reader.get_if<TemplateIdExpr>(syntax->expr) : nullptr;
                                const auto* named = id ? reader.get_if<NamedDeclExpr>(id->primary_template) : nullptr;
                                const auto* reference = named ? reader.get_if<ReferenceDecl>(named->decl) : nullptr;
                                if (reference)
                                {
                                    const auto owner = std::string(reader.get(reference->translation_unit.owner));
                                    fail(type.identity.locus, "unsupported inheritance: imported base template cannot be verified" +
                                                                  (owner.empty() ? std::string{} : " (module " + owner + ")") +
                                                                  ": " + result.qualified_name);
                                }
                            }
                            if (!base_scope && base) base_scope = crtp_reflected_base(base->type, type);
                            if (prsm || !base || base->access != Access::Public ||
                                base->traits != BaseClassTraits::None || !base_scope ||
                                base_scope->home_scope != type.home_scope)
                                fail(type.identity.locus, "unsupported inheritance: " + result.qualified_name);
                            const std::string base_name = name(base_scope->identity.name);
                            const std::string qualified_base = prefix + base_name;
                            if (!identifier(base_name) ||
                                std::ranges::none_of(model.classes, [&](const class_model& existing) {
                                    return existing.qualified_name == qualified_base;
                                }))
                                fail(type.identity.locus, "base must be a previously reflected type in the same scope: " +
                                                              result.qualified_name);
                            result.bases.push_back({qualified_base, base_name});
                        }
                        if (metadata)
                            if (const auto* reflect = metadata->directive(groups, "reflect");
                                reflect && reflect->has_arguments)
                                result.schema_name = metadata->arguments(*reflect);
                        members(type, result);
                        model.classes.push_back(std::move(result));
                    }
                }
                else if (prsm)
                    reject_binding_location(entry.index);
            }
        }
    };

    void write_changed(const std::filesystem::path& path, const std::string& text)
    {
        std::ifstream old(path, std::ios::binary);
        if (old && std::string(std::istreambuf_iterator<char>(old), {}) == text)
            return;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        if (!output)
            throw std::runtime_error("cannot write " + path.string());
    }
} // namespace

int main(int argc, char** argv)
{
    std::string input, module, registration, directory, metadata_path, prsm_adapter, interface_module;
    bool prsm = false;
    std::set<std::string> scopes{"reflgen"};
    std::vector<std::string> attribute_imports;
    const char* stage = "arguments";
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--prsm")
            {
                prsm = true;
                continue;
            }
            if (i + 1 == argc)
                throw std::runtime_error("missing option value");
            const std::string value = argv[++i];
            if (option == "--ifc")
                input = value;
            else if (option == "--import")
                module = value;
            else if (option == "--module")
                registration = value;
            else if (option == "--output")
                directory = value;
            else if (option == "--module-interface")
                interface_module = value;
            else if (option == "--metadata")
                metadata_path = value;
            else if (option == "--prsm-adapter-header")
                prsm_adapter = value;
            else if (option == "--attribute-scope")
            {
                if (!identifier(value))
                    throw std::runtime_error("attribute scope must be an identifier");
                scopes.insert(value);
            }
            else if (option == "--attribute-import")
            {
                if (value.empty() || !std::ranges::all_of(value, [](unsigned char c) {
                        return std::isalnum(c) || c == '_' || c == '.';
                    }))
                    throw std::runtime_error("invalid attribute module name");
                attribute_imports.push_back(value);
            }
            else
                throw std::runtime_error("unknown option: " + option);
        }
        if (input.empty() || directory.empty() || !identifier(registration))
            throw std::runtime_error(
                "usage: reflgen-ifc --ifc FILE --import MODULE --module REGISTRATION --output DIR "
                "[--metadata MSVC_PREPROCESSED_FILE] [--attribute-scope NAMESPACE] [--attribute-import MODULE] "
                "[--prsm --prsm-adapter-header FILE]");
        if (!interface_module.empty())
        {
            std::size_t begin = 0;
            while (begin < interface_module.size())
            {
                const auto end = interface_module.find('.', begin);
                const auto part = interface_module.substr(begin, end == std::string::npos ? end : end - begin);
                if (!identifier(part)) throw std::runtime_error("invalid generated module name");
                if (end == std::string::npos) break;
                begin = end + 1;
                if (begin == interface_module.size()) throw std::runtime_error("invalid generated module name");
            }
            if (prsm) throw std::runtime_error("PrSM module-interface output is unsupported");
        }
        stage = "open";
        if (prsm && (metadata_path.empty() || prsm_adapter.empty()))
            throw std::runtime_error("--prsm requires --metadata and --prsm-adapter-header");
        if (!prsm && !prsm_adapter.empty())
            throw std::runtime_error("--prsm-adapter-header requires --prsm");
        if (prsm)
        {
            if (!std::filesystem::is_regular_file(prsm_adapter))
                throw std::runtime_error("--prsm-adapter-header must name an existing file");
            prsm_adapter = std::filesystem::absolute(prsm_adapter).lexically_normal().generic_string();
        }
        ifc::tool::InputFile container{std::filesystem::path(input)};
        InputIfc file{container.contents()};
        const auto u8path = std::filesystem::path(input).u8string();
        stage = "validate";
        if (!file.validate<UnitSort::Primary>(Pathname{u8path.c_str()}, Architecture::Unknown, Pathname{},
                                              IfcOptions(to_underlying(IfcOptions::IntegrityCheck) |
                                                         to_underlying(IfcOptions::AllowAnyPrimaryInterface))))
            throw std::runtime_error("not an IFC primary module interface");
        const auto actual_module = std::string(file.get(file.header()->unit.module_name()));
        if (module.empty())
            module = actual_module;
        if (file.header()->unit.sort() != UnitSort::Primary)
            throw std::runtime_error("only primary named module interfaces are supported");
        if (module != actual_module)
            throw std::runtime_error("module mismatch: expected " + module + ", IFC contains " + actual_module);
        stage = "read partitions";
        Reader reader = [&file] {
            try
            {
                return Reader(file);
            }
            catch (const InvalidPartitionName& error)
            {
                throw std::runtime_error("unsupported IFC partition: " + std::string(error.name));
            }
        }();
        std::unique_ptr<module_metadata> metadata;
        if (!metadata_path.empty())
        {
            metadata = std::make_unique<module_metadata>(metadata_path);
            if (metadata->module() != module)
                throw std::runtime_error("metadata module mismatch");
            if (std::filesystem::last_write_time(metadata_path) < std::filesystem::last_write_time(input))
                throw std::runtime_error("metadata predates IFC; regenerate MSVC /P output after compiling the module");
        }
        extractor extract{reader, {input, {}, {}}, metadata.get(), scopes};
        extract.prsm = prsm;
        stage = "extract";
        extract.walk(file.header()->global_scope);
        std::optional<prsm_output> bridge;
        if (prsm)
        {
            metadata->validate_prsm_selection();
            if (file.header()->arch != Architecture::X64)
                throw std::runtime_error("PrSM IFC currently requires Windows x64");
            diagnostics report;
            auto bindings = lower_prsm(extract.exports, {"x86_64-pc-windows-msvc", 64}, registration,
                                       {{binding_source_kind::named_module, module}}, report);
            if (bindings)
                bridge = emit_prsm(*bindings, prsm_adapter, report);
            if (report.has_errors())
            {
                report.print(stderr);
                return 1;
            }
        }
        const auto output = std::filesystem::absolute(directory);
        std::filesystem::create_directories(output);
        const std::string stem = "reflgen_" + registration;
        if (!interface_module.empty())
        {
            if (interface_module == module)
                throw std::runtime_error("generated module must differ from the imported module");
            const auto target = output / (interface_module + ".ixx");
            std::error_code error;
            if (std::filesystem::equivalent(input, target, error))
                throw std::runtime_error("module output collides with its IFC input");
            for (const auto& type : extract.model.classes)
            {
                error.clear();
                if (!type.position.file.empty() && std::filesystem::equivalent(type.position.file, target, error))
                    throw std::runtime_error("module output collides with a source file");
            }
            for (const auto& type : extract.model.enums)
            {
                error.clear();
                if (!type.position.file.empty() && std::filesystem::equivalent(type.position.file, target, error))
                    throw std::runtime_error("module output collides with a source file");
            }
            const auto text = emit_ifc_module_interface(extract.model, module, interface_module, registration,
                                                        target.generic_string(), attribute_imports);
            write_changed(target, text);
            write_changed(output / (stem + ".attributes.tsv"),
                          metadata ? metadata->catalog(scopes) : "reflgen-attributes\t1\n");
            std::cout << "reflgen-ifc: " << module << ": " << extract.model.classes.size() << " class(es), "
                      << extract.model.enums.size() << " enum(s)\n";
            return 0;
        }
        if (prsm)
        {
            std::vector<std::filesystem::path> inputs{input, metadata_path, prsm_adapter};
            for (const auto& type : extract.exports)
            {
                inputs.emplace_back(type.position.file);
                for (const auto& method : type.methods)
                    inputs.emplace_back(method.position.file);
            }
            for (const auto suffix :
                 {".h", ".cpp", ".attributes.tsv", "_prsm.h", "_prsm.cpp", "_prsm.abi.tsv", "_prsm.bindings.json"})
            {
                const auto destination = output / (stem + suffix);
                for (const auto& source : inputs)
                {
                    std::error_code error;
                    if (!source.empty() && std::filesystem::equivalent(source, destination, error))
                        throw std::runtime_error("PrSM output collides with an input: " + destination.string());
                }
            }
        }
        const auto header = output / (stem + ".h");
        const std::string header_text =
            emit_ifc_header(extract.model, module, registration, header.generic_string(), attribute_imports);
        // Keep registration source tied to the complete schema, even when its list of types is unchanged.
        std::uint64_t fingerprint = 14695981039346656037ull;
        for (const unsigned char byte : header_text)
            fingerprint = (fingerprint ^ byte) * 1099511628211ull;
        std::string source =
            "// <auto-generated/> Generated by reflgen IFC\n// schema: " + std::to_string(fingerprint) +
            "\n#include \"reflgen/runtime/registry.h\"\nimport " + module + ";\n#include \"" + stem +
            ".h\"\nnamespace reflgen::generated {\nvoid register_" + registration + "(::reflgen::registry& target) {\n";
        for (const auto& type : extract.model.classes)
            source += "    target.add<::" + type.qualified_name + ">();\n";
        source += "    static_cast<void>(target);\n}\nvoid register_" + registration + "() { register_" + registration +
                  "(::reflgen::default_registry()); }\n}\n";
        // Do not publish partial output if IFC extraction or code generation failed.
        write_changed(header, header_text);
        if (bridge)
        {
            write_changed(output / (stem + "_prsm.h"), bridge->header);
            write_changed(output / (stem + "_prsm.cpp"), bridge->source);
            write_changed(output / (stem + "_prsm.abi.tsv"), bridge->abi_table);
            write_changed(output / (stem + "_prsm.bindings.json"), bridge->catalog);
        }
        write_changed(output / (stem + ".cpp"), source);
        write_changed(output / (stem + ".attributes.tsv"),
                      metadata ? metadata->catalog(scopes) : "reflgen-attributes\t1\n");
        std::cout << "reflgen-ifc: " << module << ": " << extract.model.classes.size() << " class(es), "
                  << extract.model.enums.size() << " enum(s)\n";
        return 0;
    }
    catch (const diagnostic_failure& error)
    {
        std::cerr << error.source.file << '(' << error.source.line << ',' << error.source.column
                  << "): error RG0200: " << error.what() << '\n';
    }
    catch (const IntegrityCheckFailed&)
    {
        std::cerr << input << "(1,1): error RG0200: IFC integrity check failed\n";
    }
    catch (const UnsupportedFormatVersion&)
    {
        std::cerr << input << "(1,1): error RG0200: unsupported IFC version; rebuild with a compatible MSVC/IFC SDK\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << input << "(1,1): error RG0200: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << input << "(1,1): error RG0200: invalid or incompatible IFC (" << stage << ")\n";
    }
    return 1;
}

#pragma once
#include "metadata.h"
#include "semantic.h"
#include <cctype>
#include <set>

namespace reflgen::generator::ifc_frontend
{
    struct diagnostic_failure : std::runtime_error
    {
        source_position source;
        diagnostic_failure(source_position location, const std::string& message)
            : std::runtime_error(message), source(std::move(location)) {}
    };

    inline bool identifier(std::string_view text)
    {
        if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_'))
            return false;
        return std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }

    class extractor
    {
      public:
        header_model model;
        bool prsm = false;
        std::vector<class_model> exports;

        extractor(Reader& reader, std::string path, module_metadata* metadata, std::set<std::string> scopes)
            : model{std::move(path), {}, {}}, reader_(reader), semantic_(reader), metadata_(metadata),
              scopes_(std::move(scopes)) {}

        void walk(ScopeIndex index)
        {
            collect(index, {}, {}, false);
            // All selected types are known before base traversal, including bases declared later in a scope.
            for (const auto& record : records_)
            {
                auto& result = model.classes[record.output];
                members(*record.declaration, result, record.explicit_reflect);
                std::set<const ScopeDecl*> active;
                std::set<std::string> emitted;
                bases(record.declaration->base, {}, result, active, emitted, record.declaration->identity.locus);
            }
        }

      private:
        struct record_info
        {
            const ScopeDecl* declaration;
            std::size_t output;
            bool explicit_reflect;
        };
        Reader& reader_;
        semantics semantic_;
        module_metadata* metadata_;
        std::set<std::string> scopes_;
        std::vector<record_info> records_;
        std::map<const ScopeDecl*, std::size_t> selected_;

        source_position position(SourceLocation locus) const
        {
            if (index_like::null(locus.line)) return {};
            const auto& line = reader_.get(locus.line);
            std::string file;
            if (line.file.sort() == NameSort::SourceFile)
                file = semantic_.text(reader_.get<SourceFileName>(line.file).name);
            std::replace(file.begin(), file.end(), '\\', '/');
            return {file, static_cast<unsigned>(to_underlying(line.line)),
                    static_cast<unsigned>(to_underlying(locus.column))};
        }

        [[noreturn]] void fail(SourceLocation locus, const std::string& message) const
        {
            throw diagnostic_failure(position(locus), message);
        }

        std::vector<attribute_group> attributes(SourceLocation locus, std::string_view member, bool type = false) const
        {
            if (!metadata_) return {};
            try { return metadata_->declaration(position(locus), member, type); }
            catch (const std::exception& error) { fail(locus, std::string(error.what()) + " (declaration " + std::string(member) + ")"); }
        }

        const scanned_attribute* directive(const std::vector<attribute_group>& groups, std::string_view name) const
        {
            return metadata_ ? metadata_->directive(groups, name) : nullptr;
        }

        std::vector<attribute_use> uses(const std::vector<attribute_group>& groups,
                                        const qualifier_lookup& lookup = {}) const
        {
            return metadata_ ? metadata_->uses(groups, scopes_, lookup) : std::vector<attribute_use>{};
        }

        bool binding_groups(const std::vector<attribute_group>& groups)
        {
            if (!metadata_)
                return false;
            for (const auto name : {"interop", "lifetime", "property_get", "property_set", "effect", "owner", "receiver"})
                if (metadata_->directive(groups, name))
                    return true;
            return false;
        }

        void reject_binding_location(DeclIndex index)
        {
            reader_.visit_with_index(index, [&](auto, const auto& declaration) {
                if constexpr (requires {
                                  declaration.identity.locus;
                                  declaration.identity.name;
                              })
                {
                    std::string spelling;
                    if constexpr (std::is_same_v<decltype(declaration.identity.name), NameIndex>)
                        spelling = semantic_.name(declaration.identity.name);
                    else if constexpr (std::is_same_v<decltype(declaration.identity.name), TextOffset>)
                        spelling = reader_.get(declaration.identity.name);
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
            if (const auto* alias = reader_.get_if<DesignatedType>(index))
            {
                if (alias->decl.sort() == DeclSort::Alias)
                    return scalar_type(reader_.get<AliasDecl>(alias->decl).aliasee, depth + 1);
                return result;
            }
            if (const auto* qualified = reader_.get_if<QualifiedType>(index))
            {
                if ((to_underlying(qualified->qualifiers) & ~0x03) != 0)
                    return result;
                result = scalar_type(qualified->unqualified_type, depth + 1);
                result.is_const = (to_underlying(qualified->qualifiers) & to_underlying(Qualifier::Const)) != 0;
                result.is_volatile = (to_underlying(qualified->qualifiers) & to_underlying(Qualifier::Volatile)) != 0;
                return result;
            }
            const auto* fundamental = reader_.get_if<FundamentalType>(index);
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
            const auto* body = reader_.try_get(scope.initializer);
            if (!body)
                return;
            std::map<std::string, unsigned> counts;
            for (const auto& entry : reader_.sequence(*body))
                if (entry.index.sort() == DeclSort::Method)
                    ++counts[semantic_.name(reader_.get<NonStaticMemberFunctionDecl>(entry.index).identity.name)];
                else if (entry.index.sort() == DeclSort::Function)
                    ++counts[semantic_.name(reader_.get<FunctionDecl>(entry.index).identity.name)];
            for (const auto& entry : reader_.sequence(*body))
            {
                if (entry.index.sort() != DeclSort::Method)
                {
                    reject_binding_location(entry.index);
                    continue;
                }
                const auto& method = reader_.get<NonStaticMemberFunctionDecl>(entry.index);
                const auto member_name = semantic_.name(method.identity.name);
                const auto groups = attributes(method.identity.locus, member_name, true);
                if (!binding_groups(groups))
                    continue;
                if (!identifier(member_name))
                    fail(method.identity.locus, "unsupported PrSM operator/conversion");
                const auto* type = reader_.get_if<MethodType>(method.type);
                if (!type)
                    fail(method.identity.locus, "unknown PrSM IFC method type");
                method_model result;
                result.name = member_name;
                result.owner = owner.qualified_name;
                result.position = position(method.identity.locus);
                result.access = method.access == Access::Public ? "public" : "private";
                result.attributes = metadata_->uses(groups, scopes_, {}, true);
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
                    if (const auto* tuple = reader_.get_if<TupleType>(type->source))
                        for (const auto item : reader_.sequence(*tuple))
                            parameter(item);
                    else if (scalar_type(type->source).kind != "void")
                        parameter(type->source);
                }
                owner.methods.push_back(std::move(result));
            }
        }

        void collect(ScopeIndex index, const std::string& prefix, const std::vector<std::string>& namespaces,
                     bool nested)
        {
            const auto* scope = reader_.try_get(index);
            if (!scope) return;
            for (const auto& entry : reader_.sequence(*scope))
            {
                if (entry.index.sort() == DeclSort::Enumeration)
                {
                    if (prsm) reject_binding_location(entry.index);
                    const auto& type = reader_.get<EnumerationDecl>(entry.index);
                    if (has_flag(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule) ||
                        (nested ? type.access != Access::Public : has_flag(type.basic_spec, BasicSpecifiers::NonExported)))
                        continue;
                    enum_model result;
                    result.name = semantic_.text(type.identity.name);
                    if (!identifier(result.name)) continue;
                    result.qualified_name = prefix + result.name;
                    result.position = position(type.identity.locus);
                    result.nested = nested;
                    result.namespaces = namespaces;
                    if (directive(attributes(type.identity.locus, result.name, true), "ignore")) continue;
                    for (const auto& value : reader_.sequence(type.initializer))
                        result.enumerators.push_back(semantic_.text(value.identity.name));
                    model.enums.push_back(std::move(result));
                }
                else if (entry.index.sort() == DeclSort::Scope)
                {
                    const auto& type = reader_.get<ScopeDecl>(entry.index);
                    const auto* fundamental = reader_.get_if<FundamentalType>(type.type);
                    if (!fundamental || has_flag(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule)) continue;
                    const std::string short_name = semantic_.name(type.identity.name);
                    if (!identifier(short_name)) continue;
                    if (fundamental->basis == TypeBasis::Namespace)
                    {
                        if (prsm && binding_groups(attributes(type.identity.locus, short_name, true)))
                            fail(type.identity.locus, "PrSM namespace directives are unsupported");
                        auto child_namespaces = namespaces;
                        child_namespaces.push_back(prefix + short_name);
                        collect(type.initializer, prefix + short_name + "::", child_namespaces, false);
                        continue;
                    }
                    if (fundamental->basis != TypeBasis::Struct && fundamental->basis != TypeBasis::Class) continue;
                    const auto groups = attributes(type.identity.locus, short_name, true);
                    if ((nested ? type.access != Access::Public : has_flag(type.basic_spec, BasicSpecifiers::NonExported)) ||
                        !reader_.try_get(type.initializer))
                    {
                        if (prsm && binding_groups(groups))
                            fail(type.identity.locus, "PrSM types must be exported complete public definitions");
                        continue;
                    }
                    if (prsm)
                    {
                        class_model selected;
                        selected.name = short_name;
                        selected.qualified_name = prefix + short_name;
                        selected.schema_name = "\"" + selected.qualified_name + "\"";
                        selected.position = position(type.identity.locus);
                        selected.attributes = metadata_->uses(groups, scopes_, {}, true);
                        prsm_members(type, selected);
                        if (binding_groups(groups) || !selected.methods.empty())
                        {
                            if (nested || !index_like::null(type.base))
                                fail(type.identity.locus, "PrSM IFC currently requires a nonnested class without inheritance");
                            exports.push_back(std::move(selected));
                        }
                    }
                    if (!directive(groups, "ignore") && !directive(groups, "attribute"))
                    {
                        class_model result;
                        result.name = short_name;
                        result.class_key = fundamental->basis == TypeBasis::Class ? "class" : "struct";
                        result.qualified_name = prefix + short_name;
                        result.schema_name = "\"" + result.qualified_name + "\"";
                        result.position = position(type.identity.locus);
                        result.attributes = uses(groups);
                        result.namespaces = namespaces;
                        result.nested = nested;
                        const auto* reflect = directive(groups, "reflect");
                        if (reflect && reflect->has_arguments) result.schema_name = metadata_->arguments(*reflect);
                        selected_[&type] = model.classes.size();
                        records_.push_back({&type, model.classes.size(), reflect != nullptr});
                        model.classes.push_back(std::move(result));
                    }
                    // Public nested types remain nameable even when the enclosing schema is ignored.
                    collect(type.initializer, prefix + short_name + "::", namespaces, true);
                }
                // Ordinary class members were already validated by prsm_members;
                // this traversal only discovers their public nested types.
                else if (prsm && !nested)
                    reject_binding_location(entry.index);
            }
        }

        bool grants_access(const ScopeDecl& scope) const
        {
            const auto grants = [this](DeclIndex index) {
                if (index.sort() != DeclSort::Friend) return false;
                const auto target = semantic_.friend_declaration(reader_.get<FriendDecl>(index).index);
                if (index_like::null(target)) return false;
                // An unrelated imported friend must not disable public-only reflection.
                // Without its owning IFC it also cannot prove access to private members.
                if (target.sort() != DeclSort::Scope) return false;
                const auto& friend_scope = reader_.get<ScopeDecl>(target);
                return semantic_.declaration_name(target) == "reflgen::access" &&
                    has_flag(friend_scope.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule);
            };
            // MSVC stores friendship as an associated trait, outside the member scope.
            if (const auto* friends = reader_.try_find<trait::Friends>(reader_.index_of<DeclIndex>(scope)))
                for (const auto& entry : reader_.sequence(friends->trait))
                    if (grants(entry.index)) return true;
            // Retain the declaration form for IFC producers which put friends in the body.
            if (const auto* body = reader_.try_get(scope.initializer))
                for (const auto& entry : reader_.sequence(*body))
                    if (grants(entry.index)) return true;
            return false;
        }

        bool visible(Access access, bool friend_access, bool explicit_reflect, SourceLocation locus,
                     const std::string& qualified_name) const
        {
            if (access == Access::Public || friend_access) return true;
            if (explicit_reflect)
                fail(locus, "reflecting non-public member requires friend struct ::reflgen::access: " + qualified_name);
            return false;
        }

        void members(const ScopeDecl& scope, class_model& result, bool explicit_reflect)
        {
            const auto* body = reader_.try_get(scope.initializer);
            if (!body) return;
            const bool friend_access = grants_access(scope);
            std::map<std::string, std::size_t> method_counts;
            std::set<std::string> imported_methods;
            std::set<std::string> constants;
            for (const auto& entry : reader_.sequence(*body))
            {
                if (entry.index.sort() == DeclSort::Variable)
                    constants.insert(semantic_.name(reader_.get<VariableDecl>(entry.index).identity.name));
                else if (entry.index.sort() == DeclSort::Method)
                    ++method_counts[semantic_.name(reader_.get<NonStaticMemberFunctionDecl>(entry.index).identity.name)];
                else if (entry.index.sort() == DeclSort::Function)
                    ++method_counts[semantic_.name(reader_.get<FunctionDecl>(entry.index).identity.name)];
                else if (entry.index.sort() == DeclSort::Template)
                    ++method_counts[semantic_.simple_name(reader_.get<TemplateDecl>(entry.index).identity.name)];
                else if (entry.index.sort() == DeclSort::Using)
                {
                    const auto& use = reader_.get<UsingDecl>(entry.index);
                    const auto name = index_like::null(use.name) ? use.identity.name : use.name;
                    imported_methods.insert(semantic_.text(name));
                }
            }
            const qualifier_lookup lookup = [&constants](std::string_view value) {
                return constants.contains(std::string(value)) ? "T::" : "";
            };
            for (const auto& entry : reader_.sequence(*body))
            {
                if (entry.index.sort() == DeclSort::Field)
                {
                    const auto& field = reader_.get<FieldDecl>(entry.index);
                    if (field.access != Access::Public && !friend_access && !explicit_reflect) continue;
                    const std::string field_name = semantic_.text(field.identity.name);
                    const auto groups = attributes(field.identity.locus, field_name);
                    if (directive(groups, "ignore") ||
                        !visible(field.access, friend_access, explicit_reflect, field.identity.locus,
                                 result.qualified_name + "::" + field_name)) continue;
                    bool is_reference;
                    try { is_reference = semantic_.reference_type(field.type); }
                    catch (const std::exception& error) { fail(field.identity.locus, error.what()); }
                    if (is_reference)
                        fail(field.identity.locus, "reference field is unsupported: " + result.qualified_name);
                    if (!identifier(field_name)) fail(field.identity.locus, "anonymous field is unsupported: " + result.qualified_name);
                    result.fields.push_back({field_name, position(field.identity.locus), uses(groups, lookup)});
                }
                else if (entry.index.sort() == DeclSort::Bitfield)
                {
                    const auto& bit = reader_.get<BitfieldDecl>(entry.index);
                    if (bit.access != Access::Public && !friend_access && !explicit_reflect) continue;
                    const auto groups = attributes(bit.identity.locus, semantic_.text(bit.identity.name));
                    if (!directive(groups, "ignore") &&
                        visible(bit.access, friend_access, explicit_reflect, bit.identity.locus, result.qualified_name))
                        fail(bit.identity.locus, "bit-field is unsupported: " + result.qualified_name);
                }
                else if (entry.index.sort() == DeclSort::Method)
                {
                    const auto& method = reader_.get<NonStaticMemberFunctionDecl>(entry.index);
                    if (method.access != Access::Public && !friend_access && !explicit_reflect) continue;
                    const std::string method_name = semantic_.name(method.identity.name);
                    if (!identifier(method_name)) continue;
                    const auto groups = attributes(method.identity.locus, method_name, true);
                    if (directive(groups, "ignore") ||
                        !visible(method.access, friend_access, explicit_reflect, method.identity.locus,
                                 result.qualified_name + "::" + method_name)) continue;
                    if (has_flag(method.traits, FunctionTraits::Deleted) || has_flag(method.traits, FunctionTraits::Immediate))
                        fail(method.identity.locus, "deleted or consteval method cannot be reflected: " + result.qualified_name + "::" + method_name);
                    method_model output{method_name, {}, position(method.identity.locus), uses(groups, lookup)};
                    output.is_overloaded = method_counts[method_name] > 1 || imported_methods.contains(method_name);
                    if (output.is_overloaded)
                    {
                        try { output.pointer_type = semantic_.method_pointer(method.type); }
                        catch (const std::exception& error) { fail(method.identity.locus, error.what()); }
                    }
                    if (method.chart.sort() == ChartSort::Unilevel)
                        for (const auto& parameter : reader_.sequence(reader_.get<UnilevelChart>(method.chart)))
                            output.parameters.push_back(semantic_.text(parameter.identity.name));
                    result.methods.push_back(std::move(output));
                }
            }
        }

        void bases(TypeIndex index, const template_bindings& bindings, class_model& result,
                   std::set<const ScopeDecl*>& active, std::set<std::string>& emitted, SourceLocation locus,
                   bool public_path = true)
        {
            if (index_like::null(index)) return;
            if (index.sort() == TypeSort::Tuple)
            {
                for (const auto item : reader_.sequence(reader_.get<TupleType>(index)))
                    bases(item, bindings, result, active, emitted, locus, public_path);
                return;
            }
            Access access = Access::Public;
            BaseClassTraits traits = BaseClassTraits::None;
            if (index.sort() == TypeSort::Base)
            {
                const auto& base = reader_.get<BaseType>(index);
                access = base.access;
                traits = base.traits;
                index = base.type;
            }
            const bool accessible = public_path && access == Access::Public;
            resolved_scope base;
            try { base = semantic_.scope(index, bindings); }
            catch (const std::exception& error) { fail(locus, error.what()); }
            if (!base.declaration) fail(locus, "incomplete IFC base declaration");
            if (!active.insert(base.declaration).second) fail(locus, "cyclic IFC inheritance graph");
            const auto found = selected_.find(base.declaration);
            // The scope may be an instantiated CRTP pattern not present among exported concrete types.
            const auto identity = base.authoring_template ? base.authoring_template->identity : base.declaration->identity;
            const auto short_name = semantic_.simple_name(identity.name);
            const auto source_locus = index_like::null(identity.locus.line) ? locus : identity.locus;
            if (index_like::null(identity.locus.line))
                fail(locus, "base declaration has no authoring identity in this IFC: " + short_name);
            const auto groups = attributes(source_locus, short_name, true);
            const bool reflected = found != selected_.end() ||
                (!directive(groups, "ignore") && directive(groups, "reflect"));
            if (reflected && !accessible)
                fail(locus, "non-public reflected base cannot be serialized: " + result.qualified_name);
            if (accessible &&
                (has_flag(traits, BaseClassTraits::Shared) || has_flag(traits, BaseClassTraits::Expanded)))
                fail(locus, "virtual or pack-expanded reflected base path is unsupported: " + result.qualified_name);
            if (reflected)
            {
                if (found == selected_.end())
                    fail(locus, "reflected base has no exported concrete schema in this IFC: " + short_name);
                type_reference reference;
                reference.qualified_name = model.classes[found->second].qualified_name;
                reference.name = short_name;
                reference.templated = reference.qualified_name.find('<') != std::string::npos;
                if (!emitted.insert(reference.qualified_name).second)
                    fail(locus, "duplicate reflected base ancestry is unsupported: " + reference.qualified_name);
                result.bases.push_back(std::move(reference));
            }
            else
                // An ignored helper can still lead to a reflected ancestor through a non-public path.
                bases(base.declaration->base, base.bindings, result, active, emitted, source_locus, accessible);
            active.erase(base.declaration);
        }
    };
}

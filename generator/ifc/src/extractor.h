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
                file = reader_.get(reader_.get<SourceFileName>(line.file).name);
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
            catch (const std::exception& error) { fail(locus, error.what()); }
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

        void collect(ScopeIndex index, const std::string& prefix, const std::vector<std::string>& namespaces,
                     bool nested)
        {
            const auto* scope = reader_.try_get(index);
            if (!scope) return;
            for (const auto& entry : reader_.sequence(*scope))
            {
                if (entry.index.sort() == DeclSort::Enumeration)
                {
                    const auto& type = reader_.get<EnumerationDecl>(entry.index);
                    if (has_flag(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule) ||
                        (nested ? type.access != Access::Public : has_flag(type.basic_spec, BasicSpecifiers::NonExported)))
                        continue;
                    enum_model result;
                    result.name = reader_.get(type.identity.name);
                    if (!identifier(result.name)) continue;
                    result.qualified_name = prefix + result.name;
                    result.position = position(type.identity.locus);
                    result.nested = nested;
                    result.namespaces = namespaces;
                    if (directive(attributes(type.identity.locus, result.name, true), "ignore")) continue;
                    for (const auto& value : reader_.sequence(type.initializer))
                        result.enumerators.emplace_back(reader_.get(value.identity.name));
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
                        auto child_namespaces = namespaces;
                        child_namespaces.push_back(prefix + short_name);
                        collect(type.initializer, prefix + short_name + "::", child_namespaces, false);
                        continue;
                    }
                    if (fundamental->basis != TypeBasis::Struct && fundamental->basis != TypeBasis::Class) continue;
                    if (nested ? type.access != Access::Public : has_flag(type.basic_spec, BasicSpecifiers::NonExported))
                        continue;
                    if (!reader_.try_get(type.initializer)) continue;
                    const auto groups = attributes(type.identity.locus, short_name, true);
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
            }
        }

        bool grants_access(const ScopeDecl& scope) const
        {
            const auto* body = reader_.try_get(scope.initializer);
            if (!body) return false;
            for (const auto& entry : reader_.sequence(*body))
            {
                if (entry.index.sort() != DeclSort::Friend) continue;
                const auto target = semantic_.friend_declaration(reader_.get<FriendDecl>(entry.index).index);
                if (index_like::null(target)) continue;
                if (target.sort() == DeclSort::Reference)
                    fail(scope.identity.locus,
                         "friend declaration belongs to another IFC; imported friend resolution is unsupported; "
                         "declare reflgen::access through the global module fragment");
                if (target.sort() != DeclSort::Scope) continue;
                const auto& friend_scope = reader_.get<ScopeDecl>(target);
                if (semantic_.declaration_name(target) == "reflgen::access" &&
                    has_flag(friend_scope.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule))
                    return true;
            }
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
            }
            const qualifier_lookup lookup = [&constants](std::string_view value) {
                return constants.contains(std::string(value)) ? "T::" : "";
            };
            for (const auto& entry : reader_.sequence(*body))
            {
                if (entry.index.sort() == DeclSort::Field)
                {
                    const auto& field = reader_.get<FieldDecl>(entry.index);
                    const std::string field_name = reader_.get(field.identity.name);
                    const auto groups = attributes(field.identity.locus, field_name);
                    if (directive(groups, "ignore") ||
                        !visible(field.access, friend_access, explicit_reflect, field.identity.locus,
                                 result.qualified_name + "::" + field_name)) continue;
                    if (field.type.sort() == TypeSort::LvalueReference || field.type.sort() == TypeSort::RvalueReference)
                        fail(field.identity.locus, "reference field is unsupported: " + result.qualified_name);
                    if (!identifier(field_name)) fail(field.identity.locus, "anonymous field is unsupported: " + result.qualified_name);
                    result.fields.push_back({field_name, position(field.identity.locus), uses(groups, lookup)});
                }
                else if (entry.index.sort() == DeclSort::Bitfield)
                {
                    const auto& bit = reader_.get<BitfieldDecl>(entry.index);
                    const auto groups = attributes(bit.identity.locus, reader_.get(bit.identity.name));
                    if (!directive(groups, "ignore") &&
                        visible(bit.access, friend_access, explicit_reflect, bit.identity.locus, result.qualified_name))
                        fail(bit.identity.locus, "bit-field is unsupported: " + result.qualified_name);
                }
                else if (entry.index.sort() == DeclSort::Method)
                {
                    const auto& method = reader_.get<NonStaticMemberFunctionDecl>(entry.index);
                    const std::string method_name = semantic_.name(method.identity.name);
                    if (!identifier(method_name)) continue;
                    const auto groups = attributes(method.identity.locus, method_name, true);
                    if (directive(groups, "ignore") ||
                        !visible(method.access, friend_access, explicit_reflect, method.identity.locus,
                                 result.qualified_name + "::" + method_name)) continue;
                    if (has_flag(method.traits, FunctionTraits::Deleted) || has_flag(method.traits, FunctionTraits::Immediate))
                        fail(method.identity.locus, "deleted or consteval method cannot be reflected: " + result.qualified_name + "::" + method_name);
                    method_model output{method_name, {}, position(method.identity.locus), uses(groups, lookup)};
                    output.is_overloaded = method_counts[method_name] > 1;
                    if (output.is_overloaded)
                    {
                        try { output.pointer_type = semantic_.method_pointer(method.type); }
                        catch (const std::exception& error) { fail(method.identity.locus, error.what()); }
                    }
                    if (method.chart.sort() == ChartSort::Unilevel)
                        for (const auto& parameter : reader_.sequence(reader_.get<UnilevelChart>(method.chart)))
                            output.parameters.emplace_back(reader_.get(parameter.identity.name));
                    result.methods.push_back(std::move(output));
                }
            }
        }

        void bases(TypeIndex index, const template_bindings& bindings, class_model& result,
                   std::set<const ScopeDecl*>& active, std::set<std::string>& emitted, SourceLocation locus)
        {
            if (index_like::null(index)) return;
            if (index.sort() == TypeSort::Tuple)
            {
                for (const auto item : reader_.sequence(reader_.get<TupleType>(index)))
                    bases(item, bindings, result, active, emitted, locus);
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
            resolved_scope base;
            try { base = semantic_.scope(index, bindings); }
            catch (const std::exception& error) { fail(locus, error.what()); }
            if (!base.declaration) fail(locus, "incomplete IFC base declaration");
            if (!active.insert(base.declaration).second) fail(locus, "cyclic IFC inheritance graph");
            const auto found = selected_.find(base.declaration);
            // The scope may be an instantiated CRTP pattern not present among exported concrete types.
            const auto short_name = semantic_.simple_name(base.declaration->identity.name);
            const auto groups = attributes(base.declaration->identity.locus, short_name, true);
            const bool reflected = found != selected_.end() ||
                (!directive(groups, "ignore") && directive(groups, "reflect"));
            if (access != Access::Public)
            {
                if (reflected) fail(locus, "non-public reflected base cannot be serialized: " + result.qualified_name);
                active.erase(base.declaration);
                return;
            }
            if (has_flag(traits, BaseClassTraits::Shared) || has_flag(traits, BaseClassTraits::Expanded))
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
                bases(base.declaration->base, base.bindings, result, active, emitted, base.declaration->identity.locus);
            active.erase(base.declaration);
        }
    };
}

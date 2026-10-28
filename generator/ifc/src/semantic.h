#pragma once
// Semantic spelling and identity come from the IFC, never from unparsed source text.
#include <ifc/reader.hxx>
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace reflgen::generator::ifc_frontend
{
    using namespace ifc;
    using namespace ifc::symbolic;

    template<class E>
    bool has_flag(E value, E bit)
    {
        return (to_underlying(value) & to_underlying(bit)) != 0;
    }

    using template_bindings = std::map<std::pair<std::uint32_t, std::uint32_t>, TypeIndex>;

    struct resolved_scope
    {
        DeclIndex index{};
        const ScopeDecl* declaration = nullptr;
        template_bindings bindings;
        const TemplateDecl* authoring_template = nullptr;
    };

    class semantics
    {
      public:
        explicit semantics(Reader& reader) : reader_(reader) {}

        std::string text(TextOffset index) const
        {
            const auto* value = reader_.get(index);
            return value ? value : "";
        }

        std::string simple_name(NameIndex index, unsigned depth = 0) const
        {
            check_depth(depth);
            if (index.sort() == NameSort::Template)
                return simple_name(reader_.get<TemplateName>(index).name, depth + 1);
            if (index.sort() == NameSort::Specialization)
                return simple_name(reader_.get<SpecializationName>(index).primary_template, depth + 1);
            return name(index, depth);
        }

        std::string name(NameIndex index, unsigned depth = 0) const
        {
            check_depth(depth);
            switch (index.sort())
            {
            case NameSort::Identifier:
                return text(TextOffset(to_underlying(index.index())));
            case NameSort::Template:
                return name(reader_.get<TemplateName>(index).name, depth + 1);
            case NameSort::Specialization:
            {
                const auto& value = reader_.get<SpecializationName>(index);
                return name(value.primary_template, depth + 1) + "<" + expression(value.arguments, depth + 1) + ">";
            }
            default:
                return {};
            }
        }

        std::string declaration_name(DeclIndex index, unsigned depth = 0) const
        {
            check_depth(depth);
            if (index_like::null(index))
                return {};
            switch (index.sort())
            {
            case DeclSort::Scope: return named(reader_.get<ScopeDecl>(index), depth);
            case DeclSort::Enumeration: return named(reader_.get<EnumerationDecl>(index), depth);
            case DeclSort::Alias: return named(reader_.get<AliasDecl>(index), depth);
            case DeclSort::Template: return named(reader_.get<TemplateDecl>(index), depth);
            case DeclSort::Variable: return named(reader_.get<VariableDecl>(index), depth);
            case DeclSort::Parameter:
                throw std::runtime_error("unsubstituted template parameter in IFC type");
            case DeclSort::Specialization:
            {
                const auto& value = reader_.get<SpecializationDecl>(index);
                const auto& form = reader_.get(value.specialization_form);
                return declaration_name(form.template_decl, depth + 1) + "<" +
                       expression(form.arguments, depth + 1) + ">";
            }
            case DeclSort::Prolongation:
                return declaration_name(reader_.get<ProlongationDecl>(index).original_decl, depth + 1);
            case DeclSort::Reference:
                throw std::runtime_error("declaration belongs to another IFC; imported declaration resolution is not supported");
            default:
                throw std::runtime_error("unsupported declaration in IFC type spelling");
            }
        }

        std::string type_name(TypeIndex index) const { return declarator(index, {}); }

        std::string expression(ExprIndex index, unsigned depth = 0) const
        {
            check_depth(depth);
            if (index_like::null(index))
                return {};
            switch (index.sort())
            {
            case ExprSort::Type:
                return declarator(reader_.get<TypeExpr>(index).denotation, {}, depth + 1);
            case ExprSort::NamedDecl:
                return "::" + declaration_name(reader_.get<NamedDeclExpr>(index).decl, depth + 1);
            case ExprSort::TemplateId:
            {
                const auto& value = reader_.get<TemplateIdExpr>(index);
                return expression(value.primary_template, depth + 1) + "<" + expression(value.arguments, depth + 1) + ">";
            }
            case ExprSort::Tuple:
            {
                std::string result;
                for (const auto value : reader_.sequence(reader_.get<TupleExpr>(index)))
                {
                    if (!result.empty()) result += ", ";
                    result += expression(value, depth + 1);
                }
                return result;
            }
            case ExprSort::Literal:
            {
                const auto value = reader_.get<LiteralExpr>(index).value;
                if (value.sort() == LiteralSort::Immediate)
                    return std::to_string(to_underlying(value.index()));
                if (value.sort() == LiteralSort::Integer)
                    return std::to_string(reader_.get<std::int64_t>(value));
                throw std::runtime_error("unsupported nonintegral IFC template argument");
            }
            default:
                throw std::runtime_error("unsupported expression in IFC template argument");
            }
        }

        DeclIndex friend_declaration(ExprIndex expression_index) const
        {
            if (expression_index.sort() == ExprSort::NamedDecl)
                return reader_.get<NamedDeclExpr>(expression_index).decl;
            // A class template specialization is not the non-template reflgen::access.
            return {};
        }

        bool reference_type(TypeIndex index, unsigned depth = 0) const
        {
            check_depth(depth);
            if (index.sort() == TypeSort::LvalueReference || index.sort() == TypeSort::RvalueReference)
                return true;
            if (index.sort() == TypeSort::Qualified)
                return reference_type(reader_.get<QualifiedType>(index).unqualified_type, depth + 1);
            if (index.sort() == TypeSort::Designated)
            {
                auto declaration = reader_.get<DesignatedType>(index).decl;
                while (declaration.sort() == DeclSort::Prolongation)
                {
                    check_depth(++depth);
                    declaration = reader_.get<ProlongationDecl>(declaration).original_decl;
                }
                if (declaration.sort() == DeclSort::Alias)
                    return reference_type(reader_.get<AliasDecl>(declaration).aliasee, depth + 1);
                // A foreign reference belongs to a different Reader. Ordinary imported
                // fields do not need spelling or declaration resolution here.
            }
            return false;
        }

        TypeIndex substitute(TypeIndex index, const template_bindings& bindings) const
        {
            for (unsigned depth = 0; depth < 64; ++depth)
            {
                if (index.sort() != TypeSort::Designated)
                    return index;
                const auto declaration = reader_.get<DesignatedType>(index).decl;
                if (declaration.sort() != DeclSort::Parameter)
                    return index;
                const auto& parameter = reader_.get<ParameterDecl>(declaration);
                const auto found = bindings.find({parameter.level, parameter.position});
                if (found == bindings.end() || found->second == index)
                    throw std::runtime_error("unresolved IFC template base parameter");
                index = found->second;
            }
            throw std::runtime_error("cyclic IFC template base substitution");
        }

        resolved_scope scope(TypeIndex index, const template_bindings& bindings = {}, unsigned depth = 0) const
        {
            check_depth(depth);
            index = substitute(index, bindings);
            if (index.sort() == TypeSort::Qualified)
                return scope(reader_.get<QualifiedType>(index).unqualified_type, bindings, depth + 1);
            if (index.sort() == TypeSort::Base)
                return scope(reader_.get<BaseType>(index).type, bindings, depth + 1);
            if (index.sort() == TypeSort::Syntactic)
            {
                const auto expression_index = reader_.get<SyntacticType>(index).expr;
                if (expression_index.sort() == ExprSort::TemplateId)
                {
                    const auto& value = reader_.get<TemplateIdExpr>(expression_index);
                    if (value.primary_template.sort() != ExprSort::NamedDecl)
                        throw std::runtime_error("unsupported IFC template base name");
                    return template_scope(reader_.get<NamedDeclExpr>(value.primary_template).decl,
                                          value.arguments, bindings, depth + 1);
                }
                if (expression_index.sort() == ExprSort::NamedDecl)
                    return declaration_scope(reader_.get<NamedDeclExpr>(expression_index).decl, bindings, depth + 1);
            }
            if (index.sort() != TypeSort::Designated)
                throw std::runtime_error("unsupported IFC base type; cannot preserve inherited reflection");
            return declaration_scope(reader_.get<DesignatedType>(index).decl, bindings, depth + 1);
        }

        std::string method_pointer(TypeIndex index) const
        {
            FunctionTypeTraits extra = FunctionTypeTraits::None;
            for (unsigned depth = 0; depth < 64; ++depth)
            {
                if (index.sort() == TypeSort::Qualified)
                {
                    const auto& qualified = reader_.get<QualifiedType>(index);
                    if (has_flag(qualified.qualifiers, Qualifier::Const))
                        extra = FunctionTypeTraits(to_underlying(extra) | to_underlying(FunctionTypeTraits::Const));
                    if (has_flag(qualified.qualifiers, Qualifier::Volatile))
                        extra = FunctionTypeTraits(to_underlying(extra) | to_underlying(FunctionTypeTraits::Volatile));
                    if (has_flag(qualified.qualifiers, Qualifier::Restrict))
                        throw std::runtime_error("restricted IFC method type is unsupported");
                    index = qualified.unqualified_type;
                    continue;
                }
                if (index.sort() == TypeSort::LvalueReference || index.sort() == TypeSort::RvalueReference)
                {
                    const bool lvalue = index.sort() == TypeSort::LvalueReference;
                    extra = FunctionTypeTraits(to_underlying(extra) |
                        to_underlying(lvalue ? FunctionTypeTraits::Lvalue : FunctionTypeTraits::Rvalue));
                    index = lvalue ? reader_.get<LvalueReferenceType>(index).referee
                                   : reader_.get<RvalueReferenceType>(index).referee;
                    continue;
                }
                if (index.sort() != TypeSort::Method)
                    throw std::runtime_error("unsupported IFC overloaded method type");
                const auto& method = reader_.get<MethodType>(index);
                const auto traits = FunctionTypeTraits(to_underlying(method.traits) | to_underlying(extra));
                return declarator(method.target, "(" + convention(method.convention, true) + "T::*)(" +
                                  parameters(method.source, 0) + ")" + qualifiers(traits) + noexcept_suffix(method.eh_spec));
            }
            throw std::runtime_error("cyclic IFC method type");
        }

      private:
        Reader& reader_;

        static void check_depth(unsigned depth)
        {
            if (depth > 64)
                throw std::runtime_error("cyclic or excessively nested IFC semantic declaration");
        }

        template<class D>
        std::string named(const D& declaration, unsigned depth) const
        {
            const auto short_name = [&] {
                if constexpr (std::is_same_v<decltype(declaration.identity.name), TextOffset>)
                    return text(declaration.identity.name);
                else
                    return name(declaration.identity.name, depth + 1);
            }();
            if (short_name.empty())
                throw std::runtime_error("anonymous or unsupported IFC declaration name");
            const auto parent = declaration_name(declaration.home_scope, depth + 1);
            return parent.empty() ? short_name : parent + "::" + short_name;
        }

        static std::string fundamental(const FundamentalType& value)
        {
            const std::string sign = value.sign == TypeSign::Unsigned ? "unsigned " :
                                     value.sign == TypeSign::Signed ? "signed " : "";
            switch (value.basis)
            {
            case TypeBasis::Void: return "void";
            case TypeBasis::Bool: return "bool";
            case TypeBasis::Char:
                if (value.precision != TypePrecision::Default)
                    throw std::runtime_error("unsupported IFC character type precision");
                return sign + "char";
            case TypeBasis::Wchar_t: return "wchar_t";
            case TypeBasis::Int:
                switch (value.precision)
                {
                case TypePrecision::Default: return sign + "int";
                case TypePrecision::Short: return sign + "short";
                case TypePrecision::Long: return sign + "long";
                case TypePrecision::Bit8: return sign + "__int8";
                case TypePrecision::Bit16: return sign + "__int16";
                case TypePrecision::Bit32: return sign + "__int32";
                case TypePrecision::Bit64: return sign + "__int64";
                default: throw std::runtime_error("unsupported IFC integer type precision");
                }
            case TypeBasis::Float:
                if (value.precision != TypePrecision::Default)
                    throw std::runtime_error("unsupported IFC floating-point precision");
                return "float";
            case TypeBasis::Double:
                if (value.precision == TypePrecision::Long) return "long double";
                if (value.precision == TypePrecision::Default) return "double";
                throw std::runtime_error("unsupported IFC floating-point precision");
            case TypeBasis::Nullptr: return "decltype(nullptr)";
            case TypeBasis::Ellipsis: return "...";
            default: throw std::runtime_error("unsupported IFC fundamental type in method signature");
            }
        }

        static std::string convention(CallingConvention value, bool member)
        {
            switch (value)
            {
            case CallingConvention::Cdecl: return "__cdecl ";
            case CallingConvention::This:
                if (member) return "__thiscall ";
                throw std::runtime_error("__thiscall on a non-member IFC function type");
            case CallingConvention::Fast: return "__fastcall ";
            case CallingConvention::Std: return "__stdcall ";
            case CallingConvention::Vector: return "__vectorcall ";
            default: throw std::runtime_error("unsupported IFC calling convention");
            }
        }

        static std::string qualifiers(FunctionTypeTraits traits)
        {
            constexpr unsigned known = to_underlying(FunctionTypeTraits::Const) |
                to_underlying(FunctionTypeTraits::Volatile) | to_underlying(FunctionTypeTraits::Lvalue) |
                to_underlying(FunctionTypeTraits::Rvalue);
            if ((to_underlying(traits) & ~known) != 0 ||
                (has_flag(traits, FunctionTypeTraits::Lvalue) && has_flag(traits, FunctionTypeTraits::Rvalue)))
                throw std::runtime_error("unsupported IFC function qualifiers");
            std::string result;
            if (has_flag(traits, FunctionTypeTraits::Const)) result += " const";
            if (has_flag(traits, FunctionTypeTraits::Volatile)) result += " volatile";
            if (has_flag(traits, FunctionTypeTraits::Lvalue)) result += " &";
            if (has_flag(traits, FunctionTypeTraits::Rvalue)) result += " &&";
            return result;
        }

        static std::string noexcept_suffix(const NoexceptSpecification& specification)
        {
            switch (specification.sort)
            {
            case NoexceptSort::None: case NoexceptSort::False: return {};
            case NoexceptSort::True: case NoexceptSort::Unenforced: return " noexcept";
            default: throw std::runtime_error("unresolved noexcept in IFC overloaded method");
            }
        }

        std::string parameters(TypeIndex source, unsigned depth) const
        {
            if (index_like::null(source)) return {};
            if (source.sort() != TypeSort::Tuple)
                return declarator(source, {}, depth + 1);
            std::string result;
            for (const auto parameter : reader_.sequence(reader_.get<TupleType>(source)))
            {
                if (!result.empty()) result += ", ";
                result += declarator(parameter, {}, depth + 1);
            }
            return result;
        }

        bool requires_parentheses(TypeIndex index) const
        {
            for (unsigned depth = 0; depth < 64; ++depth)
            {
                if (index.sort() != TypeSort::Qualified)
                    return index.sort() == TypeSort::Function || index.sort() == TypeSort::Array;
                index = reader_.get<QualifiedType>(index).unqualified_type;
            }
            throw std::runtime_error("cyclic IFC qualified type");
        }

        std::string declarator(TypeIndex index, std::string suffix, unsigned depth = 0) const
        {
            check_depth(depth);
            switch (index.sort())
            {
            case TypeSort::Fundamental:
            {
                const auto value = fundamental(reader_.get<FundamentalType>(index));
                return suffix.empty() ? value : value + " " + suffix;
            }
            case TypeSort::Designated:
            {
                const auto value = "::" + declaration_name(reader_.get<DesignatedType>(index).decl, depth + 1);
                return suffix.empty() ? value : value + " " + suffix;
            }
            case TypeSort::Qualified:
            {
                const auto& value = reader_.get<QualifiedType>(index);
                std::string cv;
                if (has_flag(value.qualifiers, Qualifier::Const)) cv += "const ";
                if (has_flag(value.qualifiers, Qualifier::Volatile)) cv += "volatile ";
                if (has_flag(value.qualifiers, Qualifier::Restrict))
                    throw std::runtime_error("unsupported restricted IFC type");
                return declarator(value.unqualified_type, cv + suffix, depth + 1);
            }
            case TypeSort::Pointer: case TypeSort::LvalueReference: case TypeSort::RvalueReference:
            {
                TypeIndex pointee;
                std::string operation;
                if (index.sort() == TypeSort::Pointer) { pointee = reader_.get<PointerType>(index).pointee; operation = "*"; }
                else if (index.sort() == TypeSort::LvalueReference) { pointee = reader_.get<LvalueReferenceType>(index).referee; operation = "&"; }
                else { pointee = reader_.get<RvalueReferenceType>(index).referee; operation = "&&"; }
                suffix = operation + suffix;
                if (requires_parentheses(pointee)) suffix = "(" + suffix + ")";
                return declarator(pointee, suffix, depth + 1);
            }
            case TypeSort::Array:
            {
                const auto& value = reader_.get<ArrayType>(index);
                return declarator(value.element, suffix + "[" + expression(value.bound, depth + 1) + "]", depth + 1);
            }
            case TypeSort::Function:
            {
                const auto& value = reader_.get<FunctionType>(index);
                const auto calling = convention(value.convention, false);
                suffix.insert(suffix.starts_with('(') ? 1u : 0u, calling);
                return declarator(value.target, suffix + "(" + parameters(value.source, depth + 1) + ")" +
                                  qualifiers(value.traits) + noexcept_suffix(value.eh_spec), depth + 1);
            }
            case TypeSort::Syntactic:
            {
                const auto value = expression(reader_.get<SyntacticType>(index).expr, depth + 1);
                return suffix.empty() ? value : value + " " + suffix;
            }
            case TypeSort::Placeholder:
            {
                const auto& value = reader_.get<PlaceholderType>(index);
                if (!index_like::null(value.elaboration))
                    return declarator(value.elaboration, suffix, depth + 1);
                throw std::runtime_error("unresolved placeholder in IFC method signature");
            }
            default: throw std::runtime_error("unsupported IFC type in overloaded method signature");
            }
        }

        resolved_scope declaration_scope(DeclIndex index, const template_bindings& bindings, unsigned depth) const
        {
            check_depth(depth);
            switch (index.sort())
            {
            case DeclSort::Scope: return {index, &reader_.get<ScopeDecl>(index), bindings};
            case DeclSort::Alias: return scope(reader_.get<AliasDecl>(index).aliasee, bindings, depth + 1);
            case DeclSort::Specialization:
            {
                const auto& value = reader_.get<SpecializationDecl>(index);
                const auto& form = reader_.get(value.specialization_form);
                auto instance = template_scope(form.template_decl, form.arguments, bindings, depth + 1);
                if (value.decl.sort() == DeclSort::Scope)
                {
                    instance.index = value.decl;
                    instance.declaration = &reader_.get<ScopeDecl>(value.decl);
                }
                return instance;
            }
            case DeclSort::Prolongation:
                return declaration_scope(reader_.get<ProlongationDecl>(index).original_decl, bindings, depth + 1);
            case DeclSort::Reference:
                throw std::runtime_error("base belongs to another IFC; imported base resolution is not supported");
            default: throw std::runtime_error("unsupported IFC class base declaration");
            }
        }

        resolved_scope template_scope(DeclIndex index, ExprIndex arguments, const template_bindings& outer,
                                      unsigned depth) const
        {
            check_depth(depth);
            if (index.sort() == DeclSort::Reference)
            {
                const auto& reference = reader_.get<ReferenceDecl>(index);
                const std::string owner = reader_.get(reference.translation_unit.owner);
                throw std::runtime_error("unsupported inheritance: imported base template cannot be verified" +
                    (owner.empty() ? std::string{} : " (module " + owner + ")"));
            }
            if (index.sort() != DeclSort::Template)
                throw std::runtime_error("unsupported IFC template base declaration");
            // The parameterized primary alone does not prove which partial specialization
            // the compiler instantiated. Do not silently publish its potentially wrong base.
            for (const auto& specialization : reader_.partition<PartialSpecializationDecl>())
                if (reader_.get(specialization.specialization_form).template_decl == index)
                    throw std::runtime_error("unsupported inheritance: IFC template base has partial specializations");
            const auto& value = reader_.get<TemplateDecl>(index);
            if (value.chart.sort() != ChartSort::Unilevel)
                throw std::runtime_error("multilevel IFC template base parameters are unsupported");
            std::vector<ExprIndex> args;
            if (arguments.sort() == ExprSort::Tuple)
                for (const auto argument : reader_.sequence(reader_.get<TupleExpr>(arguments))) args.push_back(argument);
            else if (!index_like::null(arguments)) args.push_back(arguments);
            const auto parameters = reader_.sequence(reader_.get<UnilevelChart>(value.chart));
            if (args.size() != parameters.size())
                throw std::runtime_error("unresolved default or packed IFC template base arguments");
            auto bindings = outer;
            for (std::size_t i = 0; i < args.size(); ++i)
            {
                // An instantiated implementation base may carry a tag/counter enum argument.
                // Only type parameters can occur as base types; retain other semantic arguments
                // in the specialization while declining to reinterpret them as type indices.
                if (parameters[i].sort == ParameterSort::NonType) continue;
                if (args[i].sort() != ExprSort::Type)
                    throw std::runtime_error("unsupported IFC template base type argument");
                bindings[{parameters[i].level, parameters[i].position}] =
                    substitute(reader_.get<TypeExpr>(args[i]).denotation, outer);
            }
            auto result = declaration_scope(value.entity.decl, bindings, depth + 1);
            // MSVC's parameterized ScopeDecl has a mangled name and no source locus.
            // The enclosing TemplateDecl retains its author-facing identity and attributes.
            result.authoring_template = &value;
            return result;
        }
    };
}

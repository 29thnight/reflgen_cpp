#include "prsm.h"
#include <algorithm>
#include <map>
#include <string_view>

namespace reflgen::generator
{
    namespace
    {
        bool directive(std::string_view name)
        {
            return name == "interop" || name == "lifetime" || name == "property_get" || name == "property_set" ||
                   name == "effect" || name == "owner";
        }

        std::map<std::string, std::string> policy(const std::vector<attribute_use>& attributes, diagnostics& report)
        {
            std::map<std::string, std::string> result;
            for (const auto& a : attributes)
            {
                if (a.scope != "reflgen" || !directive(a.name))
                    continue;
                const auto& tokens = a.argument_tokens;
                if (!a.has_arguments || tokens.size() != 1 || tokens[0].size() < 3 || tokens[0].front() != '"' ||
                    tokens[0].back() != '"' || tokens[0].find('\\') != std::string::npos || result.contains(a.name))
                {
                    report.error("RG0301", a.position,
                                 "PrSM directive requires one nonempty ordinary string literal, once: " + a.name);
                    continue;
                }
                result[a.name] = tokens[0].substr(1, tokens[0].size() - 2);
            }
            return result;
        }

        std::optional<binding_scalar> scalar(const declaration_type& type)
        {
            if (type.is_volatile || type.is_const)
                return {};
            if (type.kind == "void")
                return binding_scalar::void_;
            if (type.kind == "bool" && type.size_bytes == 1)
                return binding_scalar::boolean;
            if (type.kind == "floating_point")
            {
                if (type.canonical_spelling == "float" && type.size_bytes == 4)
                    return binding_scalar::float32;
                if (type.canonical_spelling == "double" && type.size_bytes == 8)
                    return binding_scalar::float64;
                return {};
            }
            const bool signed_ = type.kind == "signed_integer";
            if (!signed_ && type.kind != "unsigned_integer")
                return {};
            switch (type.size_bytes)
            {
            case 1:
                return signed_ ? binding_scalar::int8 : binding_scalar::uint8;
            case 2:
                return signed_ ? binding_scalar::int16 : binding_scalar::uint16;
            case 4:
                return signed_ ? binding_scalar::int32 : binding_scalar::uint32;
            case 8:
                return signed_ ? binding_scalar::int64 : binding_scalar::uint64;
            default:
                return {};
            }
        }

        std::string hex(std::string_view value)
        {
            constexpr char digits[] = "0123456789abcdef";
            std::string result;
            for (const unsigned char c : value)
            {
                result += digits[c >> 4];
                result += digits[c & 15];
            }
            return result;
        }

        std::string c_type(binding_scalar type, bool native = false)
        {
            const auto name = binding_scalar_name(type);
            if (name == "void")
                return "void";
            if (name == "bool")
                return native ? "bool" : "uint8_t";
            if (name == "float32")
                return "float";
            if (name == "float64")
                return "double";
            return name + "_t";
        }

        std::string quote(std::string_view text)
        {
            std::string result = "\"";
            for (char c : text)
            {
                if (c == '\\' || c == '"')
                    result += '\\';
                result += c;
            }
            return result + '"';
        }
    } // namespace

    std::optional<binding_module> lower_prsm(const std::vector<class_model>& declarations, const target_model& target,
                                             const std::string& module, std::vector<binding_source> sources,
                                             diagnostics& report)
    {
        binding_module_input input;
        input.name = module;
        input.target = target;
        // libclang appends the detected MSVC version; IFC has no LLVM triple.
        // Both map to this explicitly supported Windows x64 C ABI profile.
        constexpr std::string_view windows_profile = "x86_64-pc-windows-msvc";
        if (input.target.triple.starts_with(windows_profile))
        {
            const auto suffix = std::string_view(input.target.triple).substr(windows_profile.size());
            if (!suffix.empty() && suffix.front() >= '0' && suffix.front() <= '9' &&
                std::all_of(suffix.begin(), suffix.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; }))
                input.target.triple = std::string(windows_profile);
        }
        input.sources = std::move(sources);
        for (const auto& type : declarations)
        {
            const auto type_policy = policy(type.attributes, report);
            const auto selected = type_policy.find("interop");
            if (type_policy.contains("owner") && (selected == type_policy.end() || selected->second != "prsm"))
                report.error("RG0301", type.position, "owner requires an explicitly selected PrSM type");
            if (selected != type_policy.end() && selected->second != "prsm" && selected->second != "c" &&
                selected->second != "csharp")
                report.error("RG0301", type.position, "unknown interop target");
            if (selected == type_policy.end() || selected->second != "prsm")
            {
                for (const auto& method : type.methods)
                {
                    const auto member_policy = policy(method.attributes, report);
                    if ((member_policy.contains("interop") && member_policy.at("interop") == "prsm") ||
                        member_policy.contains("property_get") || member_policy.contains("property_set") ||
                        member_policy.contains("effect") || member_policy.contains("owner"))
                        report.error("RG0301", method.position,
                                     "PrSM method requires an explicitly selected PrSM type");
                }
                continue;
            }
            if (type.nested || !type_policy.contains("lifetime") || type_policy.at("lifetime") != "borrowed" ||
                type_policy.contains("property_get") || type_policy.contains("property_set") ||
                type_policy.contains("effect"))
                report.error("RG0301", type.position,
                             "PrSM type requires borrowed lifetime and an ordinary nonnested class");
            std::size_t source = 0;
            if (!input.sources.empty() && input.sources[0].kind == binding_source_kind::header)
            {
                const auto found = std::find_if(input.sources.begin(), input.sources.end(),
                                                [&](const auto& s) { return s.name == type.position.file; });
                if (found == input.sources.end())
                    report.error("RG0301", type.position, "PrSM type source is not a selected input");
                else
                    source = static_cast<std::size_t>(found - input.sources.begin());
            }
            input.types.push_back({type.qualified_name, type.qualified_name, source});
            if (type_policy.contains("owner"))
            {
                binding_callable owner;
                owner.cpp_name = "binding_owner";
                owner.script_name = type_policy.at("owner");
                owner.symbol = "ce_prsm_bind_" + hex(module) + "_" + hex(type.qualified_name) + "_owner";
                owner.operation = binding_operation::owner;
                owner.effect = binding_effect::read;
                owner.result = binding_scalar::uint64;
                owner.handle_result = type.qualified_name;
                owner.cpp_signature = "handle:" + type.qualified_name + "()";
                owner.position = type.position;
                input.callables.push_back(std::move(owner));
            }
            for (const auto& method : type.methods)
            {
                const auto p = policy(method.attributes, report);
                if (!p.contains("interop"))
                {
                    if (p.contains("property_get") || p.contains("property_set") || p.contains("effect") ||
                        p.contains("lifetime") || p.contains("owner"))
                        report.error("RG0301", method.position, "PrSM method directives require interop(\"prsm\")");
                    continue;
                }
                if (p.at("interop") != "prsm")
                {
                    if (p.at("interop") != "c" && p.at("interop") != "csharp")
                        report.error("RG0301", method.position, "unknown interop target");
                    continue;
                }
                if (method.access != "public" || method.owner != type.qualified_name || method.is_static ||
                    method.is_deleted || method.is_consteval || method.is_overloaded || method.is_variadic ||
                    method.is_volatile || method.ref_qualifier != "none" || !method.qualifiers_known ||
                    !method.declaration_specifiers_known || p.contains("lifetime") || p.contains("owner") ||
                    (method.calling_convention != "c" && method.calling_convention != "default") ||
                    (method.exception_specification != "none" && method.exception_specification != "basic_noexcept"))
                {
                    report.error("RG0301", method.position,
                                 "unsupported or unknown PrSM method declaration: " + method.name);
                    continue;
                }
                binding_callable call;
                call.receiver = type.qualified_name;
                call.cpp_name = method.name;
                call.script_name = method.name;
                call.position = method.position;
                call.is_const = method.is_const;
                call.is_noexcept = method.exception_specification != "none";
                call.symbol = "ce_prsm_bind_" + hex(module) + "_" + hex(type.qualified_name) + "_" + hex(method.name);
                const auto result = scalar(method.return_type);
                if (!result)
                {
                    report.error("RG0302", method.position, "unsupported PrSM return type");
                    continue;
                }
                call.result = *result;
                call.native_result = method.return_type.canonical_spelling;
                for (const auto& parameter : method.parameter_declarations)
                {
                    const auto value = scalar(parameter.type);
                    if (!value || *value == binding_scalar::void_)
                        report.error("RG0302", parameter.position, "unsupported PrSM parameter type");
                    else
                    {
                        call.parameters.push_back(*value);
                        call.native_parameters.push_back(parameter.type.canonical_spelling);
                    }
                }
                if (p.contains("property_get") && p.contains("property_set"))
                    report.error("RG0301", method.position, "a method cannot be both getter and setter");
                if (p.contains("property_get"))
                {
                    call.operation = binding_operation::getter;
                    call.script_name = p.at("property_get");
                    call.effect = binding_effect::read;
                }
                if (p.contains("property_set"))
                {
                    call.operation = binding_operation::setter;
                    call.script_name = p.at("property_set");
                }
                if (p.contains("effect"))
                {
                    if (p.at("effect") == "read")
                        call.effect = binding_effect::read;
                    else if (p.at("effect") == "write")
                        call.effect = binding_effect::write;
                    else
                        report.error("RG0301", method.position, "unknown PrSM effect");
                }
                // Shared normalized signature: fixed scalar tokens, no frontend spelling/aliases.
                call.cpp_signature = binding_scalar_name(call.result) + "(";
                for (std::size_t i = 0; i < call.parameters.size(); ++i)
                    call.cpp_signature += (i ? "," : "") + binding_scalar_name(call.parameters[i]);
                call.cpp_signature += ")";
                if (call.is_const)
                    call.cpp_signature += " const";
                if (call.is_noexcept)
                    call.cpp_signature += " noexcept";
                input.callables.push_back(std::move(call));
            }
        }
        return build_binding_module(std::move(input), report);
    }

    prsm_output emit_prsm(const binding_module& module, const std::string& adapter_header, diagnostics& report)
    {
        if (adapter_header.empty() || adapter_header.find_first_of("\r\n\"<>") != std::string::npos)
        {
            report.error("RG0301", {}, "PrSM requires a safe adapter include path");
            return {};
        }
        prsm_output output;
        const auto stem = "reflgen_" + module.name() + "_prsm";
        const auto build_macro = "REFLGEN_PRSM_" + hex(module.name()) + "_BUILD";
        output.header =
            "// <auto-generated/> PrSM production binding ABI v1\n#pragma once\n#include <reflgen/prsm/binding_abi.h>\n"
            "#ifdef __cplusplus\n#define REFLGEN_PRSM_NOEXCEPT noexcept\nextern \"C\" {\n#else\n#define "
            "REFLGEN_PRSM_NOEXCEPT\n#endif\n";
        output.header += "#if defined(" + build_macro +
                         ") && defined(_WIN32)\n#define REFLGEN_PRSM_API __declspec(dllexport)\n"
                         "#elif defined(" +
                         build_macro +
                         ")\n#define REFLGEN_PRSM_API __attribute__((visibility(\"default\")))\n"
                         "#else\n#define REFLGEN_PRSM_API\n#endif\n";
        output.source = "// <auto-generated/> PrSM production binding ABI v1\n#define " + build_macro + "\n#include " +
                        quote(stem + ".h") +
                        "\n#include <reflgen/prsm/access.h>\n#include <type_traits>\n#include <limits>\n";
        // Target guards prevent using metadata lowered for another architecture/ABI.
        output.source += "static_assert(sizeof(void*) == 8);\n";
        if (module.target().triple == "x86_64-pc-windows-msvc")
            output.source += "#if !defined(_WIN32) || !defined(_M_X64)\n#error PrSM binding target mismatch\n#endif\n";
        else if (module.target().triple.starts_with("x86_64"))
            output.source +=
                "#if !defined(__linux__) || !defined(__x86_64__)\n#error PrSM binding target mismatch\n#endif\n";
        else
            output.source +=
                "#if !defined(__linux__) || !defined(__aarch64__)\n#error PrSM binding target mismatch\n#endif\n";
        for (const auto& source : module.sources())
            output.source += source.kind == binding_source_kind::header ? "#include " + quote(source.name) + "\n"
                                                                        : "import " + source.name + ";\n";
        output.source += "#include " + quote(adapter_header) + "\n";
        output.abi_table = "# reflgen.prsm.calls\t1\n";
        for (const auto& contract : module.callables())
        {
            const auto& call = contract.declaration;
            if (call.operation == binding_operation::owner)
            {
                const auto declaration = "int32_t REFLGEN_PRSM_BINDING_CDECL " + call.symbol + "(uint64_t* out_value)";
                output.header += "REFLGEN_PRSM_API " + declaration + " REFLGEN_PRSM_NOEXCEPT;\n";
                output.source += "extern \"C\" " + declaration +
                                 " noexcept {\n"
                                 "    if (!out_value) return REFLGEN_PRSM_BINDING_INVALID_ARGUMENT;\n"
                                 "    try { uint64_t handle = 0;\n"
                                 "        static_assert(std::is_same_v<decltype(&::reflgen::prsm::binding_owner<::" +
                                 call.handle_result +
                                 ">::get), int32_t(*)(uint64_t*)> || "
                                 "std::is_same_v<decltype(&::reflgen::prsm::binding_owner<::" +
                                 call.handle_result +
                                 ">::get), int32_t(*)(uint64_t*) noexcept>);\n"
                                 "        const auto status = ::reflgen::prsm::binding_owner<::" +
                                 call.handle_result +
                                 ">::get(&handle);\n"
                                 "        if (status != 0) return status;\n"
                                 "        ::reflgen::prsm::binding_access<::" +
                                 call.handle_result +
                                 "> access(handle);\n"
                                 "        static_assert(std::is_nothrow_destructible_v<decltype(access)>);\n"
                                 "        static_assert(noexcept(access.status()) && noexcept(access.get()));\n"
                                 "        static_assert(std::is_same_v<decltype(access.status()), int32_t> && "
                                 "std::is_same_v<decltype(access.get()), ::" +
                                 call.handle_result +
                                 "*>);\n"
                                 "        const auto checked = access.status(); if (checked != 0) return checked;\n"
                                 "        if (!access.get()) return REFLGEN_PRSM_BINDING_INVALID_HANDLE;\n"
                                 "        *out_value = handle; return 0;\n"
                                 "    } catch (...) { return REFLGEN_PRSM_BINDING_NATIVE_FAILURE; }\n}\n";
                output.abi_table += call.symbol + "\t\t" + call.script_name + "\t" + contract.abi_sha256 + "\t" +
                                    contract.abi_descriptor + "\n";
                continue;
            }
            std::string args = "uint64_t receiver", native_args, invocation;
            for (std::size_t i = 0; i < call.parameters.size(); ++i)
            {
                const auto name = "arg" + std::to_string(i);
                args += ", " + c_type(call.parameters[i]) + " " + name;
                native_args += (i ? ", " : "") + (call.native_parameters.empty() ? c_type(call.parameters[i], true)
                                                                                 : call.native_parameters[i]);
                invocation +=
                    (i ? ", " : "") + (call.parameters[i] == binding_scalar::boolean ? "(" + name + " != 0)" : name);
            }
            if (call.result != binding_scalar::void_)
                args += ", " + c_type(call.result) + "* out_value";
            const auto declaration = "int32_t REFLGEN_PRSM_BINDING_CDECL " + call.symbol + "(" + args + ")";
            output.header += "REFLGEN_PRSM_API " + declaration + " REFLGEN_PRSM_NOEXCEPT;\n";
            const auto suffix = std::string(call.is_const ? " const" : "") + (call.is_noexcept ? " noexcept" : "");
            output.source += "static_assert(std::is_same_v<decltype(&::" + call.receiver + "::" + call.cpp_name +
                             "), " + (call.native_result.empty() ? c_type(call.result, true) : call.native_result) +
                             " (::" + call.receiver + "::*)(" + native_args + ")" + suffix + ">);\n";
            if (call.result == binding_scalar::float32 || call.result == binding_scalar::float64 ||
                std::find(call.parameters.begin(), call.parameters.end(), binding_scalar::float32) !=
                    call.parameters.end() ||
                std::find(call.parameters.begin(), call.parameters.end(), binding_scalar::float64) !=
                    call.parameters.end())
                output.source += "static_assert(sizeof(float) == 4 && sizeof(double) == 8 && "
                                 "std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);\n";
            output.source += "extern \"C\" " + declaration + " noexcept\n{\n";
            if (call.result != binding_scalar::void_)
                output.source += "    if (!out_value) return REFLGEN_PRSM_BINDING_INVALID_ARGUMENT;\n";
            output.source += "    try {\n        ::reflgen::prsm::binding_access<::" + call.receiver +
                             "> access(receiver);\n"
                             "        static_assert(std::is_nothrow_destructible_v<decltype(access)>);\n"
                             "        static_assert(noexcept(access.status()) && noexcept(access.get()));\n"
                             "        static_assert(std::is_same_v<decltype(access.status()), int32_t>);\n"
                             "        static_assert(std::is_same_v<decltype(access.get()), ::" +
                             call.receiver +
                             "*>);\n"
                             "        const auto status = access.status();\n        if (status != 0) return status;\n"
                             "        auto* object = access.get();\n        if (!object) return "
                             "REFLGEN_PRSM_BINDING_INVALID_HANDLE;\n        ";
            if (call.result != binding_scalar::void_)
                output.source += "const auto result = ";
            output.source += "object->" + call.cpp_name + "(" + invocation + ");\n";
            if (call.result != binding_scalar::void_)
                output.source +=
                    "        *out_value = " +
                    std::string(call.result == binding_scalar::boolean ? "static_cast<uint8_t>(result ? 1 : 0)"
                                                                       : "result") +
                    ";\n";
            output.source += "        return REFLGEN_PRSM_BINDING_OK;\n    } catch (...) { return "
                             "REFLGEN_PRSM_BINDING_NATIVE_FAILURE; }\n}\n";
            output.abi_table += call.symbol + "\t" + call.receiver + "\t" + call.script_name + "\t" +
                                contract.abi_sha256 + "\t" + contract.abi_descriptor + "\n";
        }
        output.header +=
            "REFLGEN_PRSM_API const reflgen_prsm_binding_entry* REFLGEN_PRSM_BINDING_CDECL ce_prsm_table_" +
            hex(module.name()) + "(uint32_t* count) REFLGEN_PRSM_NOEXCEPT;\n";
        output.source += emit_prsm_native_table(module);
        output.catalog = emit_prsm_catalog(module);
        output.header += "#ifdef __cplusplus\n}\n#endif\n#undef REFLGEN_PRSM_NOEXCEPT\n#undef REFLGEN_PRSM_API\n";
        return output;
    }
} // namespace reflgen::generator

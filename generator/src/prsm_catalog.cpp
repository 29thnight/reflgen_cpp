#include "prsm.h"
namespace reflgen::generator
{
    namespace
    {
        std::string quoted(const std::string& value)
        {
            std::string result = "\"";
            for (char c : value)
            {
                if (c == '\\' || c == '"')
                    result += '\\';
                result += c;
            }
            return result + "\"";
        }
        std::string hex(const std::string& value)
        {
            constexpr char digits[] = "0123456789abcdef";
            std::string result;
            for (unsigned char c : value)
            {
                result += digits[c >> 4];
                result += digits[c & 15];
            }
            return result;
        }
    } // namespace
    std::string emit_prsm_catalog(const binding_module& module)
    {
        std::string out =
            "{\"format\":\"reflgen.prsm.bindings\",\"version\":1,\"abi\":\"reflgen.prsm.abi.v1\",\"module\":" +
            quoted(module.name()) + ",\"target\":{\"triple\":" + quoted(module.target().triple) +
            ",\"pointer_width\":64},\"types\":[";
        for (std::size_t i = 0; i < module.types().size(); ++i)
            out += std::string(i ? "," : "") + "{\"name\":" + quoted(module.types()[i].script_name) +
                   ",\"cpp_name\":" + quoted(module.types()[i].cpp_name) +
                   ",\"receiver\":" + quoted(binding_receiver_name(module.types()[i].receiver)) + "}";
        out += "]";
        // Value types (D-042). The key is absent for scalar-only modules, so their
        // catalog bytes are unchanged and older consumers keep accepting them.
        if (!module.values().empty())
        {
            out += ",\"values\":[";
            for (std::size_t i = 0; i < module.values().size(); ++i)
            {
                const auto& v = module.values()[i];
                out += std::string(i ? "," : "") + "{\"name\":" + quoted(v.script_name) +
                       ",\"cpp_name\":" + quoted(v.cpp_name);
                if (v.kind == binding_value_kind::record)
                {
                    out += ",\"kind\":\"record\",\"size\":" + std::to_string(v.size) +
                           ",\"align\":" + std::to_string(v.align) + ",\"fields\":[";
                    for (std::size_t f = 0; f < v.fields.size(); ++f)
                        out += std::string(f ? "," : "") + "{\"name\":" + quoted(v.fields[f].name) +
                               ",\"type\":" + quoted(binding_scalar_name(v.fields[f].type)) +
                               ",\"offset\":" + std::to_string(v.fields[f].offset) + "}";
                }
                else
                {
                    out += ",\"kind\":\"enum\",\"underlying\":" + quoted(binding_scalar_name(v.underlying)) +
                           ",\"cases\":[";
                    for (std::size_t c = 0; c < v.cases.size(); ++c)
                        out += std::string(c ? "," : "") + "{\"name\":" + quoted(v.cases[c].name) +
                               ",\"value\":" + quoted(v.cases[c].value) + "}";
                }
                out += "]}";
            }
            out += "]";
        }
        out += ",\"callables\":[";
        for (std::size_t n = 0; n < module.callables().size(); ++n)
        {
            const auto& contract = module.callables()[n];
            const auto& c = contract.declaration;
            const bool owner = c.operation == binding_operation::owner;
            const std::string kind = owner                                      ? "owner"
                                     : c.operation == binding_operation::method ? "method"
                                     : c.operation == binding_operation::getter ? "getter"
                                                                                : "setter";
            out += std::string(n ? "," : "") + "{\"name\":" + quoted(c.script_name) +
                   ",\"cpp_name\":" + quoted(c.cpp_name) +
                   ",\"receiver\":" + (owner ? std::string("null") : quoted(c.receiver)) +
                   ",\"operation\":" + quoted(kind) + ",\"signature\":" + quoted(c.cpp_signature) +
                   ",\"const\":" + (c.is_const ? "true" : "false") +
                   ",\"noexcept\":" + (c.is_noexcept ? "true" : "false") +
                   ",\"effect\":" + quoted(c.effect == binding_effect::read ? "read" : "write") +
                   ",\"result\":" +
                   quoted(owner ? "handle:" + c.handle_result : binding_slot_token(module, c.result, c.result_value)) +
                   ",\"params\":[";
            for (std::size_t i = 0; i < c.parameters.size(); ++i)
                out += std::string(i ? "," : "") +
                       quoted(binding_slot_token(module, c.parameters[i],
                                                 c.parameter_values.empty() ? std::string() : c.parameter_values[i]));
            out += "],\"symbol\":" + quoted(c.symbol) + ",\"abi_sha256\":" + quoted(contract.abi_sha256) + "}";
        }
        return out + "]}\n";
    }
    std::string emit_prsm_native_table(const binding_module& module)
    {
        std::string out;
        if (!module.callables().empty())
        {
            out = "static const reflgen_prsm_binding_entry binding_entries[] = {\n";
            for (const auto& contract : module.callables())
            {
                const auto& c = contract.declaration;
                out += "    {" + quoted(c.symbol) + ", " + std::to_string(c.symbol.size()) + ", {";
                for (std::size_t i = 0; i < contract.abi_sha256.size(); i += 2)
                    out += std::string(i ? "," : "") + "0x" + contract.abi_sha256.substr(i, 2);
                out += "}, reinterpret_cast<uintptr_t>(&" + c.symbol + ")},\n";
            }
            out += "};\n";
        }
        out += "extern \"C\" const reflgen_prsm_binding_entry* REFLGEN_PRSM_BINDING_CDECL ce_prsm_table_" +
               hex(module.name()) + "(uint32_t* count) noexcept { if (!count) return nullptr; *count = " +
               std::to_string(module.callables().size()) + "; return " +
               (module.callables().empty() ? "nullptr" : "binding_entries") + "; }\n";
        return out;
    }
} // namespace reflgen::generator

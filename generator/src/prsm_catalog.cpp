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
                   ",\"cpp_name\":" + quoted(module.types()[i].cpp_name) + "}";
        out += "],\"callables\":[";
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
                   ",\"result\":" + quoted(owner ? "handle:" + c.handle_result : binding_scalar_name(c.result)) +
                   ",\"params\":[";
            for (std::size_t i = 0; i < c.parameters.size(); ++i)
                out += std::string(i ? "," : "") + quoted(binding_scalar_name(c.parameters[i]));
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

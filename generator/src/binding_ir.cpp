#include "binding_ir.h"
#include "binding_hash.h"
#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace reflgen::generator
{
    namespace
    {
        bool identifier(std::string_view value)
        {
            const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
            return !value.empty() && letter(value.front()) &&
                   std::all_of(value.begin(), value.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
        }

        bool qualified(std::string_view value, std::string_view separator = "::")
        {
            for (;;)
            {
                const auto end = value.find(separator);
                if (!identifier(value.substr(0, end)))
                    return false;
                if (end == std::string_view::npos)
                    return true;
                value.remove_prefix(end + separator.size());
            }
        }

        bool text(std::string_view value)
        {
            return !value.empty() && value.size() <= 4096 &&
                   std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
        }

        std::string operation_name(binding_operation operation)
        {
            switch (operation)
            {
            case binding_operation::method:
                return "method";
            case binding_operation::getter:
                return "getter";
            case binding_operation::setter:
                return "setter";
            case binding_operation::owner:
                return "owner";
            }
            return {};
        }

        // Lengths count UTF-8 bytes, never code points or UTF-16 code units.
        void append(std::string& output, std::string_view value)
        {
            output += std::to_string(value.size()) + ":" + std::string(value) + ";";
        }

        std::string descriptor(const binding_module_input& module, const binding_callable& call)
        {
            std::string output;
            for (const std::string_view item :
                 {"reflgen.prsm.call.v1", "cdecl", "int32", "0", "typed_out_pointer", "no_out_pointer",
                  "generational_u64", "domain_generation_nominal_type", "borrowed_call_guard", "game_thread",
                  "caught_to_status", "u8_nonzero_in_normalized_out", "untouched", "synchronous"})
                append(output, item);
            for (const auto& item :
                 {module.name, module.target.triple, std::to_string(module.target.pointer_width), call.symbol,
                  operation_name(call.operation), call.receiver, call.cpp_name, call.cpp_signature,
                  call.effect == binding_effect::read ? std::string("read") : std::string("write"),
                  call.handle_result.empty() ? binding_scalar_name(call.result) : "handle:" + call.handle_result,
                  std::to_string(call.parameters.size())})
                append(output, item);
            for (const auto type : call.parameters)
                append(output, binding_scalar_name(type));
            return output;
        }
    } // namespace

    std::string binding_scalar_name(binding_scalar type)
    {
        switch (type)
        {
        case binding_scalar::void_:
            return "void";
        case binding_scalar::boolean:
            return "bool";
        case binding_scalar::int8:
            return "int8";
        case binding_scalar::int16:
            return "int16";
        case binding_scalar::int32:
            return "int32";
        case binding_scalar::int64:
            return "int64";
        case binding_scalar::uint8:
            return "uint8";
        case binding_scalar::uint16:
            return "uint16";
        case binding_scalar::uint32:
            return "uint32";
        case binding_scalar::uint64:
            return "uint64";
        case binding_scalar::float32:
            return "float32";
        case binding_scalar::float64:
            return "float64";
        }
        return {};
    }

    std::optional<binding_module> build_binding_module(binding_module_input input, diagnostics& report)
    {
        const auto fail = [&](const source_position& where, std::string message) {
            report.error("RG0300", where, std::move(message));
        };
        if (!text(input.name) || !qualified(input.name))
            fail({}, "invalid PrSM binding module identity");
        // These are generation contracts, not assertions that cross-target execution was tested.
        const std::set<std::string> targets{"x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu", "x86_64-pc-linux-gnu",
                                            "aarch64-unknown-linux-gnu"};
        if (input.target.pointer_width != 64 || !targets.contains(input.target.triple))
            fail({}, "unsupported PrSM binding target; an exact supported 64-bit target is required");
        if (input.sources.empty() || input.sources.size() > 4096 || input.types.size() > 4096 ||
            input.callables.size() > 4096)
        {
            fail({}, "PrSM binding source/type/callable budget exceeded or no source supplied");
            return std::nullopt;
        }
        std::set<std::pair<binding_source_kind, std::string>> source_names;
        for (const auto& source : input.sources)
        {
            if ((source.kind != binding_source_kind::header && source.kind != binding_source_kind::named_module) ||
                !text(source.name) ||
                (source.kind == binding_source_kind::named_module && !qualified(source.name, ".")) ||
                !source_names.emplace(source.kind, source.name).second)
                fail({}, "invalid or duplicate PrSM binding source");
        }
        std::set<std::string> cpp_types, script_types, symbols;
        for (const auto& type : input.types)
        {
            if (!text(type.cpp_name) || !qualified(type.cpp_name) || !text(type.script_name) ||
                !qualified(type.script_name) || type.source >= input.sources.size() ||
                !cpp_types.insert(type.cpp_name).second || !script_types.insert(type.script_name).second)
                fail({}, "invalid or duplicate PrSM nominal binding type: " + type.cpp_name);
        }
        std::set<std::pair<std::string, std::string>> cpp_members;
        std::map<std::pair<std::string, std::string>, std::vector<const binding_callable*>> members;
        for (const auto& call : input.callables)
        {
            const std::set<std::string> native_types{"void",
                                                     "bool",
                                                     "char",
                                                     "signed char",
                                                     "unsigned char",
                                                     "short",
                                                     "short int",
                                                     "unsigned short",
                                                     "unsigned short int",
                                                     "int",
                                                     "unsigned int",
                                                     "long",
                                                     "long int",
                                                     "unsigned long",
                                                     "unsigned long int",
                                                     "long long",
                                                     "long long int",
                                                     "unsigned long long",
                                                     "unsigned long long int",
                                                     "float",
                                                     "double",
                                                     "__int64",
                                                     "unsigned __int64"};
            if ((!call.native_result.empty() && !native_types.contains(call.native_result)) ||
                (!call.native_parameters.empty() && call.native_parameters.size() != call.parameters.size()))
                fail(call.position, "invalid PrSM native scalar signature");
            for (const auto& name : call.native_parameters)
                if (name == "void" || !native_types.contains(name))
                    fail(call.position, "invalid PrSM native parameter");
            const bool owner = call.operation == binding_operation::owner;
            if (owner && (!call.receiver.empty() || !cpp_types.contains(call.handle_result) ||
                          call.result != binding_scalar::uint64 || !call.parameters.empty() ||
                          call.effect != binding_effect::read || call.is_const || call.is_noexcept ||
                          call.cpp_name != "binding_owner"))
                fail(call.position, "invalid PrSM owner ingress");
            if (!owner && !call.handle_result.empty())
                fail(call.position, "handle results require owner ingress in v1");
            if ((!owner && !cpp_types.contains(call.receiver)) || !text(call.cpp_name) || !identifier(call.cpp_name) ||
                !text(call.cpp_signature) || !text(call.script_name) ||
                !(owner ? qualified(call.script_name) : identifier(call.script_name)) || !text(call.symbol) ||
                !identifier(call.symbol) || !call.symbol.starts_with("ce_prsm_bind_") ||
                !symbols.insert(call.symbol).second ||
                !cpp_members.emplace(owner ? "owner:" + call.handle_result : call.receiver, call.cpp_name).second)
                fail(call.position,
                     "invalid/duplicate PrSM callable, symbol, receiver or unsupported overload: " + call.symbol);
            if (operation_name(call.operation).empty() ||
                (call.effect != binding_effect::read && call.effect != binding_effect::write) ||
                binding_scalar_name(call.result).empty() || call.parameters.size() > 64)
                fail(call.position, "unsupported PrSM operation/effect/scalar or parameter budget");
            for (const auto type : call.parameters)
                if (type == binding_scalar::void_ || binding_scalar_name(type).empty())
                    fail(call.position, "PrSM parameters require a supported non-void scalar");
            if (call.operation == binding_operation::getter &&
                (!call.parameters.empty() || call.result == binding_scalar::void_ ||
                 call.effect != binding_effect::read))
                fail(call.position, "PrSM getter requires read, no parameters and a non-void result");
            if (call.operation == binding_operation::setter &&
                (call.parameters.size() != 1 || call.result != binding_scalar::void_ ||
                 call.effect != binding_effect::write))
                fail(call.position, "PrSM setter requires write, one parameter and a void result");
            if (!owner)
                members[{call.receiver, call.script_name}].push_back(&call);
        }
        for (const auto& [name, calls] : members)
        {
            if (calls.size() == 1 && calls.front()->operation != binding_operation::setter)
                continue;
            const auto* getter = calls.front();
            const auto* setter = calls.back();
            if (getter->operation == binding_operation::setter)
                std::swap(getter, setter);
            if (calls.size() != 2 || getter->operation != binding_operation::getter ||
                setter->operation != binding_operation::setter || setter->parameters.size() != 1 ||
                setter->parameters.front() != getter->result)
                fail(calls.front()->position, "duplicate member or inconsistent PrSM getter/setter: " + name.second);
        }
        std::vector<std::string> global_names(script_types.begin(), script_types.end());
        for (const auto& call : input.callables)
            if (call.operation == binding_operation::owner)
                global_names.push_back(call.script_name);
        const std::set<std::string> reserved_names{
            "void", "bool", "int", "uint", "float", "double", "int8", "int16", "int32", "int64",
            "uint8", "uint16", "uint32", "uint64", "float32", "float64"};
        for (std::size_t i = 0; i < global_names.size(); ++i)
        {
            const auto& a = global_names[i];
            if (reserved_names.contains(a) || a.starts_with("std::"))
                fail({}, "reserved PrSM global identity: " + a);
            for (std::size_t j = i + 1; j < global_names.size(); ++j)
            {
                const auto& b = global_names[j];
                if (a == b || a.starts_with(b + "::") || b.starts_with(a + "::"))
                    fail({}, "conflicting PrSM type/owner identity: " + a);
            }
        }
        if (report.has_errors())
            return std::nullopt;
        std::sort(input.types.begin(), input.types.end(),
                  [](const auto& a, const auto& b) { return a.cpp_name < b.cpp_name; });
        std::sort(input.callables.begin(), input.callables.end(),
                  [](const auto& a, const auto& b) { return a.symbol < b.symbol; });
        binding_module result;
        for (const auto& call : input.callables)
        {
            const auto abi = descriptor(input, call);
            result.calls_.push_back({call, abi, binding_sha256(abi)});
        }
        result.input_ = std::move(input);
        return result;
    }
} // namespace reflgen::generator

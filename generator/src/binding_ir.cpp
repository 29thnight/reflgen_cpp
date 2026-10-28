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

        const binding_value* find_value(const std::vector<binding_value>& values, const std::string& cpp_name)
        {
            const auto found = std::find_if(values.begin(), values.end(),
                                            [&](const binding_value& v) { return v.cpp_name == cpp_name; });
            return found == values.end() ? nullptr : &*found;
        }

        std::string slot_token(const std::vector<binding_value>& values, binding_scalar scalar, const std::string& value)
        {
            if (value.empty())
                return binding_scalar_name(scalar);
            const auto* found = find_value(values, value);
            if (found == nullptr)
                return {};
            return (found->kind == binding_value_kind::record ? "value:" : "enum:") + value;
        }

        std::string parameter_value(const binding_callable& call, std::size_t index)
        {
            return call.parameter_values.empty() ? std::string() : call.parameter_values[index];
        }

        // Every referenced value type's full definition, once, in first-appearance
        // order (result, then parameters). Calls without value types append nothing,
        // so their descriptor bytes are unchanged from scalar-only v1.
        void append_values(std::string& output, const binding_module_input& module, const binding_callable& call)
        {
            std::vector<std::string> order;
            const auto note = [&](const std::string& name) {
                if (!name.empty() && std::find(order.begin(), order.end(), name) == order.end())
                    order.push_back(name);
            };
            note(call.result_value);
            for (const auto& name : call.parameter_values)
                note(name);
            for (const auto& name : order)
            {
                const auto& value = *find_value(module.values, name);
                if (value.kind == binding_value_kind::record)
                {
                    for (const auto& item : {std::string("record"), value.cpp_name, std::to_string(value.size),
                                             std::to_string(value.align), std::to_string(value.fields.size())})
                        append(output, item);
                    for (const auto& field : value.fields)
                        for (const auto& item :
                             {field.name, binding_scalar_name(field.type), std::to_string(field.offset)})
                            append(output, item);
                }
                else
                {
                    for (const auto& item : {std::string("enum"), value.cpp_name, binding_scalar_name(value.underlying),
                                             std::to_string(value.cases.size())})
                        append(output, item);
                    for (const auto& item : value.cases)
                    {
                        append(output, item.name);
                        append(output, item.value);
                    }
                }
            }
        }

        bool numeric(binding_scalar type)
        {
            return type != binding_scalar::void_ && type != binding_scalar::boolean;
        }

        std::size_t scalar_width(binding_scalar type)
        {
            switch (type)
            {
            case binding_scalar::int8:
            case binding_scalar::uint8:
            case binding_scalar::boolean:
                return 1;
            case binding_scalar::int16:
            case binding_scalar::uint16:
                return 2;
            case binding_scalar::int32:
            case binding_scalar::uint32:
            case binding_scalar::float32:
                return 4;
            case binding_scalar::int64:
            case binding_scalar::uint64:
            case binding_scalar::float64:
                return 8;
            case binding_scalar::void_:
                break;
            }
            return 0;
        }

        // Canonical decimal that fits the underlying integer type.
        bool enumerator_value(std::string_view text, binding_scalar underlying)
        {
            const bool negative = !text.empty() && text.front() == '-';
            const auto digits = negative ? text.substr(1) : text;
            if (digits.empty() || digits.size() > 20 || (digits.size() > 1 && digits.front() == '0') ||
                (negative && digits == "0") ||
                !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return false;
            const bool is_signed = underlying == binding_scalar::int8 || underlying == binding_scalar::int16 ||
                                   underlying == binding_scalar::int32 || underlying == binding_scalar::int64;
            if (negative && !is_signed)
                return false;
            unsigned long long magnitude = 0;
            for (const char c : digits)
            {
                const unsigned long long digit = static_cast<unsigned long long>(c - '0');
                if (magnitude > (~0ULL - digit) / 10)
                    return false;
                magnitude = magnitude * 10 + digit;
            }
            const auto bits = scalar_width(underlying) * 8;
            if (!is_signed)
                return bits == 64 || magnitude < (1ULL << bits);
            const unsigned long long limit = 1ULL << (bits - 1); // |min| for negatives, max+1 for positives
            return negative ? magnitude <= limit : magnitude < limit;
        }

        std::string descriptor(const binding_module_input& module, const binding_callable& call)
        {
            std::string output;
            for (const std::string_view item :
                 {"reflgen.prsm.call.v1", "cdecl", "int32", "0", "typed_out_pointer", "no_out_pointer",
                  "generational_u64"})
                append(output, item);
            // The default handle kind keeps the original v1 descriptor bytes.
            append(output, binding_receiver_validation(call.receiver_kind));
            for (const std::string_view item :
                 {"borrowed_call_guard", "game_thread", "caught_to_status", "u8_nonzero_in_normalized_out",
                  "untouched", "synchronous"})
                append(output, item);
            for (const auto& item :
                 {module.name, module.target.triple, std::to_string(module.target.pointer_width), call.symbol,
                  operation_name(call.operation), call.receiver, call.cpp_name, call.cpp_signature,
                  call.effect == binding_effect::read ? std::string("read") : std::string("write"),
                  call.handle_result.empty() ? slot_token(module.values, call.result, call.result_value)
                                             : "handle:" + call.handle_result,
                  std::to_string(call.parameters.size())})
                append(output, item);
            for (std::size_t i = 0; i < call.parameters.size(); ++i)
                append(output, slot_token(module.values, call.parameters[i], parameter_value(call, i)));
            append_values(output, module, call);
            return output;
        }
    } // namespace

    const binding_value* binding_module::value(const std::string& cpp_name) const noexcept
    {
        return find_value(input_.values, cpp_name);
    }
    std::string binding_slot_token(const binding_module_input& module, binding_scalar scalar, const std::string& value)
    {
        return slot_token(module.values, scalar, value);
    }
    std::string binding_slot_token(const binding_module& module, binding_scalar scalar, const std::string& value)
    {
        return slot_token(module.values(), scalar, value);
    }

    std::string binding_receiver_name(binding_receiver kind)
    {
        switch (kind)
        {
        case binding_receiver::handle:
            return "handle";
        case binding_receiver::entity:
            return "entity";
        case binding_receiver::component:
            return "component";
        }
        return {};
    }
    std::string binding_receiver_validation(binding_receiver kind)
    {
        switch (kind)
        {
        case binding_receiver::handle:
            return "domain_generation_nominal_type";
        case binding_receiver::entity:
            return "entity_generation";
        case binding_receiver::component:
            return "entity_generation_component_lookup";
        }
        return {};
    }
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
        std::map<std::string, binding_receiver> receiver_kinds;
        for (const auto& type : input.types)
        {
            if (!text(type.cpp_name) || !qualified(type.cpp_name) || !text(type.script_name) ||
                !qualified(type.script_name) || type.source >= input.sources.size() ||
                binding_receiver_name(type.receiver).empty() || !cpp_types.insert(type.cpp_name).second ||
                !script_types.insert(type.script_name).second)
                fail({}, "invalid or duplicate PrSM nominal binding type: " + type.cpp_name);
            receiver_kinds[type.cpp_name] = type.receiver;
        }
        if (input.values.size() > 4096)
            fail({}, "PrSM binding value budget exceeded");
        for (const auto& value : input.values)
        {
            if (!text(value.cpp_name) || !qualified(value.cpp_name) || !text(value.script_name) ||
                !qualified(value.script_name) || value.source >= input.sources.size() ||
                !cpp_types.insert(value.cpp_name).second || !script_types.insert(value.script_name).second)
            {
                fail({}, "invalid or duplicate PrSM value type: " + value.cpp_name);
                continue;
            }
            std::set<std::string> names;
            if (value.kind == binding_value_kind::record)
            {
                const bool aligned = value.align >= 1 && value.align <= 16 && (value.align & (value.align - 1)) == 0;
                if (value.fields.empty() || value.fields.size() > 64 || !aligned || value.size == 0 ||
                    value.size > 4096 || value.size % value.align != 0 || !value.cases.empty())
                    fail({}, "unsupported PrSM value record layout: " + value.cpp_name);
                std::uint64_t end = 0;
                for (const auto& field : value.fields)
                {
                    const auto width = scalar_width(field.type);
                    if (!identifier(field.name) || !names.insert(field.name).second || !numeric(field.type) ||
                        field.offset < end || field.offset % width != 0 || field.offset + width > value.size)
                        fail({}, "unsupported PrSM value record field: " + value.cpp_name + "::" + field.name);
                    end = field.offset + width;
                }
            }
            else
            {
                const auto underlying = value.underlying;
                if (value.cases.empty() || value.cases.size() > 4096 || !numeric(underlying) ||
                    underlying == binding_scalar::float32 || underlying == binding_scalar::float64 ||
                    !value.fields.empty())
                    fail({}, "unsupported PrSM value enumeration: " + value.cpp_name);
                std::set<std::string> numbers;
                for (const auto& item : value.cases)
                    if (!identifier(item.name) || !names.insert(item.name).second ||
                        !enumerator_value(item.value, underlying) || !numbers.insert(item.value).second)
                        fail({}, "invalid or duplicate PrSM enumerator (each case needs a distinct value): " +
                                     value.cpp_name + "::" + item.name);
            }
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
            if (!call.parameter_values.empty() && call.parameter_values.size() != call.parameters.size())
                fail(call.position, "invalid PrSM value slot list");
            const auto value_slot = [&](const std::string& name) {
                return name.empty() || find_value(input.values, name) != nullptr;
            };
            if (!value_slot(call.result_value) ||
                !std::all_of(call.parameter_values.begin(), call.parameter_values.end(), value_slot))
                fail(call.position, "PrSM value slot does not name a selected value type");
            // Value slots keep the exact native spelling (T, const T&) for the generated member assertion.
            if ((!call.native_result.empty() && call.result_value.empty() &&
                 !native_types.contains(call.native_result)) ||
                (!call.result_value.empty() && !text(call.native_result)) ||
                (!call.native_parameters.empty() && call.native_parameters.size() != call.parameters.size()))
                fail(call.position, "invalid PrSM native scalar signature");
            for (std::size_t i = 0; i < call.native_parameters.size(); ++i)
            {
                const auto& name = call.native_parameters[i];
                if (parameter_value(call, i).empty() ? (name == "void" || !native_types.contains(name)) : !text(name))
                    fail(call.position, "invalid PrSM native parameter");
            }
            const bool has_values = !call.result_value.empty() ||
                                    std::any_of(call.parameter_values.begin(), call.parameter_values.end(),
                                                [](const std::string& v) { return !v.empty(); });
            const bool owner = call.operation == binding_operation::owner;
            const bool void_result = call.result_value.empty() && call.result == binding_scalar::void_;
            if (owner && (!call.receiver.empty() || !receiver_kinds.contains(call.handle_result) || has_values ||
                          call.result != binding_scalar::uint64 || !call.parameters.empty() ||
                          call.effect != binding_effect::read || call.is_const || call.is_noexcept ||
                          call.cpp_name != "binding_owner"))
                fail(call.position, "invalid PrSM owner ingress");
            if (!owner && !call.handle_result.empty())
                fail(call.position, "handle results require owner ingress in v1");
            const auto kind = receiver_kinds.find(owner ? call.handle_result : call.receiver);
            if (kind != receiver_kinds.end() && kind->second != call.receiver_kind)
                fail(call.position, "PrSM callable receiver kind disagrees with its nominal type: " + call.symbol);
            if ((!owner && !receiver_kinds.contains(call.receiver)) || !text(call.cpp_name) || !identifier(call.cpp_name) ||
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
            for (std::size_t i = 0; i < call.parameters.size(); ++i)
                if (parameter_value(call, i).empty() &&
                    (call.parameters[i] == binding_scalar::void_ || binding_scalar_name(call.parameters[i]).empty()))
                    fail(call.position, "PrSM parameters require a supported non-void scalar or value type");
            if (call.operation == binding_operation::getter &&
                (!call.parameters.empty() || void_result || call.effect != binding_effect::read))
                fail(call.position, "PrSM getter requires read, no parameters and a non-void result");
            if (call.operation == binding_operation::setter &&
                (call.parameters.size() != 1 || !void_result || call.effect != binding_effect::write))
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
                slot_token(input.values, setter->parameters.front(), parameter_value(*setter, 0)) !=
                    slot_token(input.values, getter->result, getter->result_value))
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
        std::sort(input.values.begin(), input.values.end(),
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

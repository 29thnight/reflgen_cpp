#pragma once
// Frontend-independent, explicitly selected PrSM contracts. Header/IFC readers
// must prove declaration facts before building this IR; reflection is not consent.
#include "diagnostics.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reflgen::generator
{
    enum class binding_source_kind
    {
        header,
        named_module
    };
    struct binding_source
    {
        binding_source_kind kind = binding_source_kind::header;
        std::string name; // include path or primary module import name; never ABI identity
    };

    enum class binding_scalar
    {
        void_,
        boolean,
        int8,
        int16,
        int32,
        int64,
        uint8,
        uint16,
        uint32,
        uint64,
        float32,
        float64
    };
    enum class binding_operation
    {
        method,
        getter,
        setter,
        owner
    };
    enum class binding_effect
    {
        read,
        write
    };

    // How the uint64_t receiver bits identify the native object. The engine
    // supplies one adapter per kind, never per selected type for entity/component:
    //   handle    - a type-specific registry handle; binding_access<T>/binding_owner<T>
    //   entity    - the engine entity generation handle; entity_access<T>/entity_owner<T>
    //   component - the owning entity's generation handle, resolved to that entity's
    //               component of type T on every call; component_access<T>/component_owner<T>
    // Entity and component receivers share the entity handle domain, so a script
    // never holds a component-specific handle or a cached component address.
    enum class binding_receiver
    {
        handle,
        entity,
        component
    };

    struct binding_type
    {
        std::string cpp_name; // canonical nominal identity, independent of input location
        std::string script_name;
        std::size_t source = 0;
        binding_receiver receiver = binding_receiver::handle;
    };

    // A selected PrSM value type (D-042). Records are trivially copyable,
    // standard-layout aggregates of numeric scalars passed through pointers in
    // the C wrapper, never as by-value C aggregates whose lowering is target
    // specific. Enumerations cross as a uint32 case index in declaration order;
    // the wrapper maps it to and from the native enumerator value.
    enum class binding_value_kind
    {
        record,
        enumeration
    };
    struct binding_value_field
    {
        std::string name;
        binding_scalar type = binding_scalar::int32;
        std::uint64_t offset = 0;
    };
    struct binding_value_case
    {
        std::string name;
        std::string value; // canonical decimal of the native enumerator value
    };
    struct binding_value
    {
        std::string cpp_name; // canonical nominal C++ identity of the target type
        std::string script_name;
        std::size_t source = 0;
        binding_value_kind kind = binding_value_kind::record;
        std::uint64_t size = 0;
        std::uint64_t align = 0;
        std::vector<binding_value_field> fields;
        binding_scalar underlying = binding_scalar::int32;
        std::vector<binding_value_case> cases;
    };

    struct binding_callable
    {
        std::string receiver; // canonical C++ nominal type; receiver ABI is uint64_t
        std::string cpp_name;
        std::string cpp_signature; // normalized native declaration signature, NOT the lowered C prototype
        std::string script_name;
        std::string symbol;
        binding_operation operation = binding_operation::method;
        binding_effect effect = binding_effect::write;
        binding_scalar result = binding_scalar::void_;
        std::vector<binding_scalar> parameters;
        source_position position;
        bool is_const = false;
        bool is_noexcept = false;
        std::string native_result;
        std::vector<std::string> native_parameters;
        std::string handle_result;
        // Receiver kind of `receiver` (or of `handle_result` for owner ingress).
        binding_receiver receiver_kind = binding_receiver::handle;
        // Value-type slots: the cpp_name of a selected binding_value, or empty for a
        // scalar slot. `parameter_values` is empty or has one entry per parameter.
        // A value slot's scalar entry (`result`/`parameters[i]`) is ignored.
        std::string result_value;
        std::vector<std::string> parameter_values;
    };

    struct binding_module_input
    {
        std::string name;
        target_model target;
        std::vector<binding_source> sources;
        std::vector<binding_type> types;
        std::vector<binding_value> values;
        std::vector<binding_callable> callables;
    };

    struct binding_call_contract
    {
        binding_callable declaration;
        std::string abi_descriptor;
        std::string abi_sha256; // 64 lowercase hex digits; SHA-256 of descriptor UTF-8 bytes
    };

    // Only the builder can construct a validated module. Emitters consume this,
    // never frontend AST nodes or partially validated binding_module_input.
    class binding_module
    {
      public:
        const std::string& name() const noexcept { return input_.name; }
        const target_model& target() const noexcept { return input_.target; }
        const std::vector<binding_source>& sources() const noexcept { return input_.sources; }
        const std::vector<binding_type>& types() const noexcept { return input_.types; }
        const std::vector<binding_value>& values() const noexcept { return input_.values; }
        const binding_value* value(const std::string& cpp_name) const noexcept;
        const std::vector<binding_call_contract>& callables() const noexcept { return calls_; }

      private:
        binding_module_input input_;
        std::vector<binding_call_contract> calls_;
        binding_module() = default;
        friend std::optional<binding_module> build_binding_module(binding_module_input, diagnostics&);
    };

    std::optional<binding_module> build_binding_module(binding_module_input input, diagnostics& report);
    std::string binding_scalar_name(binding_scalar type);
    // ABI token of one slot: a scalar name, `value:<cpp>` (record) or `enum:<cpp>`.
    std::string binding_slot_token(const binding_module_input& module, binding_scalar scalar,
                                   const std::string& value);
    std::string binding_slot_token(const binding_module& module, binding_scalar scalar, const std::string& value);
    std::string binding_receiver_name(binding_receiver kind);
    // Validation field of the ABI descriptor for one receiver kind.
    std::string binding_receiver_validation(binding_receiver kind);
} // namespace reflgen::generator

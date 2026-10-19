#pragma once
// Frontend-independent, explicitly selected PrSM contracts. Header/IFC readers
// must prove declaration facts before building this IR; reflection is not consent.
#include "diagnostics.h"
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

    struct binding_type
    {
        std::string cpp_name; // canonical nominal identity, independent of input location
        std::string script_name;
        std::size_t source = 0;
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
    };

    struct binding_module_input
    {
        std::string name;
        target_model target;
        std::vector<binding_source> sources;
        std::vector<binding_type> types;
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
        const std::vector<binding_call_contract>& callables() const noexcept { return calls_; }

      private:
        binding_module_input input_;
        std::vector<binding_call_contract> calls_;
        binding_module() = default;
        friend std::optional<binding_module> build_binding_module(binding_module_input, diagnostics&);
    };

    std::optional<binding_module> build_binding_module(binding_module_input input, diagnostics& report);
    std::string binding_scalar_name(binding_scalar type);
} // namespace reflgen::generator

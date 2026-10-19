#pragma once
#include "binding_ir.h"

namespace reflgen::generator
{
    // Shared semantic lowering for both libclang and IFC declaration facts.
    std::optional<binding_module> lower_prsm(const std::vector<class_model>& declarations, const target_model& target,
                                             const std::string& module, std::vector<binding_source> sources,
                                             diagnostics& report);
    struct prsm_output
    {
        std::string header;
        std::string source;
        std::string abi_table; // diagnostic TSV; not the PrSM compiler catalog
        std::string catalog;
    };
    prsm_output emit_prsm(const binding_module& module, const std::string& adapter_header, diagnostics& report);
    std::string emit_prsm_catalog(const binding_module& module);
    std::string emit_prsm_native_table(const binding_module& module);
} // namespace reflgen::generator

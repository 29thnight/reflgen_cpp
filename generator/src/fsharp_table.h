#pragma once
// Direct F# bindings for explicitly selected, host-injected native function tables.
// This backend does not introduce an object wrapper ABI, DLL imports or a C# assembly.
#include "diagnostics.h"
#include "model.h"
#include <string>
#include <vector>

namespace reflgen::generator
{
    struct fsharp_table_options
    {
        std::string module_name;
        std::string managed_namespace = "Reflgen.Generated";
    };

    struct fsharp_table_output
    {
        std::string fsharp_source;
        std::string cpp_source;
        std::string manifest_json;
        std::string fingerprint;
    };

    // Any error leaves every output empty. The caller publishes all outputs only after validating
    // every requested backend. Generated native layout/signature assertions must be compiled by the host.
    fsharp_table_output emit_fsharp_tables(const std::vector<native_table_model>& tables,
                                          const target_model& target, const fsharp_table_options& options,
                                          diagnostics& report);
}

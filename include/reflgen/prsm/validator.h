#pragma once
#include "binding_abi.h"
#include <cstring>
namespace reflgen::prsm
{
    struct binding_table_view
    {
        const reflgen_prsm_binding_entry* entries;
        uint32_t count;
    };
    // Host must validate context/thread first. Only compiled native tables are trusted.
    inline int32_t validate_binding(const binding_table_view* tables, uint32_t table_count, const uint8_t* symbol,
                                    uint32_t length, const uint8_t* digest) noexcept
    {
        if (!tables || !symbol || !digest || length == 0 || length > 4096 || table_count > 4096)
            return REFLGEN_PRSM_BINDING_INVALID_ARGUMENT;
        const reflgen_prsm_binding_entry* found = nullptr;
        for (uint32_t t = 0; t < table_count; ++t)
        {
            if (tables[t].count > 4096 || (tables[t].count && !tables[t].entries))
                return REFLGEN_PRSM_BINDING_ABI_MISMATCH;
            for (uint32_t i = 0; i < tables[t].count; ++i)
            {
                const auto& entry = tables[t].entries[i];
                if (entry.symbol_length == length && entry.symbol && std::memcmp(entry.symbol, symbol, length) == 0)
                {
                    if (found)
                        return REFLGEN_PRSM_BINDING_ABI_MISMATCH;
                    found = &entry;
                }
            }
        }
        return found && found->address && std::memcmp(found->digest, digest, 32) == 0
                   ? REFLGEN_PRSM_BINDING_OK
                   : REFLGEN_PRSM_BINDING_ABI_MISMATCH;
    }
} // namespace reflgen::prsm

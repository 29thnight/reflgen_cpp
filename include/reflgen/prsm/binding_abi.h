#ifndef REFLGEN_PRSM_BINDING_ABI_H
#define REFLGEN_PRSM_BINDING_ABI_H
/* Production engine-binding extension, separate from PrSM's module ABI and
 * reflgen's borrowed-address interop. See docs/prsm-production-abi.md.
 * This header declares no implementation and does not select script exports. */
#include <stdint.h>

#define REFLGEN_PRSM_BINDING_ABI_VERSION 1u
#define REFLGEN_PRSM_BINDING_DIGEST_BYTES 32u

#if defined(_WIN32)
#define REFLGEN_PRSM_BINDING_CDECL __cdecl
#else
#define REFLGEN_PRSM_BINDING_CDECL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /* Return int32_t, not a platform-sized C enum. Every nonzero code is failure. */
    typedef int32_t reflgen_prsm_binding_status;
    enum
    {
        REFLGEN_PRSM_BINDING_OK = 0,
        REFLGEN_PRSM_BINDING_INVALID_ARGUMENT = 1,
        REFLGEN_PRSM_BINDING_NATIVE_FAILURE = 2,
        REFLGEN_PRSM_BINDING_INVALID_HANDLE = 3,
        REFLGEN_PRSM_BINDING_WRONG_THREAD = 4,
        REFLGEN_PRSM_BINDING_ABI_MISMATCH = 5,
        REFLGEN_PRSM_BINDING_INVALID_CONTEXT = 6
    };

    /* uint64_t is opaque bits; do not duplicate the engine registry's bit layout. */
    typedef uint64_t reflgen_prsm_binding_handle;
    typedef struct reflgen_prsm_binding_entry
    {
        const char* symbol;
        uint32_t symbol_length;
        uint8_t digest[REFLGEN_PRSM_BINDING_DIGEST_BYTES];
        uintptr_t address; /* native host/JIT registration only; never script data */
    } reflgen_prsm_binding_entry;

    /* ce_prsm_validate_binding implements this protocol. Length excludes a NUL;
 * digest32 addresses exactly 32 bytes, not hex text. Callable symbols must
 * instead use the reserved ce_prsm_bind_ prefix. */
    typedef reflgen_prsm_binding_status(REFLGEN_PRSM_BINDING_CDECL* reflgen_prsm_validate_binding_fn)(
        const uint8_t* symbol_utf8, uint32_t symbol_length, const uint8_t* digest32);

#ifdef __cplusplus
}
static_assert(sizeof(reflgen_prsm_binding_status) == 4, "PrSM binding status width");
static_assert(sizeof(reflgen_prsm_binding_handle) == 8, "PrSM binding handle width");
#endif
#endif

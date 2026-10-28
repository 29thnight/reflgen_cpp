#include "reflgen/prsm/binding_abi.h"

typedef char binding_status_width[(sizeof(reflgen_prsm_binding_status) == 4) ? 1 : -1];
typedef char binding_handle_width[(sizeof(reflgen_prsm_binding_handle) == 8) ? 1 : -1];

static reflgen_prsm_binding_status REFLGEN_PRSM_BINDING_CDECL reject_binding(const uint8_t* symbol, uint32_t length,
                                                                             const uint8_t* digest)
{
    (void)symbol;
    (void)length;
    (void)digest;
    return REFLGEN_PRSM_BINDING_ABI_MISMATCH;
}

int reflgen_prsm_binding_abi_c_probe(void)
{
    reflgen_prsm_validate_binding_fn validator = &reject_binding;
    return validator(0, 0, 0);
}

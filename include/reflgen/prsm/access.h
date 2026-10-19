#pragma once
#include "binding_abi.h"

namespace reflgen::prsm
{
    // Engine adapter specializes this for each selected nominal type. Required:
    // explicit binding_access(uint64_t), noexcept status() -> int32_t,
    // noexcept get() -> T*, and a noexcept destructor. It validates context,
    // game thread, registry domain/generation/type and holds the call lifetime.
    // There is deliberately no raw-pointer fallback.
    template<class T>
    class binding_access;
    template<class T>
    class binding_owner;
} // namespace reflgen::prsm

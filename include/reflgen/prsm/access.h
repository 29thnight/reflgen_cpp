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

    // receiver("entity") / receiver("component") types. The engine defines each
    // template once, generically, for its entity class and for every component
    // class (for example constrained by std::derived_from<T, Component>); new
    // selected types need no adapter code. The receiver bits are the engine's
    // entity generation handle in both kinds. component_access<T> resolves the
    // live entity, then that entity's component of type T, on every call and
    // keeps neither a component handle nor an address across calls. The same
    // members as binding_access/binding_owner are required.
    template<class T>
    class entity_access;
    template<class T>
    class entity_owner;
    template<class T>
    class component_access;
    template<class T>
    class component_owner;
} // namespace reflgen::prsm

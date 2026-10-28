#pragma once
// Generic per-kind engine adapter for the scene fixture: one entity_access and
// one component_access for every component type, no per-type specialization.
#include <reflgen/prsm/access.h>
#include <concepts>

namespace scene_fixture
{
    inline scene::Entity* entity = nullptr;
    inline uint64_t entity_handle = 0x0000000700000003ULL;
    inline bool context = true;
    inline int lookups = 0;

    inline scene::Entity* Resolve(uint64_t handle) noexcept
    {
        return context && handle == entity_handle ? entity : nullptr;
    }
    template<class T>
    T* Find(scene::Entity& owner) noexcept
    {
        ++lookups;
        if constexpr (std::same_as<T, scene::LightComponent>)
            return owner.light;
        else
            return nullptr;
    }
} // namespace scene_fixture

namespace reflgen::prsm
{
    template<class T>
    class entity_access
    {
      public:
        explicit entity_access(uint64_t handle) noexcept
        {
            if (!scene_fixture::context)
                status_ = REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
            else if (!(object_ = scene_fixture::Resolve(handle)))
                status_ = REFLGEN_PRSM_BINDING_INVALID_HANDLE;
        }
        entity_access(const entity_access&) = delete;
        ~entity_access() noexcept = default;
        int32_t status() const noexcept { return status_; }
        T* get() noexcept { return object_; }

      private:
        int32_t status_ = REFLGEN_PRSM_BINDING_OK;
        T* object_ = nullptr;
    };
    template<class T>
    class entity_owner
    {
      public:
        static int32_t get(uint64_t* output) noexcept
        {
            *output = scene_fixture::entity_handle;
            return scene_fixture::context ? REFLGEN_PRSM_BINDING_OK : REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
        }
    };

    template<class T>
        requires std::derived_from<T, scene::Component>
    class component_access<T>
    {
      public:
        explicit component_access(uint64_t handle) noexcept
        {
            scene::Entity* owner = scene_fixture::context ? scene_fixture::Resolve(handle) : nullptr;
            if (!scene_fixture::context)
                status_ = REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
            else if (!owner || !(object_ = scene_fixture::Find<T>(*owner)))
                status_ = REFLGEN_PRSM_BINDING_INVALID_HANDLE;
        }
        component_access(const component_access&) = delete;
        ~component_access() noexcept = default;
        int32_t status() const noexcept { return status_; }
        T* get() noexcept { return object_; }

      private:
        int32_t status_ = REFLGEN_PRSM_BINDING_OK;
        T* object_ = nullptr;
    };
    template<class T>
        requires std::derived_from<T, scene::Component>
    class component_owner<T>
    {
      public:
        static int32_t get(uint64_t* output) noexcept
        {
            *output = scene_fixture::entity_handle;
            return scene_fixture::context ? REFLGEN_PRSM_BINDING_OK : REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
        }
    };
} // namespace reflgen::prsm

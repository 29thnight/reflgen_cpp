#pragma once
// Entity/component receiver fixture: script handles are entity generation
// handles; component receivers resolve the entity's component on every call.
namespace reflgen
{
    struct access;
}

namespace scene
{
    class Component
    {
      public:
        virtual ~Component() = default;
    };

    class [[reflgen::reflect, reflgen::interop("prsm"), reflgen::lifetime("borrowed"), reflgen::receiver("component"),
            reflgen::owner("scene::Light")]] LightComponent : public Component
    {
      public:
        [[reflgen::interop("prsm"), reflgen::property_get("Intensity")]]
        float GetIntensity() const noexcept
        {
            return intensity_;
        }
        [[reflgen::interop("prsm"), reflgen::property_set("Intensity")]]
        void SetIntensity(float value) noexcept
        {
            intensity_ = value;
        }
        float Hidden() const { return -1.0f; }

      private:
        friend struct reflgen::access;

        float intensity_ = 2.0f;
    };

    class [[reflgen::interop("prsm"), reflgen::lifetime("borrowed"), reflgen::receiver("entity"),
            reflgen::owner("scene::Self")]] Entity
    {
      public:
        [[reflgen::interop("prsm"), reflgen::property_get("Enabled")]]
        bool IsEnabled() const noexcept
        {
            return enabled_;
        }
        [[reflgen::interop("prsm"), reflgen::property_set("Enabled")]]
        void SetEnabled(bool value) noexcept
        {
            enabled_ = value;
        }
        LightComponent* light = nullptr;

      private:
        bool enabled_ = true;
    };
} // namespace scene

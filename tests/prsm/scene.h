#pragma once
// Entity/component receiver fixture: script handles are entity generation
// handles; component receivers resolve the entity's component on every call.
// Value types (D-042) come from a "third-party" namespace that carries no
// annotations; engine-owned alias declarations give the consent.
namespace reflgen
{
    struct access;
}

namespace thirdparty
{
    struct vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };
    // Non-contiguous, negative and out-of-order values: script sees case indices.
    enum class shade : int
    {
        dim = 4,
        off = -1,
        bright = 9
    };
} // namespace thirdparty

namespace scene
{
    using Vector3 [[reflgen::interop("prsm"), reflgen::value("scene::Vector3")]] = thirdparty::vec3;
    using Shade [[reflgen::interop("prsm"), reflgen::value("scene::Shade")]] = thirdparty::shade;

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
        // Record by const reference in both directions.
        [[reflgen::interop("prsm"), reflgen::property_get("Position")]]
        const thirdparty::vec3& GetPosition() const noexcept
        {
            return position;
        }
        [[reflgen::interop("prsm"), reflgen::property_set("Position")]]
        void SetPosition(const thirdparty::vec3& value) noexcept
        {
            position = value;
        }
        // Enumeration by value; a record argument by value next to a scalar.
        [[reflgen::interop("prsm"), reflgen::property_get("Shade")]]
        thirdparty::shade GetShade() const noexcept
        {
            return shade;
        }
        [[reflgen::interop("prsm"), reflgen::property_set("Shade")]]
        void SetShade(thirdparty::shade value) noexcept
        {
            shade = value;
        }
        [[reflgen::interop("prsm")]]
        float Move(thirdparty::vec3 delta, float scale) noexcept
        {
            position.x += delta.x * scale;
            position.y += delta.y * scale;
            position.z += delta.z * scale;
            return position.x + position.y + position.z;
        }
        LightComponent* light = nullptr;
        thirdparty::vec3 position{1.0f, 2.0f, 3.0f};
        thirdparty::shade shade = thirdparty::shade::dim;

      private:
        bool enabled_ = true;
    };
} // namespace scene

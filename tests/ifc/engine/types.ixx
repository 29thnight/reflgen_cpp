module;
#include <memory>
#include "reflgen/core/hook.h"
export module reflgen.test.engine;

export namespace engine_fixture
{
    struct [[reflgen::ignore]] opaque_value
    {
        int code = 11;
    };

    struct [[reflgen::reflect]] component_base
    {
        int entity_id = 17;
        void inherited_add(int amount) { entity_id += amount; }
        int choose(int value) const noexcept { return entity_id + value; }
    };

    template<class Derived, class Base>
    struct [[reflgen::ignore]] component_node : Base
    {
        // The CRTP layer is an implementation detail, not another serialized base.
        [[reflgen::ignore]] Derived& derived() { return static_cast<Derived&>(*this); }
    };

    struct [[reflgen::ignore]] storage_root
    {
        int internal_storage = 2;
    };

    struct [[reflgen::ignore]] counted : public storage_root
    {
        int internal_count = 1;
    };

    struct [[reflgen::ignore]] protected_state
    {
        int internal_state = 3;
    };

    // Non-public helper-only paths stay valid; the public CRTP sibling still exposes its reflected base.
    class [[reflgen::reflect]] component
        : private counted, protected protected_state, public component_node<component, component_base>
    {
        friend struct ::reflgen::access;
        [[reflgen::serialized_name("secret")]] int secret_ = 9;
        int hidden_call(int value) const noexcept { return secret_ + value; }

      protected:
        int protected_ = 10;

      public:
        component() = default;

        enum class mode : unsigned { idle = 2, active = 5 };
        struct settings { int limit = 32; };

        std::shared_ptr<component_base> owner = std::make_shared<component_base>();
        opaque_value external;
        settings configuration;
        mode state = mode::active;

        int apply(int value) & noexcept { return entity_id + value; }
        int apply(float value) const & noexcept { return entity_id + static_cast<int>(value) * 2; }
        using component_base::choose;
        int choose(float value) const noexcept { return entity_id + static_cast<int>(value) * 3; }
        int pick() & noexcept { return 1; }
        int pick() const & noexcept { return 2; }
        int pick() && noexcept { return 3; }
    };

    // Automatic export discovery must not turn private implementation into schema.
    class legacy_public_only
    {
        int hidden_ = 21;
        [[maybe_unused]] int overloaded(int value) { return value + hidden_; }
      public:
        int visible = 22;
        int overloaded(float value) { return static_cast<int>(value) + visible; }
    };

    // Ignored implementation fields require no access grant, even for explicit reflection.
    class [[reflgen::reflect]] explicitly_ignored_private
    {
        [[reflgen::ignore]] int ignored_ = 23;
      public:
        int visible = 24;
    };
}

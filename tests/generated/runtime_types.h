#pragma once
#include <array>
#include <string>
#include <vector>
#include "reflgen/core/hook.h"

namespace runtime_generated
{
    struct [[reflgen::runtime]] payload
    {
        int value = 4;
        std::string label = "runtime";
    };

    struct [[reflgen::runtime]] base
    {
        int inherited = 7;
    };

    // Runtime-only public fields do not require private reflection friendship and
    // never opt this type into authoring serialization.
    class [[reflgen::runtime("runtime.public_surface")]] surface : public base
    {
      public:
        payload item;
        payload& reference = item;
        const payload& readonly_reference = item;
        using payload_reference = payload&;
        using const_payload_reference = const payload&;
        payload_reference alias_reference = item;
        const_payload_reference readonly_alias_reference = item;
        std::vector<payload> values{{}};
        int calls = 0;

        int add(int amount) { ++calls; return item.value + amount; }
        long add(const std::array<long, 2>& amount) const & noexcept
        { return item.value + amount[0] + amount[1]; }
        payload& current() { return item; }
        const payload& current() const { return item; }
        [[reflgen::ignore]] void hidden() { ++calls; }
        [[reflgen::script_internal]] void lifecycle() { ++calls; }
        consteval int compile_time() const { return 9; }
        void unavailable() = delete;

      private:
        int private_value = 12;
        void private_method() { ++private_value; }
    };

    struct [[reflgen::reflect, reflgen::public_methods]] authored
    {
        friend struct reflgen::access;
        int value = 3;
        int read() const { return value; }
        payload inspect() const { return {}; }
      private:
        int saved_private = 5;
    };

    struct [[reflgen::runtime]] imported_base
    {
        int select(int value) const { return value + 1; }
    };

    struct [[reflgen::runtime]] imported_overload : imported_base
    {
        using imported_base::select;
        long select(long value) const { return value + 2; }
    };

    struct [[reflgen::runtime]] complex_returns
    {
        using callback = int(*)(int);
        int values[2] = {11, 12};
        callback callback_for(int) const { return nullptr; }
        callback callback_for(float) const { return nullptr; }
        const int (&array_for(int) const)[2] { return values; }
        const int (&array_for(float) const)[2] { return values; }
    };
}

#pragma once
namespace fsharp_fixture
{
    struct point { int x; int y; };
    using PointLayout [[reflgen::interop("fsharp"), reflgen::value("Point")]] = point;
    struct [[reflgen::interop("fsharp"), reflgen::lifetime("owned")]] owned_counter
    {
        ~owned_counter() noexcept;
        [[reflgen::interop("fsharp")]] int read() const { return 73; }
    };
    enum class mode : unsigned short { first = 2, second = 65530 };
    struct [[reflgen::interop("fsharp"), reflgen::lifetime("borrowed")]] counter
    {
        [[reflgen::interop("fsharp")]] int add(int x, bool enabled) const { return enabled ? x + 1 : x; }
        [[reflgen::interop("fsharp")]] int read_pointer(const void* address) const
        {
            return address ? *static_cast<const int*>(address) : -1;
        }
        [[reflgen::interop("fsharp")]] int read_typed(const int* address) const
        {
            return address ? *address : -2;
        }
        [[reflgen::interop("fsharp")]] int utf8_length(const char* text) const
        {
            if (!text) return -1;
            int n = 0;
            while (text[n]) ++n;
            return n;
        }
        [[reflgen::interop("fsharp")]] int sum_point(const point* value) const
        {
            return value ? value->x + value->y : -1;
        }
        [[reflgen::interop("fsharp")]] point shifted(point value, int amount) const
        {
            return {value.x + amount, value.y - amount};
        }
        [[reflgen::interop("fsharp")]] point failing_point(point value) const;
        [[reflgen::interop("fsharp")]] mode choose(mode value) const { return value; }
        [[reflgen::interop("fsharp"), reflgen::callback("synchronous")]] int apply(int (*callback)(int), int value) const
        {
            return callback(value) + callback(value + 1);
        }
        [[reflgen::interop("fsharp"), reflgen::consume_ownership("0")]] int consume_owned(owned_counter* value, bool should_throw) const;
        [[reflgen::interop("fsharp"), reflgen::consume_ownership("0"), reflgen::return_ownership("owned")]] owned_counter* swap_owned(owned_counter* value) const;
        [[reflgen::interop("fsharp"), reflgen::consume_ownership("0")]] void consume_void(owned_counter* value) const;
        [[reflgen::interop("fsharp"), reflgen::consume_ownership("0")]] point consume_point(owned_counter* value) const;
        [[reflgen::interop("fsharp"), reflgen::return_ownership("owned")]] owned_counter* create_owned() const;
        [[reflgen::interop("fsharp"), reflgen::return_ownership("owned")]] owned_counter* no_owned() const { return nullptr; }
        [[reflgen::interop("fsharp"), reflgen::return_ownership("owned")]] owned_counter* failing_owned() const;
        [[reflgen::interop("fsharp")]] void fail() const;
    };
}

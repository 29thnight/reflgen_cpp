#pragma once
// 선택한 export 만 C ABI 에 들어간다. reflection 여부와 객체의 private 데이터는 별개다.
namespace interop_tests
{
    using count_type = unsigned int;

    class [[reflgen::interop("csharp"), reflgen::lifetime("borrowed")]] counter
    {
      public:
        [[reflgen::interop("csharp")]] int value() const { return value_; }
        [[reflgen::interop("csharp")]] void set_value(int value) { value_ = value; }
        [[reflgen::interop("csharp")]] bool toggle(bool enabled) { return !enabled; }
        [[reflgen::interop("csharp")]] double scale(double) const;
        [[reflgen::interop("csharp")]] constexpr int constant() const { return 7; }
        [[reflgen::interop("csharp")]] count_type unsigned_value() const { return 1; }
        [[reflgen::interop("c")]] int native_only() const { return value_; }

        void reset() { value_ = 0; }

      private:
        int value_ = 0;
    };

    struct [[reflgen::interop("c"), reflgen::lifetime("borrowed")]] native_counter
    {
        [[reflgen::interop("c")]] unsigned int native_value() const { return 1; }
    };

    struct [[reflgen::reflect]] reflection_only
    {
        int internal = 0;
        [[reflgen::reflect]] int reflected_method() const { return internal; }
    };
} // namespace interop_tests

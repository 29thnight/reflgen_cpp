#pragma once
// 표준 라이브러리 없이 파싱할 수 있는 선언 manifest 시험. attribute 타입은 반영 데이터와 별개다.
namespace reflgen
{
    struct access;
} // namespace reflgen

namespace declarations_tests
{
    struct [[reflgen::attribute]] label
    {
        constexpr explicit label(const char*) {}
    };

    struct [[reflgen::attribute]] limits
    {
        constexpr limits(int, int) {}
    };

    struct [[reflgen::attribute]] marker
    {
    };

    using count_type = unsigned int;

    struct [[reflgen::reflect]] base
    {
        int inherited = 0;
    };

    enum class [[reflgen::reflect]] mode : unsigned char
    {
        idle,
        busy = 7,
    };

    class [[reflgen::reflect("tests.declarations"), declarations_tests::label("fixture")]] sample : public base
    {
        friend struct reflgen::access;

      public:
        static constexpr int max_count = 10;

        [[declarations_tests::limits(0, max_count), declarations_tests::marker]] count_type count = 0;
        const sample* observed = nullptr;
        sample* const fixed = nullptr;
        const count_type* aliased = nullptr;
        count_type values[2] = {};
        volatile int revision = 0;
        int lanes[3] = {};
        mode state = mode::idle;
        [[reflgen::ignore]] int ignored = 0;

        [[reflgen::reflect, declarations_tests::label("read")]]
        const sample& read(const sample* input, count_type, sample&& moved) const volatile & noexcept;

        [[reflgen::reflect]] sample&& take() &&;

        [[reflgen::reflect]] int (*callback() volatile)(int);

        [[reflgen::reflect]] bool conditional() noexcept(sizeof("(") > 0);

        int unreflected() const;

      protected:
        int protected_value = 0;

      private:
        int private_value = 0;
    };
} // namespace declarations_tests

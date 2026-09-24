#pragma once
// 코드 생성기 end-to-end 시험용 타입. 손으로 쓴 reflect() 가 하나도 없다 — 서술은 전부
// [[reflgen::…]] attribute 에서 생성된다.
#include "reflgen/reflgen.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace generated_tests
{
    constexpr int max_level = 99;

    // 사용자 정의 attribute — ATTRIBUTE_SCOPES 에 generated_tests 를 넣었으므로 옮겨진다. 이 이름공간에는 데이터
    // 타입도 있으므로 [[reflgen::attribute]] 로 편집기 자동완성 카탈로그에 이것만 내보낸다.
    struct [[reflgen::attribute]] tooltip
    {
        std::string_view text;

        constexpr explicit tooltip(std::string_view value) : text(value) {}
    };

    // 1 << 10 은 값 범위 스캔(기본 [-128, 128]) 밖이다 — 생성된 정확한 표로만 이름이 붙는다.
    enum class [[reflgen::reflect]] element
    {
        fire,
        water,
        earth = 7,
        wind = 1 << 10,
    };

    class [[reflgen::reflect("tests.stats")]] stats
    {
        friend struct reflgen::access;

      public:
        [[reflgen::range(1, max_level)]] int level = 1;
        [[reflgen::display_name("Health"), generated_tests::tooltip("hit points")]] float health = 100.0f;
        [[reflgen::ignore]] int cache = 0;
        [[reflgen::transient]] int frame = 0;

        [[reflgen::reflect]] int level_up(int amount)
        {
            level += amount;
            return level;
        }

        int secret() const { return secret_; }
        void set_secret(int value) { secret_ = value; }

        bool operator==(const stats& other) const
        {
            return level == other.level && health == other.health && secret_ == other.secret_;
        }

      private:
        [[reflgen::serialized_name("secret")]] int secret_ = 7;
    };

    struct [[reflgen::reflect]] entity
    {
        virtual ~entity() = default;
        std::string name = "entity";
    };

    struct [[reflgen::reflect("tests.hero")]] hero : entity
    {
        stats base_stats;
        element affinity = element::fire;
        std::vector<std::string> tags;
        std::map<std::string, int> inventory;
    };

    namespace nested
    {
        struct [[reflgen::reflect]] marker
        {
            int id = 0;
        };
    } // namespace nested

    struct limits
    {
        static constexpr int cap = 64;

        // attribute 인자는 자기 클래스와 감싸는 클래스의 이름을 한정 없이 쓴다 — 생성 코드는 클래스
        // 밖에 있으므로 생성기가 한정해 준다.
        struct [[reflgen::reflect]] slot
        {
            static constexpr int floor = 1;

            [[reflgen::range(floor, cap)]] int count = 1;
            // 이름 뒤 attribute 는 그 declarator 에만 붙는다 — scratch 만 빠지고 weight 는 남는다.
            int scratch [[reflgen::ignore]] = 0, weight = 2;
            [[reflgen::display_name("Tag" // 인자 안의 주석은 생성 코드로 옮겨지지 않는다
                                    )]] std::string tag;

            [[reflgen::reflect]] explicit operator bool() const { return count != 0; }
        };
    };

    // 반영하지 않는 타입 — 생성물에 나타나지 않아야 한다.
    struct plain
    {
        int value = 0;
    };
} // namespace generated_tests

#include "game_types.reflgen.h"

#pragma once
// 코드 생성기 end-to-end 시험용 타입. 손으로 쓴 reflect() 가 하나도 없다 — 서술은 전부
// [[reflgen::…]] attribute 에서 생성된다.
#include "reflgen/reflgen.h"
#include "test_attributes.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

// 이 header 의 타입은 모두 전방 선언할 수 있다 — 생성 header 는 이 파일을 include 하지 않는다(가벼운 주입).
// 원본 header 가 필요한 경우(중첩 타입, 이름공간 상수를 쓰는 인자)는 fallback_types.h 에 있다.
namespace generated_tests
{
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
        static constexpr int max_level = 99; // 인자가 쓰는 자기 멤버 — 생성 코드는 T::max_level 로 찾는다

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

    // 반영하지 않는 중간층(엔진의 CRTP 정체성 베이스 같은 것) — 그 위의 반영된 조상의 필드가 이어져야 한다.
    template<class Self, class Base>
    struct stamped : Base
    {
        int stamp = 0; // 중간층의 필드는 반영되지 않는다
    };

    struct [[reflgen::reflect]] stamped_hero : stamped<stamped_hero, entity>
    {
        int rank = 1;
    };

    // 다른 라이브러리의 static reflect() 를 가진 채 [[reflgen::reflect]] 로 옮겨 가는 타입(CreatorEngine 의 meta 레시피
    // 같은 것) — 그 reflect() 는 reflgen 레시피가 아니므로 생성된 서술이 쓰인다.
    struct [[reflgen::reflect]] migrating
    {
        static consteval int reflect() { return 0; }
        int value = 3;
    };

    // 외래 reflect() 를 가진 중간층 — reflgen 레시피가 아니므로 그 위의 반영된 조상(entity)까지 거슬러 올라가야 한다.
    struct foreign_layer : entity
    {
        static consteval int reflect() { return 0; }
    };

    struct [[reflgen::reflect]] over_foreign_layer : foreign_layer
    {
        int own = 2;
    };

    // 손으로 쓴 reflgen 레시피를 가진 부모 — 반영된 조상이다.
    struct recipe_base
    {
        static consteval auto reflect()
        {
            return reflgen::schema<recipe_base>(reflgen::field<&recipe_base::base_value>);
        }

        int base_value = 5;
    };

    struct [[reflgen::reflect]] over_recipe : recipe_base
    {
        int own = 1;
    };

    namespace nested
    {
        struct [[reflgen::reflect]] marker
        {
            int id = 0;
        };
    } // namespace nested

    // 반영하지 않는 타입 — 생성물에 나타나지 않아야 한다.
    struct plain
    {
        int value = 0;
    };
} // namespace generated_tests

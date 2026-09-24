// reflgen 빠른 시작 — 서술, 속성, 직렬화, 런타임 서술자.
#include "reflgen/binary.h"
#include "reflgen/json.h"
#include "reflgen/reflgen.h"
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace game
{
    // 사용자 정의 속성 — 생성자가 constexpr 인 구조체면 무엇이든 된다.
    struct tooltip
    {
        std::string_view text;

        constexpr explicit tooltip(std::string_view value) : text(value) {}
    };

    enum class rarity
    {
        common,
        rare,
        legendary
    };

    struct item
    {
        std::string name;
        rarity grade = rarity::common;
        int count = 1;

        static consteval auto reflect()
        {
            // clang-format off
            return reflgen::schema<item>(
                reflgen::field<&item::name>,
                reflgen::field<&item::grade>,
                reflgen::field<&item::count>.with(reflgen::range(1, 99), tooltip("stack size")));
            // clang-format on
        }
    };

    struct component
    {
        virtual ~component() = default;
        bool enabled = true;

        static consteval auto reflect()
        {
            return reflgen::schema<component>(reflgen::field<&component::enabled>).named("game.component");
        }
    };

    struct health : component
    {
        float current = 100.0f;

        static consteval auto reflect()
        {
            return reflgen::schema<health>(reflgen::base<component>,
                                           reflgen::field<&health::current>.with(reflgen::range(0.0f, 100.0f)))
                .named("game.health");
        }
    };

    class player
    {
        friend struct reflgen::access;

      public:
        std::string name = "hero";
        std::vector<item> inventory;
        std::map<std::string, int> stats;
        std::optional<std::string> guild;
        std::vector<std::unique_ptr<component>> components;

      private:
        int session_ = 0; // 저장하지 않는다

        static consteval auto reflect()
        {
            // clang-format off
            return reflgen::schema<player>(
                reflgen::field<&player::name>,
                reflgen::field<&player::inventory>,
                reflgen::field<&player::stats>,
                reflgen::field<&player::guild>,
                reflgen::field<&player::components>,
                reflgen::field<&player::session_>.with(reflgen::transient()));
            // clang-format on
        }
    };
} // namespace game

int main()
{
    // 다형 포인터로 읽고 쓸 파생 타입은 등록한다.
    reflgen::register_type<game::health>();

    game::player player;
    player.inventory = {{"potion", game::rarity::common, 3}, {"crown", game::rarity::legendary, 1}};
    player.stats = {{"str", 12}, {"dex", 9}};
    player.components.push_back(std::make_unique<game::health>());

    const std::string text = reflgen::json::to_string(player, 2);
    std::printf("%s\n", text.c_str());

    const auto loaded = reflgen::json::from_string<game::player>(text);
    const auto bytes = reflgen::binary::to_bytes(loaded);
    std::printf("binary size: %zu bytes\n", bytes.size());

    // 런타임 서술자 — 타입을 런타임 값으로만 아는 도구(인스펙터 등)가 쓴다.
    const reflgen::type_descriptor& type = reflgen::type_descriptor_of<game::item>();
    for (const reflgen::field_info& field : type.fields())
    {
        std::printf("%.*s : %.*s", static_cast<int>(field.name().size()), field.name().data(),
                    static_cast<int>(field.type_name().size()), field.type_name().data());
        if (const auto* range = field.attributes().find<reflgen::range<int>>())
        {
            std::printf("  range [%d, %d]", range->min, range->max);
        }
        if (const auto* tip = field.attributes().find<game::tooltip>())
        {
            std::printf("  \"%.*s\"", static_cast<int>(tip->text.size()), tip->text.data());
        }
        std::printf("\n");
    }
    return 0;
}

module;
#include <string>
#include <vector>
export module reflgen.test.types;

struct not_exported
{
    int hidden = 0;
};
export namespace game
{
    enum class element : unsigned
    {
        fire = 1,
        water = 4096
    };
    struct point
    {
        float x = 0;
        float y = 0;
    };
    class player
    {
        int secret_ = 42;

      protected:
        int protected_ = 7;

      public:
        std::string name = "hero";
        int health = 100;
        point location;
        element kind = element::water;
        std::vector<int> inventory{1, 2, 3};
        void damage(int value) { health -= value; }
        int score() const { return health + secret_; }
    };
} // namespace game

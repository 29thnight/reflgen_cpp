export module reflgen.test.crtp_adversary;
export namespace game
{
    struct point { int x = 0; };
    struct replacement { int y = 0; };
    template<class Self, class Base> struct identity : Base {};
    template<class Self> struct identity<Self, point> : replacement {};
    struct guarded_point : identity<guarded_point, point> { int shield = 0; };
}

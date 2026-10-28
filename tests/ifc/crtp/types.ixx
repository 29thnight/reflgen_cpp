export module reflgen.test.crtp;

export namespace game
{
    struct point { int x = 0; };
    template<class Self, class Base> struct identity : Base {};
    struct guarded_point : identity<guarded_point, point> { int shield = 0; };
}

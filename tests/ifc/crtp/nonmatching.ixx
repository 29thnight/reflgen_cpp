export module reflgen.test.crtp_nonmatching;
export namespace game
{
    struct no_base {};
    struct point { int x = 0; };
    template<class Self, class Base> struct identity : Base {};
    template<class Self> struct identity<Self, no_base> {};
    struct guarded_point : identity<guarded_point, point> { int shield = 0; };
}

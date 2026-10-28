export module reflgen.test.imported_base;
export namespace reflgen {
    struct replacement { int value; };
    template<class Derived, class Base> struct reflected_base : Base {};
    template<class Derived> struct reflected_base<Derived, int> : replacement {};
}

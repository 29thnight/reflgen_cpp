export module reflgen.test.imported_base;
export namespace reflgen {
    template<class Derived, class Base> struct reflected_base : Base {};
}

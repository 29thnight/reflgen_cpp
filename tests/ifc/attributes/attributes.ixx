export module reflgen.test.attributes;

export namespace editor
{
    struct [[reflgen::attribute]] tooltip
    {
        int value;
        constexpr explicit tooltip(int number) : value(number) {}
    };
}

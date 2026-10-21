export module mixed.types;
export namespace mixed
{
    struct [[reflgen::ignore]] payload { int value = 29; };
    struct player
    {
        [[reflgen::ignore]] int ignored = 5;
        [[reflgen::serialized_name("score")]] int value = 7;
        payload extra;
    };
}

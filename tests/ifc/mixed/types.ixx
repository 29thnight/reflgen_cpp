export module mixed.types;
export namespace mixed
{
    struct player
    {
        [[reflgen::ignore]] int ignored = 5;
        [[reflgen::serialized_name("score")]] int value = 7;
    };
}

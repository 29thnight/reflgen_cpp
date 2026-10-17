#pragma once
struct module_item
{
    std::string name = "module";
    int value = 7;
    int cache = 9;
    static consteval auto reflect()
    {
        return reflgen::schema<module_item>(
            reflgen::field<&module_item::name>,
            reflgen::field<&module_item::value>.with(reflgen::serialized_name("number"), reflgen::range(0, 10)),
            reflgen::field<&module_item::cache>.with(reflgen::transient{}));
    }
};
struct custom_number
{
    int value = 12;
};
struct external_model
{
    int value = 21;
};
template<>
struct reflgen::reflection<external_model>
{
    static constexpr auto value = reflgen::schema<external_model>(reflgen::field<&external_model::value>);
};
template<>
struct reflgen::serializer<custom_number>
{
    static void write(reflgen::writer& out, const custom_number& value) { reflgen::serialize(out, value.value); }
    static void read(reflgen::reader& in, custom_number& value) { reflgen::deserialize(in, value.value); }
};
const reflgen::type_descriptor* header_descriptor();
reflgen::registry* header_registry();

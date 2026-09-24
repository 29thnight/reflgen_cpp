#include "harness.h"
#include "reflgen/core/name.h"
#include "reflgen/core/type_id.h"
#include <functional>
#include <string>
#include <string_view>

namespace names
{
struct item
{
    int count;
    double weight;
    void use() {}
};

template<class T>
struct box
{
    T value;
};

namespace inner
{
struct deep
{
};
} // namespace inner

struct base
{
    virtual ~base() = default;
    int base_value;
};

struct derived : base
{
    float derived_value;
    int compute(int) const { return 0; }
};
} // namespace names

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::test;

// 추출이 이 툴체인에서 돈다는 것부터 — 아래 모든 테스트의 전제다.
static_assert(reflgen::name_extraction_supported);
static_assert(reflgen::type_name_of<names::item>() == "names::item");
static_assert(reflgen::member_name_of<&names::item::count>() == "count");

std::string normalized(std::string_view raw)
{
    std::string result(reflgen::detail::normalize_type_name(raw, nullptr), '\0');
    reflgen::detail::normalize_type_name(raw, result.data());
    return result;
}

const test type_names("name: qualified type names drop class keywords", [] {
    check_equal(reflgen::type_name_of<names::item>(), std::string_view("names::item"));
    check_equal(reflgen::type_name_of<names::inner::deep>(), std::string_view("names::inner::deep"));
    check_equal(reflgen::type_name_of<names::box<names::item>>(), std::string_view("names::box<names::item>"));
    check_equal(reflgen::type_name_of<int>(), std::string_view("int"));
    check_equal(reflgen::type_name_of<long long>(), std::string_view("long long"));
    check_equal(reflgen::type_name_of<unsigned long long>(), std::string_view("unsigned long long"));
    check_equal(reflgen::type_name_of<const char*>(), std::string_view("const char*"));
});

const test type_name_nul_terminated("name: type names are NUL terminated", [] {
    const std::string_view name = reflgen::type_name_of<names::item>();
    check_equal(name.data()[name.size()], '\0');
});

const test normalization("name: normalization removes compiler-specific spelling", [] {
    check_equal(normalized("class ns::a<struct ns::b,class ns::c>"), std::string("ns::a<ns::b,ns::c>"));
    check_equal(normalized("ns::x<ns::y<int> >"), std::string("ns::x<ns::y<int>>"));
    check_equal(normalized("ns::m<int, float>"), std::string("ns::m<int,float>"));
    check_equal(normalized("unsigned __int64"), std::string("unsigned long long"));
    check_equal(normalized("__int64"), std::string("long long"));
    check_equal(normalized("enum ns::color"), std::string("ns::color"));
    check_equal(normalized("const char *"), std::string("const char*"));
    // 식별자 일부로 들어 있는 키워드는 지우지 않는다.
    check_equal(normalized("ns::subclass"), std::string("ns::subclass"));
    check_equal(normalized("ns::my__int64"), std::string("ns::my__int64"));
});

const test member_names("name: member pointers yield unqualified names", [] {
    check_equal(reflgen::member_name_of<&names::item::count>(), std::string_view("count"));
    check_equal(reflgen::member_name_of<&names::item::weight>(), std::string_view("weight"));
    check_equal(reflgen::member_name_of<&names::item::use>(), std::string_view("use"));
    check_equal(reflgen::member_name_of<&names::derived::derived_value>(), std::string_view("derived_value"));
    check_equal(reflgen::member_name_of<&names::derived::base_value>(), std::string_view("base_value"));
    check_equal(reflgen::member_name_of<&names::derived::compute>(), std::string_view("compute"));
    check_equal(reflgen::member_name_of<&names::box<int>::value>(), std::string_view("value"));
});

const test raw_member_parsing("name: raw member spellings of every compiler parse", [] {
    using reflgen::detail::member_name_from_raw;
    check_equal(member_name_from_raw("&ns::t::hp"), std::string_view("hp"));
    check_equal(member_name_from_raw("void __cdecl ns::t::fire(int)"), std::string_view("fire"));
    check_equal(member_name_from_raw("&ns::box<ns::a::b>::value"), std::string_view("value"));
    check(member_name_from_raw("&ns::t::operator()").empty(), "operators are not identifiers");
    check(member_name_from_raw("").empty());
});

const test raw_enumerator_parsing("name: cast spellings are not enumerators", [] {
    using reflgen::detail::enumerator_name_from_raw;
    check_equal(enumerator_name_from_raw("ns::color::green"), std::string_view("green"));
    check_equal(enumerator_name_from_raw("UC_One"), std::string_view("UC_One"));
    check(enumerator_name_from_raw("(enum ns::color)0x7").empty());
    check(enumerator_name_from_raw("(ns::color)7").empty());
    check(enumerator_name_from_raw("7").empty());
});

const test type_ids("name: type ids are stable hashes of names", [] {
    constexpr reflgen::type_id item_id = reflgen::type_id_of<names::item>();
    static_assert(item_id == reflgen::type_id_of<names::item>());
    static_assert(item_id != reflgen::type_id_of<names::derived>());
    check_equal(item_id.value(), reflgen::detail::fnv1a("names::item"));
    check(reflgen::type_id{} < reflgen::type_id{1});
    check_equal(std::hash<reflgen::type_id>{}(item_id), static_cast<std::size_t>(item_id.value()));
});
} // namespace

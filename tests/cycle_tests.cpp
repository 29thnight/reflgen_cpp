#include "support.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cycles
{
struct node
{
    std::string name;
    std::shared_ptr<node> next;

    static consteval auto reflect()
    {
        return reflgen::schema<node>(reflgen::field<&node::name>, reflgen::field<&node::next>);
    }
};

// 같은 객체를 두 경로에서 가리키는 공유 — 순환이 아니다.
struct pair_of_nodes
{
    std::shared_ptr<node> left;
    std::shared_ptr<node> right;

    static consteval auto reflect()
    {
        return reflgen::schema<pair_of_nodes>(reflgen::field<&pair_of_nodes::left>,
                                              reflgen::field<&pair_of_nodes::right>);
    }
};

struct actor
{
    virtual ~actor() = default;
    std::shared_ptr<actor> partner;

    static consteval auto reflect()
    {
        return reflgen::schema<actor>(reflgen::field<&actor::partner>).named("cycles.actor");
    }
};

struct dancer : actor
{
    int steps = 3;

    static consteval auto reflect()
    {
        return reflgen::schema<dancer>(reflgen::base<actor>, reflgen::field<&dancer::steps>).named("cycles.dancer");
    }
};

struct holder
{
    int value = 1;
    std::vector<std::reference_wrapper<holder>> links;

    static consteval auto reflect()
    {
        return reflgen::schema<holder>(reflgen::field<&holder::value>, reflgen::field<&holder::links>);
    }
};

// 순환 없이 깊기만 한 구조.
struct link
{
    int value = 0;
    std::unique_ptr<link> next;

    static consteval auto reflect()
    {
        return reflgen::schema<link>(reflgen::field<&link::value>, reflgen::field<&link::next>);
    }
};

std::unique_ptr<link> make_chain(std::size_t length)
{
    std::unique_ptr<link> head;
    for (std::size_t i = 0; i < length; ++i)
    {
        auto next = std::make_unique<link>();
        next->value = static_cast<int>(i);
        next->next = std::move(head);
        head = std::move(next);
    }
    return head;
}

// 긴 체인을 재귀 소멸자 없이 푼다 — 소멸이 먼저 stack 을 넘치면 시험이 무의미해진다.
void release_chain(std::unique_ptr<link> head)
{
    while (head)
    {
        head = std::move(head->next);
    }
}

std::size_t chain_length(const link* head)
{
    std::size_t length = 0;
    for (; head != nullptr; head = head->next.get())
    {
        ++length;
    }
    return length;
}
} // namespace cycles

namespace
{
using reflgen_test::check;
using reflgen_test::check_equal;
using reflgen_test::check_throws;
using reflgen_test::require;
using reflgen_test::test;

template<class F>
std::string failure_path(F&& action)
{
    try
    {
        action();
    }
    catch (const reflgen::serialization_error& error)
    {
        return error.path();
    }
    return "<no error>";
}

const test shared_cycle("cycle: a shared_ptr cycle fails instead of recursing forever", [] {
    auto a = std::make_shared<cycles::node>();
    auto b = std::make_shared<cycles::node>();
    a->name = "a";
    b->name = "b";
    a->next = b;
    b->next = a;

    check_throws([&] { reflgen::json::to_string(*a); }, "cyclic reference");
    check_throws([&] { reflgen::binary::to_bytes(*a); }, "cyclic reference");
    // 루트 a → b → a 까지는 적히고, 두 번째 b 에서 멈춘다.
    check_equal(failure_path([&] { reflgen::json::to_string(*a); }), std::string("/next/next/next"));
    check_equal(failure_path([&] { reflgen::json::to_string(a); }), std::string("/next/next"));
    b->next.reset();
});

const test self_cycle("cycle: an object pointing to itself", [] {
    auto self = std::make_shared<cycles::node>();
    self->next = self;
    check_equal(failure_path([&] { reflgen::json::to_string(self); }), std::string("/next"));
    self->next.reset();
});

const test polymorphic_cycle("cycle: polymorphic pointers are tracked by their most derived address", [] {
    reflgen::register_type<cycles::dancer>();
    auto first = std::make_shared<cycles::dancer>();
    auto second = std::make_shared<cycles::dancer>();
    first->partner = second;
    second->partner = first;
    const std::shared_ptr<cycles::actor> root = first;
    check_throws([&] { reflgen::json::to_string(root); }, "cyclic reference");
    second->partner.reset();
    check_equal(reflgen::json::to_string(root),
                std::string(R"({"type":"cycles.dancer","value":{"partner":{"type":"cycles.dancer",)"
                            R"("value":{"partner":null,"steps":3}},"steps":3}})"));
});

const test reference_cycle("cycle: reference_wrapper back to its owner", [] {
    cycles::holder holder;
    holder.links.push_back(std::ref(holder));
    check_throws([&] { reflgen::json::to_string(holder); }, "cyclic reference");

    cycles::holder other;
    holder.links = {std::ref(other), std::ref(other)};
    check_equal(reflgen::json::to_string(holder), std::string(R"({"value":1,"links":[{"value":1,"links":[]},)"
                                                              R"({"value":1,"links":[]}]})"));
});

const test shared_is_not_a_cycle("cycle: the same object on two paths is written twice", [] {
    cycles::pair_of_nodes pair;
    pair.left = std::make_shared<cycles::node>();
    pair.left->name = "shared";
    pair.right = pair.left;
    const auto loaded = reflgen_test::json_round_trip(pair);
    require(loaded.left && loaded.right);
    check_equal(loaded.left->name, std::string("shared"));
    check_equal(loaded.right->name, std::string("shared"));
    // 공유 관계는 보존하지 않는다 — 읽으면 사본 둘이다.
    check(loaded.left != loaded.right);
});

const test path_is_released("cycle: the tracking state is clean after a failure", [] {
    auto self = std::make_shared<cycles::node>();
    self->next = self;
    check_throws([&] { reflgen::json::to_string(self); }, "cyclic reference");
    self->next.reset();
    check(reflgen::detail::active_indirections.empty(), "a failed write left entries on the active path");
    check_equal(reflgen::json::to_string(self), std::string(R"({"name":"","next":null})"));
});

// 상한 바로 아래 깊이를 실제로 쓰고 읽는다 — 상한이 stack 한계보다 작다는 증명이다.
// 상한이 너무 크면 이 시험이 Debug 빌드에서 stack overflow 로 죽는다.
const test deepest_allowed_chain("cycle: the deepest chain the default limits allow round-trips", [] {
    constexpr std::size_t length = reflgen::json::writer::default_max_depth - 1;
    auto chain = cycles::make_chain(length);
    const std::string text = reflgen::json::to_string(chain);
    auto from_json = reflgen::json::from_string<std::unique_ptr<cycles::link>>(text);
    check_equal(cycles::chain_length(from_json.get()), length);
    const auto bytes = reflgen::binary::to_bytes(chain);
    auto from_binary = reflgen::binary::from_bytes<std::unique_ptr<cycles::link>>(bytes);
    check_equal(cycles::chain_length(from_binary.get()), length);
    cycles::release_chain(std::move(chain));
    cycles::release_chain(std::move(from_json));
    cycles::release_chain(std::move(from_binary));
});

const test too_deep_chain("cycle: chains deeper than the limit fail cleanly", [] {
    auto chain = cycles::make_chain(reflgen::json::writer::default_max_depth * 4);
    check_throws([&] { reflgen::json::to_string(chain); }, "nesting is deeper than");
    check_throws([&] { reflgen::binary::to_bytes(chain); }, "nesting is deeper than");
    cycles::release_chain(std::move(chain));
});

const test custom_writer_depth("cycle: writer depth limits are configurable", [] {
    const std::vector<std::vector<std::vector<int>>> nested{{{1}}};
    check_equal(reflgen::json::to_string(nested, 0, 3), std::string("[[[1]]]"));
    check_throws([&] { reflgen::json::to_string(nested, 0, 2); }, "nesting is deeper than 2");
    check_throws([&] { reflgen::binary::to_bytes(nested, 2); }, "nesting is deeper than 2");
});
} // namespace

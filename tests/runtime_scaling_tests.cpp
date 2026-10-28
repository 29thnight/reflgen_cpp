#include "harness.h"
#include "reflgen/runtime/type_descriptor.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>

namespace runtime_scaling
{
    struct source_slot { std::size_t index; };
    struct storage { int seed = 7; };

    template<std::size_t Start, std::size_t Count, class Base = storage>
    struct method_surface : Base
    {
        template<std::size_t I>
        int sample(int amount) const noexcept
        { return this->seed + static_cast<int>(Start + I) + amount; }

        static consteval auto reflect()
        {
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return reflgen::schema<method_surface>(reflgen::base<Base>,
                    reflgen::method<&method_surface::template sample<I>>.named("sample")
                        .parameters("amount").with(source_slot{Start + I})...);
            }(std::make_index_sequence<Count>{});
        }
    };

    // CreatorEngine Scene has 142 direct methods. The old tuple_cat copy hit
    // MSVC C1054 even with no reflected bases; keep this case above that size.
    using direct = method_surface<0, 160>;
    using layer1 = method_surface<0, 40>;
    using layer2 = method_surface<40, 40, layer1>;
    using layer3 = method_surface<80, 40, layer2>;
    using layer4 = method_surface<120, 40, layer3>;
    struct padding { std::uint64_t prefix = 0; };
    struct offset_leaf : padding, layer4
    {
        static consteval auto reflect()
        { return reflgen::schema<offset_leaf>(reflgen::base<layer4>); }
    };
}

namespace
{
    using namespace reflgen_test;

    template<class T>
    void verify_surface(T& object)
    {
        const auto& descriptor = reflgen::type_descriptor_of<T>();
        const auto methods = descriptor.methods();
        require(methods.size() == 160);
        int amount = 5;
        void* arguments[] = {&amount};
        for (std::size_t index = 0; index < methods.size(); ++index)
        {
            const auto& method = methods[index];
            const auto* source = method.attributes().template find<runtime_scaling::source_slot>();
            require(source != nullptr);
            check_equal(source->index, index);
            require(method.parameters().size() == 1);
            check_equal(method.parameters()[0].name(), std::string_view("amount"));
            check(method.is_serialized_invocable());
            int result = 0;
            method.invoke(&object, arguments, &result);
            check_equal(result, object.seed + static_cast<int>(index) + amount);
        }
        check(descriptor.find_method("sample") == &methods.back());
    }

    const test direct_method_scaling("runtime scaling: 160 direct methods reference their canonical schemas without tuple copies", [] {
        runtime_scaling::direct object;
        verify_surface(object);
        const auto& first = reflgen::type_descriptor_of<runtime_scaling::direct>().methods().front();
        const auto& source = std::get<0>(std::get<0>(reflgen::runtime_schema_of<runtime_scaling::direct>.methods).attributes);
        check(first.attributes().find<runtime_scaling::source_slot>() == std::addressof(source));
    });

    const test inherited_method_scaling("runtime scaling: large inherited surfaces preserve source order, owners and base pointer adjustment", [] {
        runtime_scaling::offset_leaf object;
        check(static_cast<void*>(&object) != static_cast<void*>(static_cast<runtime_scaling::layer4*>(&object)));
        verify_surface(object);
        const auto methods = reflgen::type_descriptor_of<runtime_scaling::offset_leaf>().methods();
        check(methods[0].declaring_type() == reflgen::type_id_of<runtime_scaling::layer1>());
        check(methods[40].declaring_type() == reflgen::type_id_of<runtime_scaling::layer2>());
        check(methods[80].declaring_type() == reflgen::type_id_of<runtime_scaling::layer3>());
        check(methods[120].declaring_type() == reflgen::type_id_of<runtime_scaling::layer4>());
    });
}

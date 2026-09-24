#pragma once
// 런타임 서술자 — 컴파일타임 스키마를 타입을 지운 표로 물질화한 것.
//
// 편집기(인스펙터), 콘솔 명령, 다형 직렬화처럼 타입을 런타임 값으로만 아는 소비자가
// 쓴다. 표는 전부 constexpr 이다 — 정적 초기화 순서 문제가 없고, 서로를 참조하는
// 타입(A 가 B* 를, B 가 A* 를 든다)도 함수 포인터로 느슨하게 이어져 순환이 없다.
//
// fields() 는 **상속 필드를 포함한다**(부모 우선). 필드의 address() 는 언제나 이
// 서술자의 타입(T*) 기준이라 부모 필드도 캐스트 없이 같은 객체 포인터로 닿는다.
#include "reflgen/core/attributes.h"
#include "reflgen/core/schema.h"
#include "reflgen/serial/detail/object.h"
#include "reflgen/serial/detail/traits.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/fwd.h"
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace reflgen
{
    class type_descriptor;

    namespace detail
    {
        struct runtime_factory;
    }

    class field_info
    {
      public:
        std::string_view name() const noexcept { return name_; }
        // 직렬화 키 — serialized_name 속성이 있으면 그것, 없으면 name().
        std::string_view key() const noexcept { return key_; }
        type_id type() const noexcept { return type_; }
        std::string_view type_name() const noexcept { return type_name_; }
        // 이 필드를 선언한 클래스(상속 필드면 부모).
        std::string_view declaring_type_name() const noexcept { return declaring_type_name_; }
        // 값 타입에 서술이 있으면 그 서술자.
        const type_descriptor* reflected_type() const noexcept
        {
            return reflected_type_ != nullptr ? &reflected_type_() : nullptr;
        }
        attribute_list attributes() const noexcept { return attributes_; }

        void* address(void* object) const noexcept { return address_(object); }
        const void* address(const void* object) const noexcept { return address_(const_cast<void*>(object)); }

        bool is_serializable() const noexcept { return write_ != nullptr; }
        bool is_deserializable() const noexcept { return read_ != nullptr; }

        void serialize(writer& out, const void* object) const
        {
            if (write_ == nullptr)
            {
                throw serialization_error("field '" + std::string(name_) + "' is not serializable");
            }
            write_(out, object);
        }

        void deserialize(reader& in, void* object) const
        {
            if (read_ == nullptr)
            {
                throw serialization_error("field '" + std::string(name_) + "' is not deserializable");
            }
            read_(in, object);
        }

      private:
        friend struct detail::runtime_factory;

        using descriptor_function = const type_descriptor& (*)() noexcept;
        using address_function = void* (*)(void*) noexcept;
        using write_function = void (*)(writer&, const void*);
        using read_function = void (*)(reader&, void*);

        constexpr field_info(std::string_view name, std::string_view key, type_id type, std::string_view type_name,
                             std::string_view declaring_type_name, descriptor_function reflected_type,
                             attribute_list attributes, address_function address, write_function write,
                             read_function read) noexcept
            : name_(name), key_(key), type_(type), type_name_(type_name), declaring_type_name_(declaring_type_name),
              reflected_type_(reflected_type), attributes_(attributes), address_(address), write_(write), read_(read)
        {
        }

        std::string_view name_;
        std::string_view key_;
        type_id type_;
        std::string_view type_name_;
        std::string_view declaring_type_name_;
        descriptor_function reflected_type_;
        attribute_list attributes_;
        address_function address_;
        write_function write_;
        read_function read_;
    };

    // base<B> 로 선언한 직계 부모 하나.
    class base_info
    {
      public:
        type_id type() const noexcept { return type_; }
        // 부모에 서술이 없으면(base<B> 만 적고 B 는 서술하지 않은 경우) nullptr.
        const type_descriptor* descriptor() const noexcept { return descriptor_ != nullptr ? &descriptor_() : nullptr; }
        // 파생 객체 포인터를 이 부모의 포인터로 바꾼다(다중 상속의 오프셋 보정 포함).
        void* cast(void* derived) const noexcept { return cast_(derived); }

      private:
        friend struct detail::runtime_factory;

        using descriptor_function = const type_descriptor& (*)() noexcept;
        using cast_function = void* (*)(void*) noexcept;

        constexpr base_info(type_id type, descriptor_function descriptor, cast_function cast) noexcept
            : type_(type), descriptor_(descriptor), cast_(cast)
        {
        }

        type_id type_;
        descriptor_function descriptor_;
        cast_function cast_;
    };

    class type_descriptor
    {
      public:
        using instance = std::unique_ptr<void, void (*)(void*)>;

        // 등록 키이자 다형 태그. schema(...).named(...) 로 정한 이름이다.
        std::string_view name() const noexcept { return name_; }
        type_id id() const noexcept { return id_; }
        std::size_t size() const noexcept { return size_; }
        std::size_t alignment() const noexcept { return alignment_; }

        // 상속 필드 포함, 부모 우선.
        std::span<const field_info> fields() const noexcept { return fields_; }
        std::span<const base_info> bases() const noexcept { return bases_; }
        attribute_list attributes() const noexcept { return attributes_; }

        // 다형 타입만 가진다(RTTI 를 쓰는 곳을 다형 타입으로 한정해 -fno-rtti 빌드를 살린다).
        const std::type_info* rtti() const noexcept { return rtti_ != nullptr ? rtti_() : nullptr; }

        const field_info* find_field(std::string_view field_name) const noexcept
        {
            // 같은 이름이 부모와 자식에 다 있으면 자식(뒤쪽)이 가린다 — C++ 이름 탐색과 같다.
            for (auto it = fields_.rbegin(); it != fields_.rend(); ++it)
            {
                if (it->name() == field_name)
                {
                    return &*it;
                }
            }
            return nullptr;
        }

        bool is_constructible() const noexcept { return create_ != nullptr; }

        instance create() const
        {
            if (create_ == nullptr)
            {
                throw serialization_error("type '" + std::string(name_) + "' is not default constructible");
            }
            return instance(create_(), destroy_);
        }

        // object(이 타입의 객체)를 target 타입의 포인터로 바꾼다. target 이 선언된 조상이
        // 아니면 nullptr.
        void* upcast(void* object, type_id target) const noexcept
        {
            if (id_ == target)
            {
                return object;
            }
            for (const base_info& base : bases_)
            {
                void* base_object = base.cast(object);
                if (base.type() == target)
                {
                    return base_object;
                }
                if (const type_descriptor* descriptor = base.descriptor())
                {
                    if (void* found = descriptor->upcast(base_object, target))
                    {
                        return found;
                    }
                }
            }
            return nullptr;
        }

        bool is_serializable() const noexcept { return write_ != nullptr; }
        bool is_deserializable() const noexcept { return read_ != nullptr; }

        void serialize(writer& out, const void* object) const
        {
            if (write_ == nullptr)
            {
                throw serialization_error("type '" + std::string(name_) + "' is not serializable");
            }
            write_(out, object);
        }

        void deserialize(reader& in, void* object) const
        {
            if (read_ == nullptr)
            {
                throw serialization_error("type '" + std::string(name_) + "' is not deserializable");
            }
            read_(in, object);
        }

      private:
        friend struct detail::runtime_factory;

        using create_function = void* (*)();
        using destroy_function = void (*)(void*);
        using rtti_function = const std::type_info* (*)() noexcept;
        using write_function = void (*)(writer&, const void*);
        using read_function = void (*)(reader&, void*);

        constexpr type_descriptor(std::string_view name, type_id id, std::size_t size, std::size_t alignment,
                                  std::span<const field_info> fields, std::span<const base_info> bases,
                                  attribute_list attributes, rtti_function rtti, create_function create,
                                  destroy_function destroy, write_function write, read_function read) noexcept
            : name_(name), id_(id), size_(size), alignment_(alignment), fields_(fields), bases_(bases),
              attributes_(attributes), rtti_(rtti), create_(create), destroy_(destroy), write_(write), read_(read)
        {
        }

        std::string_view name_;
        type_id id_;
        std::size_t size_;
        std::size_t alignment_;
        std::span<const field_info> fields_;
        std::span<const base_info> bases_;
        attribute_list attributes_;
        rtti_function rtti_;
        create_function create_;
        destroy_function destroy_;
        write_function write_;
        read_function read_;
    };

    template<reflectable T>
    constexpr const type_descriptor& type_descriptor_of() noexcept;

    namespace detail
    {
        struct runtime_factory
        {
            template<class... Args>
            static constexpr field_info field(Args... args) noexcept
            {
                return field_info(args...);
            }

            template<class... Args>
            static constexpr base_info base(Args... args) noexcept
            {
                return base_info(args...);
            }

            template<class... Args>
            static constexpr type_descriptor type(Args... args) noexcept
            {
                return type_descriptor(args...);
            }
        };

        // 부모 우선으로 모든 필드 서술자를 한 튜플에 모은다(사본이지만 정적 저장소다).
        template<class T>
        consteval auto all_fields_of()
        {
            if constexpr (reflectable<T>)
            {
                return []<class... Bases>(type_list<Bases...>) {
                    return std::tuple_cat(all_fields_of<Bases>()..., schema_of<T>.fields);
                }(direct_bases_t<T>{});
            }
            else
            {
                return std::tuple<>{};
            }
        }

        template<class... Attrs>
        consteval std::array<attribute_ref, sizeof...(Attrs)> make_attribute_refs(
            const std::tuple<Attrs...>& attributes)
        {
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::array<attribute_ref, sizeof...(Attrs)>{
                    attribute_ref{type_id_of<Attrs>(), &std::get<Is>(attributes)}...};
            }(std::index_sequence_for<Attrs...>{});
        }

        template<class V>
        constexpr auto reflected_type_getter() noexcept
        {
            using getter = const type_descriptor& (*)() noexcept;
            if constexpr (reflectable<V>)
            {
                return static_cast<getter>(&type_descriptor_of<V>);
            }
            else
            {
                return static_cast<getter>(nullptr);
            }
        }

        template<class T, auto Member>
        void* field_address(void* object) noexcept
        {
            return std::addressof(static_cast<T*>(object)->*Member);
        }

        template<class T, auto Member>
        void write_field(writer& out, const void* object)
        {
            serialize(out, static_cast<const T*>(object)->*Member);
        }

        template<class T, auto Member>
        void read_field(reader& in, void* object)
        {
            deserialize(in, static_cast<T*>(object)->*Member);
        }

        template<class T>
        void write_instance(writer& out, const void* object)
        {
            serialize(out, *static_cast<const T*>(object));
        }

        template<class T>
        void read_instance(reader& in, void* object)
        {
            deserialize(in, *static_cast<T*>(object));
        }

        template<class T>
        void* create_instance()
        {
            return new T();
        }

        template<class T>
        void destroy_instance(void* object)
        {
            delete static_cast<T*>(object);
        }

        template<class T>
        const std::type_info* rtti_of() noexcept
        {
            return &typeid(T);
        }

        template<class T, class Base>
        void* cast_to_base(void* object) noexcept
        {
            return static_cast<Base*>(static_cast<T*>(object));
        }

        template<class T>
        inline constexpr auto runtime_field_descriptors = all_fields_of<T>();

        template<class T, std::size_t I>
        inline constexpr auto runtime_field_attributes =
            make_attribute_refs(std::get<I>(runtime_field_descriptors<T>).attributes);

        template<class T, std::size_t I>
        constexpr field_info make_field_info() noexcept
        {
            constexpr const auto& descriptor = std::get<I>(runtime_field_descriptors<T>);
            using descriptor_type = std::remove_cvref_t<decltype(descriptor)>;
            using value_type = typename descriptor_type::value_type;
            constexpr auto member = descriptor_type::pointer;

            using write_function = void (*)(writer&, const void*);
            using read_function = void (*)(reader&, void*);
            write_function write = nullptr;
            read_function read = nullptr;
            if constexpr (is_serializable<value_type>())
            {
                write = &write_field<T, member>;
            }
            if constexpr (is_deserializable<value_type>())
            {
                read = &read_field<T, member>;
            }

            return runtime_factory::field(
                descriptor.name, serialized_key(descriptor), type_id_of<value_type>(), type_name_of<value_type>(),
                type_name_of<typename descriptor_type::owner_type>(), reflected_type_getter<value_type>(),
                attribute_list(runtime_field_attributes<T, I>), &field_address<T, member>, write, read);
        }

        template<class T>
        inline constexpr auto runtime_fields = []<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::array<field_info, sizeof...(Is)>{make_field_info<T, Is>()...};
        }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(runtime_field_descriptors<T>)>>>{});

        template<class T, class Base>
        constexpr base_info make_base_info() noexcept
        {
            return runtime_factory::base(type_id_of<Base>(), reflected_type_getter<Base>(), &cast_to_base<T, Base>);
        }

        template<class T>
        inline constexpr auto runtime_bases = []<class... Bases>(type_list<Bases...>) {
            return std::array<base_info, sizeof...(Bases)>{make_base_info<T, Bases>()...};
        }(direct_bases_t<T>{});

        template<class T>
        inline constexpr auto runtime_type_attributes = make_attribute_refs(schema_of<T>.attributes);

        // 사용자 serializer<T> 특수화가 있으면 그것이 곧 쓰기 방법이다. 없으면 필드 전부가
        // 쓰일 수 있어야 객체 전체를 쓸 수 있다.
        template<class T>
        consteval bool instance_serializable() noexcept
        {
            if constexpr (has_custom_serializer<T>)
            {
                return is_serializable<T>();
            }
            else
            {
                return object_fields_serializable<T>();
            }
        }

        template<class T>
        consteval bool instance_deserializable() noexcept
        {
            if constexpr (has_custom_serializer<T>)
            {
                return is_deserializable<T>();
            }
            else
            {
                return object_fields_deserializable<T>();
            }
        }

        template<class T>
        constexpr type_descriptor make_type_descriptor() noexcept
        {
            using rtti_function = const std::type_info* (*)() noexcept;
            using create_function = void* (*)();
            using destroy_function = void (*)(void*);
            using write_function = void (*)(writer&, const void*);
            using read_function = void (*)(reader&, void*);

            rtti_function rtti = nullptr;
            create_function create = nullptr;
            destroy_function destroy = nullptr;
            write_function write = nullptr;
            read_function read = nullptr;
            if constexpr (std::is_polymorphic_v<T>)
            {
                rtti = &rtti_of<T>;
            }
            if constexpr (std::is_default_constructible_v<T> && !std::is_abstract_v<T>)
            {
                create = &create_instance<T>;
                destroy = &destroy_instance<T>;
            }
            if constexpr (instance_serializable<T>())
            {
                write = &write_instance<T>;
            }
            if constexpr (instance_deserializable<T>())
            {
                read = &read_instance<T>;
            }

            return runtime_factory::type(
                schema_of<T>.name, type_id_of<T>(), sizeof(T), alignof(T),
                std::span<const field_info>(runtime_fields<T>), std::span<const base_info>(runtime_bases<T>),
                attribute_list(runtime_type_attributes<T>), rtti, create, destroy, write, read);
        }

        template<class T>
        inline constexpr type_descriptor runtime_type = make_type_descriptor<T>();
    } // namespace detail

    template<reflectable T>
    constexpr const type_descriptor& type_descriptor_of() noexcept
    {
        return detail::runtime_type<T>;
    }
} // namespace reflgen

// 꼬리 include — 런타임 서술자·등록소·직렬화는 서로의 정의를 필요로 하는 한 덩어리다
// (필드 썽크가 serialize 를, 다형 포인터 직렬화가 등록소를 부른다). 어느 헤더로
// 들어와도 덩어리 전체가 올바른 순서로 정의되도록, 각 헤더는 자기 정의를 마친 뒤에
// 다음 고리를 부른다: type_descriptor → registry → serializer → pointer.
#include "reflgen/runtime/registry.h"

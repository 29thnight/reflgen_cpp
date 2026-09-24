#pragma once
// 스마트 포인터 — unique_ptr · shared_ptr.
//
// 비다형 원소는 null 또는 값 그대로다. 다형 원소(가상 함수가 있는 타입)는 동적
// 타입을 함께 적는다:
//
//   {"type": "game.fireball", "value": { … }}
//
// 동적 타입의 서술자는 등록소(default_registry)에서 찾는다. 읽을 때 "type" 이
// "value" 보다 먼저 와야 한다 — 당겨 읽기라 무엇을 만들지 먼저 알아야 한다. 이
// 라이브러리의 writer 는 언제나 그 순서로 쓴다.
//
// shared_ptr 의 공유 관계(같은 객체를 두 포인터가 가리킴)는 보존하지 않는다 —
// 읽으면 사본 둘이 된다.
#include "reflgen/core/schema.h"
#include "reflgen/core/type_id.h"
#include "reflgen/runtime/registry.h"
#include "reflgen/runtime/type_descriptor.h"
#include "reflgen/serial/detail/path.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/serializer.h"
#include "reflgen/serial/writer.h"
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>

namespace reflgen::detail
{
inline constexpr std::string_view polymorphic_type_key = "type";
inline constexpr std::string_view polymorphic_value_key = "value";

template<class Base>
void write_polymorphic(writer& out, const Base& object)
{
    const type_descriptor* type = default_registry().find(typeid(object));
    const void* most_derived = dynamic_cast<const void*>(&object);

    out.begin_object(2);
    out.write_key(polymorphic_type_key);
    if (type != nullptr)
    {
        out.write_string(type->name());
        out.write_key(polymorphic_value_key);
        with_path(polymorphic_value_key, [&] { type->serialize(out, most_derived); });
    }
    else if constexpr (reflectable<Base> && serializable<Base>)
    {
        // 등록 없이도 정적 타입 그 자체인 객체는 쓸 수 있게 한다 — 파생이 없는 흔한 경우.
        if (typeid(object) != typeid(Base))
        {
            throw serialization_error("dynamic type '" + std::string(typeid(object).name()) +
                                      "' of a polymorphic pointer is not registered; call reflgen::register_type<T>()");
        }
        out.write_string(schema_of<Base>.name);
        out.write_key(polymorphic_value_key);
        with_path(polymorphic_value_key, [&] { serialize(out, object); });
    }
    else
    {
        throw serialization_error("dynamic type '" + std::string(typeid(object).name()) +
                                  "' of a polymorphic pointer is not registered; call reflgen::register_type<T>()");
    }
    out.end_object();
}

// 소유권은 호출자에게 넘어간다(Base 의 가상 소멸자로 지운다).
template<class Base>
Base* read_polymorphic(reader& in)
{
    in.begin_object();
    std::string key;
    if (!in.next_key(key) || key != polymorphic_type_key)
    {
        throw serialization_error("a polymorphic object must start with a \"type\" key");
    }
    const std::string name = in.read_string();
    if (!in.next_key(key) || key != polymorphic_value_key)
    {
        throw serialization_error("a polymorphic object must have a \"value\" key after \"type\"");
    }

    Base* result = nullptr;
    if (const type_descriptor* type = default_registry().find(name))
    {
        type_descriptor::instance created = type->create();
        void* base_object = type->upcast(created.get(), type_id_of<Base>());
        if (base_object == nullptr)
        {
            throw serialization_error("type '" + name + "' is not declared as derived from " +
                                      std::string(type_name_of<Base>()) + " (reflgen::base<>)");
        }
        with_path(polymorphic_value_key, [&] { type->deserialize(in, created.get()); });
        created.release();
        result = static_cast<Base*>(base_object);
    }
    else if constexpr (reflectable<Base> && std::is_default_constructible_v<Base> && !std::is_abstract_v<Base>)
    {
        if (name != schema_of<Base>.name)
        {
            throw serialization_error("unknown polymorphic type '" + name +
                                      "'; register it with reflgen::register_type<T>()");
        }
        auto created = std::make_unique<Base>();
        with_path(polymorphic_value_key, [&] { deserialize(in, *created); });
        result = created.release();
    }
    else
    {
        throw serialization_error("unknown polymorphic type '" + name +
                                  "'; register it with reflgen::register_type<T>()");
    }

    // 여기서 실패하면 result 가 새지 않도록 잠시 소유한다.
    std::unique_ptr<Base> guard(result);
    if (in.next_key(key))
    {
        throw serialization_error("unexpected key '" + key + "' in a polymorphic object");
    }
    in.end_object();
    return guard.release();
}

template<class Pointer>
void write_pointer(writer& out, const Pointer& value)
{
    using element = typename Pointer::element_type;
    static_assert(!std::is_array_v<element>, "reflgen: smart pointers to arrays are not supported; use a container");

    if (!value)
    {
        out.write_null();
    }
    else if constexpr (std::is_polymorphic_v<element>)
    {
        write_polymorphic(out, *value);
    }
    else
    {
        serialize(out, *value);
    }
}

template<class Pointer>
void read_pointer(reader& in, Pointer& value)
{
    using element = typename Pointer::element_type;
    static_assert(!std::is_array_v<element>, "reflgen: smart pointers to arrays are not supported; use a container");
    static_assert(std::is_same_v<Pointer, std::unique_ptr<element>> ||
                      std::is_same_v<Pointer, std::shared_ptr<element>>,
                  "reflgen: only unique_ptr with the default deleter and shared_ptr can be deserialized");

    if (in.peek() == value_kind::null)
    {
        in.read_null();
        value.reset();
        return;
    }

    if constexpr (std::is_polymorphic_v<element>)
    {
        static_assert(std::has_virtual_destructor_v<element>,
                      "reflgen: a polymorphic pointee needs a virtual destructor to be deserialized");
        value.reset(read_polymorphic<element>(in));
    }
    else
    {
        // 새 객체에 읽는다 — shared_ptr 이 가리키던 객체는 다른 소유자와 공유될 수
        // 있어서 제자리에서 고치면 안 된다. unique_ptr 도 같은 규칙으로 맞췄다.
        auto created = std::make_unique<element>();
        deserialize(in, *created);
        value = std::move(created);
    }
}
} // namespace reflgen::detail

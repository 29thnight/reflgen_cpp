#pragma once
// Type-erased values used by runtime bindings. Views borrow storage for one native operation;
// consumers must re-resolve owner/member paths instead of retaining addresses across mutations.
#include "reflgen/core/schema.h"
#include "reflgen/runtime/schema.h"
#include "reflgen/core/type_id.h"
#include "reflgen/runtime/enum_descriptor.h"
#include "reflgen/serial/fwd.h"
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace reflgen
{
    class type_descriptor;
    class value_descriptor;
    class value_view;

    enum class value_shape : unsigned char
    {
        unsupported, void_value, boolean, signed_integer, unsigned_integer, floating,
        enumeration, string, record, sequence, map, optional, pointer, variant, tuple, custom, expected
    };

    enum class parameter_passing : unsigned char
    {
        value, const_reference, mutable_reference, rvalue_reference
    };

    // Optional engine extension. Supported members:
    //   static constexpr value_shape shape;
    //   using children = type_list<...>;
    //   static constexpr array<string_view, N> child_names; // optional
    //   static const T* get(const Wrapper&);                // explicit borrowed target
    //   static T* get(Wrapper&);                            // optional mutable target
    //   static size_t size(const Container&);               // custom indexed navigation
    //   static const Element& at(const Container&, size_t);  // stable native element
    //   static Element& at(Container&, size_t);              // optional mutable element
    //   static const Key& key(const Container&, size_t);     // map key, always readonly
    //   static constexpr bool mutable_borrow = false;       // required to use mutable get
    //   static constexpr bool retained_borrow = false;      // value-returned wrapper may expose its target
    //   static void write(writer&, const T&);                // runtime-only serializer override
    //   static void read(reader&, T&);                       // runtime-only deserializer override
    //   static constexpr bool prefer_record_fields = false; // bypass an authoring serializer for records
    //   static constexpr bool serializable/deserializable; // may restrict serializer capabilities
    //   static constexpr bool argument_safe = false;        // pointer-like method input consent
    //   static constexpr bool assignment_safe = false;      // custom value can be staged independently
    //   static constexpr string_view unsupported_reason;   // optional policy explanation
    // Specializations must be visible in the canonical descriptor registration translation unit.
    template<class T>
    struct runtime_value_traits {};

    template<class T>
    constexpr const value_descriptor& value_descriptor_of() noexcept;

    namespace detail
    {
        template<class T, class Visiting = type_list<>> consteval bool runtime_value_serializable();
        template<class T, class Visiting = type_list<>> consteval bool runtime_value_deserializable();
        template<class T> void runtime_serialize(writer&, const T&);
        template<class T> void runtime_deserialize(reader&, T&);
    }

    class value_child_info
    {
      public:
        using getter = const value_descriptor& (*)() noexcept;
        constexpr value_child_info(std::string_view name, getter value) noexcept : name_(name), value_(value) {}
        std::string_view name() const noexcept { return name_; }
        const value_descriptor& value() const noexcept { return value_(); }
      private:
        std::string_view name_;
        getter value_;
    };

    class value_view
    {
      public:
        value_view() = default;
        value_view(const value_descriptor& descriptor, const void* data, void* writable = nullptr,
                   std::shared_ptr<const void> storage = {})
            : descriptor_(&descriptor), data_(data), writable_(writable), storage_(std::move(storage)) {}
        explicit operator bool() const noexcept { return descriptor_ != nullptr && data_ != nullptr; }
        const value_descriptor& descriptor() const;
        const void* data() const noexcept { return data_; }
        void* mutable_data() const noexcept { return writable_; }
        bool is_writable() const noexcept { return writable_ != nullptr; }
        // Preserve both ownership chains when a getter returns a reference into an owned snapshot.
        void retain_parent(const value_view& parent);
        // Keep auxiliary argument or result storage alive without changing access capabilities.
        void retain_storage(std::shared_ptr<const void> storage);
        std::size_t size() const;
        value_view select_member(std::string_view name) const;
        value_view select_index(std::size_t index) const;
        value_view select_key(std::size_t index) const;
        value_view dereference() const;
        void serialize(writer& out) const;
        void deserialize(reader& in) const;
      private:
        friend class value_descriptor;
        const value_descriptor* descriptor_ = nullptr;
        const void* data_ = nullptr;
        void* writable_ = nullptr;
        // Proxy ranges (notably vector<bool>) are readable snapshots; their lifetime is the view.
        std::shared_ptr<const void> storage_;
    };

    class value_descriptor
    {
      public:
        using object_getter = const type_descriptor& (*)() noexcept;
        using write_function = void (*)(writer&, const void*);
        using read_function = void (*)(reader&, void*);
        using count_function = std::size_t (*)(const void*);
        using select_function = value_view (*)(const void*, void*, std::size_t);

        constexpr value_descriptor(type_id id, std::string_view name, std::size_t size, std::size_t alignment,
            value_shape shape, bool complete, std::span<const value_child_info> children, object_getter object,
            const enum_descriptor* enumeration, write_function write, read_function read, read_function assign,
            count_function count, select_function select, select_function key, std::string_view reason) noexcept
            : id_(id), name_(name), size_(size), alignment_(alignment), shape_(shape), complete_(complete),
              children_(children), object_(object), enumeration_(enumeration), write_(write), read_(read), assign_(assign),
              count_(count), select_(select), key_(key), reason_(reason) {}

        type_id id() const noexcept { return id_; }
        std::string_view name() const noexcept { return name_; }
        std::size_t size() const noexcept { return size_; }
        std::size_t alignment() const noexcept { return alignment_; }
        value_shape shape() const noexcept { return shape_; }
        bool is_complete() const noexcept { return complete_; }
        std::span<const value_child_info> children() const noexcept { return children_; }
        const type_descriptor* reflected_type() const noexcept { return object_ ? &object_() : nullptr; }
        const enum_descriptor* enumeration() const noexcept { return enumeration_; }
        bool is_serializable() const noexcept { return write_ != nullptr; }
        bool is_deserializable() const noexcept { return read_ != nullptr; }
        bool is_assignable() const noexcept { return assign_ != nullptr; }
        bool can_select_index() const noexcept { return count_ != nullptr && select_ != nullptr; }
        bool can_select_key() const noexcept { return key_ != nullptr; }
        bool can_dereference() const noexcept
        { return (shape_ == value_shape::pointer || shape_ == value_shape::optional) && can_select_index(); }
        std::string_view unsupported_reason() const noexcept { return reason_; }
        value_view view(void* data) const { return value_view(*this, data, data); }
        value_view view(const void* data) const { return value_view(*this, data); }
        void serialize(writer& out, const void* value) const;
        void deserialize(reader& in, void* value) const;
        // Copy, parse into the independent copy, then commit with a non-throwing swap. Pointer-bearing
        // graphs and readonly fields are excluded unless the owning engine explicitly supplies consent.
        void assign_serialized(reader& in, void* value) const;

      private:
        friend class value_view;
        type_id id_;
        std::string_view name_;
        std::size_t size_;
        std::size_t alignment_;
        value_shape shape_;
        bool complete_;
        std::span<const value_child_info> children_;
        object_getter object_;
        const enum_descriptor* enumeration_;
        write_function write_;
        read_function read_;
        read_function assign_;
        count_function count_;
        select_function select_;
        select_function key_;
        std::string_view reason_;
    };

    // Deterministic, cycle-safe closure through bases, fields, methods and wrapper/container children.
    // Unsupported descriptors remain in the result; consumers must report capabilities explicitly.
    std::vector<const value_descriptor*> value_type_closure(std::span<const type_descriptor* const> roots);
    std::string_view value_shape_name(value_shape shape) noexcept;
}

// Complete the descriptor/serializer definition cycle when this header is the public entrypoint.
#include "reflgen/runtime/type_descriptor.h"

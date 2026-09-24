#pragma once
// 런타임 등록소 — 이름·타입 식별자·동적 타입(RTTI)으로 서술자를 찾는다.
//
// 등록은 명시 호출이다(register_type<T>()). 정적 초기화 객체로 자동 등록하지 않는
// 이유: 정적 라이브러리에 든 등록 객체는 아무도 참조하지 않으면 링커가 버리고,
// 그러면 타입이 "가끔" 사라진다. 코드 생성기는 모듈별 등록 함수를 만들어 준다.
//
// default_registry() 는 모듈(DLL/EXE)마다 하나다. 여러 모듈이 한 등록소를 공유해야
// 하면 등록소를 하나 만들어 참조를 넘긴다.
#include "reflgen/runtime/type_descriptor.h"
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace reflgen
{
class registry
{
  public:
    registry() = default;
    registry(const registry&) = delete;
    registry& operator=(const registry&) = delete;

    // 같은 서술자를 다시 넣는 것은 무해하다. 같은 이름으로 다른 타입을 넣으면 실패한다 —
    // 다형 태그가 모호해지는 것을 파일을 읽는 시점이 아니라 등록 시점에 드러낸다.
    void add(const type_descriptor& type)
    {
        if (type.name().empty())
        {
            throw std::invalid_argument("cannot register a type without a name; name it with schema(...).named()");
        }

        std::unique_lock lock(mutex_);
        if (const auto found = by_name_.find(type.name()); found != by_name_.end())
        {
            if (found->second == &type)
            {
                return;
            }
            throw std::invalid_argument("a different type is already registered as '" + std::string(type.name()) + "'");
        }
        by_name_.emplace(type.name(), &type);
        by_id_.emplace(type.id(), &type);
        if (const std::type_info* rtti = type.rtti())
        {
            by_rtti_.emplace(std::type_index(*rtti), &type);
        }
        types_.push_back(&type);
    }

    template<reflectable T>
    const type_descriptor& add()
    {
        const type_descriptor& type = type_descriptor_of<T>();
        add(type);
        return type;
    }

    const type_descriptor* find(std::string_view name) const
    {
        std::shared_lock lock(mutex_);
        const auto found = by_name_.find(name);
        return found != by_name_.end() ? found->second : nullptr;
    }

    const type_descriptor* find(type_id id) const
    {
        std::shared_lock lock(mutex_);
        const auto found = by_id_.find(id);
        return found != by_id_.end() ? found->second : nullptr;
    }

    // 동적 타입으로 찾는다 — 다형 타입만 걸린다.
    const type_descriptor* find(const std::type_info& dynamic_type) const
    {
        std::shared_lock lock(mutex_);
        const auto found = by_rtti_.find(std::type_index(dynamic_type));
        return found != by_rtti_.end() ? found->second : nullptr;
    }

    // 등록 순서대로의 사본 — 순회 중 등록이 끼어들어도 안전하게.
    std::vector<const type_descriptor*> types() const
    {
        std::shared_lock lock(mutex_);
        return types_;
    }

    std::size_t size() const
    {
        std::shared_lock lock(mutex_);
        return types_.size();
    }

  private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string_view, const type_descriptor*> by_name_;
    std::unordered_map<type_id, const type_descriptor*> by_id_;
    std::unordered_map<std::type_index, const type_descriptor*> by_rtti_;
    std::vector<const type_descriptor*> types_;
};

inline registry& default_registry()
{
    static registry instance;
    return instance;
}

template<reflectable T>
const type_descriptor& register_type(registry& target = default_registry())
{
    return target.add<T>();
}
} // namespace reflgen

// 꼬리 include — 순서의 이유는 type_descriptor.h 끝의 설명을 볼 것.
#include "reflgen/serial/serializer.h"

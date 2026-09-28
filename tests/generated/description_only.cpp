// NO_REGISTRATION 시험 — 서술은 있다(schema_of·for_each_field). 등록 함수만 target 에 없다
// (check_no_registration.cmake).
#include "description_only.h"

namespace description_only
{
    static_assert(reflgen::reflectable<handle_holder>);
    static_assert(reflgen::field_count<handle_holder>() == 2);
    static_assert(reflgen::schema_of<native_handle>.field_count == 1);

    int generation_of(const handle_holder& holder)
    {
        int generation = 0;
        reflgen::for_each_field(holder, [&](const auto& field, const auto& value) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, int>)
            {
                generation = field.name == "generation" ? value : generation;
            }
        });
        return generation;
    }
} // namespace description_only

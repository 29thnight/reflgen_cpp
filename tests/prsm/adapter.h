#pragma once
#include <reflgen/prsm/access.h>

namespace fixture
{
    inline demo::Counter* object = nullptr;
    inline bool context = true;
    inline thread_local bool game_thread = false;
    inline uint64_t live_handle = 0x1234000100000001ULL;
    inline int guards = 0;
    inline int acquisitions = 0;
    inline int owner_calls = 0;
} // namespace fixture

namespace reflgen::prsm
{
    template<>
    class binding_access<demo::Counter>
    {
      public:
        explicit binding_access(uint64_t handle)
        {
            ++fixture::acquisitions;
            if (!fixture::context)
                status_ = REFLGEN_PRSM_BINDING_INVALID_CONTEXT;
            else if (!fixture::game_thread)
                status_ = REFLGEN_PRSM_BINDING_WRONG_THREAD;
            else if (!fixture::object || handle != fixture::live_handle)
                status_ = REFLGEN_PRSM_BINDING_INVALID_HANDLE;
            else
            {
                object_ = fixture::object;
                ++fixture::guards;
            }
        }
        binding_access(const binding_access&) = delete;
        ~binding_access() noexcept
        {
            if (object_)
                --fixture::guards;
        }
        int32_t status() const noexcept { return status_; }
        demo::Counter* get() const noexcept { return object_; }

      private:
        int32_t status_ = REFLGEN_PRSM_BINDING_OK;
        demo::Counter* object_ = nullptr;
    };
} // namespace reflgen::prsm
namespace reflgen::prsm
{
    template<>
    class binding_owner<demo::Counter>
    {
      public:
        static int32_t get(uint64_t* output) noexcept
        {
            ++fixture::owner_calls;
            *output = fixture::live_handle;
            return REFLGEN_PRSM_BINDING_OK;
        }
    };
} // namespace reflgen::prsm

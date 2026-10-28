#include "types.h"
#include <stdexcept>
#include <atomic>
#include <memory>
#include <cstddef>
#include <type_traits>
#if defined(_WIN32)
#define FS_EXPORT __declspec(dllexport)
#else
#define FS_EXPORT __attribute__((visibility("default")))
#endif
fsharp_fixture::point fsharp_fixture::counter::failing_point(point) const { throw std::runtime_error("fixture"); }
void fsharp_fixture::counter::fail() const { throw std::runtime_error("fixture"); }
extern "C" FS_EXPORT void* fsharp_fixture_counter()
{
    static fsharp_fixture::counter value;
    return &value;
}

static_assert(std::is_standard_layout_v<fsharp_fixture::point>);
static_assert(std::is_trivially_copyable_v<fsharp_fixture::point>);
extern "C" FS_EXPORT int fsharp_fixture_point_size() { return static_cast<int>(sizeof(fsharp_fixture::point)); }
extern "C" FS_EXPORT int fsharp_fixture_point_alignment() { return static_cast<int>(alignof(fsharp_fixture::point)); }
extern "C" FS_EXPORT int fsharp_fixture_point_offset_x() { return static_cast<int>(offsetof(fsharp_fixture::point, x)); }
extern "C" FS_EXPORT int fsharp_fixture_point_offset_y() { return static_cast<int>(offsetof(fsharp_fixture::point, y)); }

namespace { std::atomic<int> owned_destroyed{0}; }
int fsharp_fixture::counter::consume_owned(owned_counter* value, bool should_throw) const
{
    std::unique_ptr<owned_counter> owned(value);
    if (should_throw) throw std::runtime_error("fixture");
    return owned->read();
}
fsharp_fixture::owned_counter* fsharp_fixture::counter::swap_owned(owned_counter* value) const
{
    std::unique_ptr<owned_counter> owned(value);
    return new owned_counter;
}
void fsharp_fixture::counter::consume_void(owned_counter* value) const { std::unique_ptr<owned_counter> owned(value); }
fsharp_fixture::point fsharp_fixture::counter::consume_point(owned_counter* value) const
{
    std::unique_ptr<owned_counter> owned(value);
    return {owned->read(), 11};
}
fsharp_fixture::owned_counter* fsharp_fixture::counter::failing_owned() const { throw std::runtime_error("fixture"); }
fsharp_fixture::owned_counter* fsharp_fixture::counter::create_owned() const { return new owned_counter; }
fsharp_fixture::owned_counter::~owned_counter() noexcept { ++owned_destroyed; }
extern "C" FS_EXPORT void* fsharp_fixture_create_owned() { return new fsharp_fixture::owned_counter; }
extern "C" FS_EXPORT int fsharp_fixture_destroyed_count() { return owned_destroyed.load(); }

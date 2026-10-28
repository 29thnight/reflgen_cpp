#include "types.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define FIXTURE_EXPORT extern "C" __declspec(dllexport)
#else
#define FIXTURE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace
{
    using namespace fixture;
    constexpr ObjectHandle live_owner{17u, 9u};
    constexpr std::uint64_t live_body = 0xfedcba9876543210ull;
    constexpr char owner_name[] = "\xed\x95\x9c\xea\xb8\x80-\xf0\x9f\x8e\xb9";
    Float3 position{1.25f, -2.5f, 3.75f};
    double forced_seconds = 0.0;
    unsigned char payload_bytes[32]{};
    int payload_size = 0;
    std::uint64_t sequence_number = 0;

    int FIXTURE_CALL is_alive(ObjectHandle owner)
    {
        return owner.index == live_owner.index && owner.generation == live_owner.generation;
    }
    ObjectHandle FIXTURE_CALL find_name(const char* name)
    {
        return name && std::strcmp(name, owner_name) == 0 ? live_owner : ObjectHandle{};
    }
    int FIXTURE_CALL get_name(ObjectHandle owner, char* output, int capacity)
    {
        const int count = static_cast<int>(sizeof(owner_name) - 1);
        if (!is_alive(owner) || !output || capacity <= count) return -1;
        std::memcpy(output, owner_name, sizeof(owner_name));
        return count;
    }
    Float3 FIXTURE_CALL get_position(ObjectHandle owner) { return is_alive(owner) ? position : Float3{}; }
    void FIXTURE_CALL set_position(ObjectHandle owner, Float3 value) { if (is_alive(owner)) position = value; }
    Float4 FIXTURE_CALL get_rotation(ObjectHandle) { return {0.f, 0.f, 0.f, 1.f}; }
    Float2 FIXTURE_CALL screen_size() { return {1920.f, 1080.f}; }
    float FIXTURE_CALL field_of_view(ObjectHandle owner) { return is_alive(owner) ? 72.5f : 0.f; }
    unsigned long long FIXTURE_CALL frame_count() { return 0xf123456789abcdefull; }
    std::uint32_t FIXTURE_CALL profiler_register(const char* name) { return name ? 0xfedcba98u : 0u; }
    std::uint64_t FIXTURE_CALL body_find(ObjectHandle owner) { return is_alive(owner) ? live_body : 0; }
    int FIXTURE_CALL body_read(ObjectHandle owner, std::uint64_t instance, BodyState* output)
    {
        if (!output) return 1;
        *output = {};
        if (!is_alive(owner) || instance != live_body) return 2;
        *output = {2, 7.5f, position, {0.f, 0.f, 0.f, 1.f}, {11.f, 12.f, 13.f}, {21.f, 22.f, 23.f}};
        return 0;
    }
    int FIXTURE_CALL character_read(ObjectHandle owner, std::uint64_t instance, CharacterState* output)
    {
        if (!output) return 1;
        *output = {};
        if (!is_alive(owner) || instance != live_body) return 2;
        output->position = position;
        output->flags = 9;
        output->tick = 0xffffeeee11112222ull;
        output->movement_velocity = {4.f, 5.f, 6.f};
        output->forced_remaining = forced_seconds;
        return 0;
    }
    int FIXTURE_CALL character_force(ObjectHandle owner, std::uint64_t instance, Float3 velocity, double seconds)
    {
        if (!is_alive(owner) || instance != live_body) return 2;
        forced_seconds = seconds + velocity.y;
        return 0;
    }
    int FIXTURE_CALL query_batch(ObjectHandle owner, const QueryRequest* requests, int count,
                                PhysicsHit* hits, int capacity, BatchResult* summaries)
    {
        if (!is_alive(owner) || !requests || !hits || !summaries || count != 2 || capacity < 2) return 1;
        for (int i = 0; i < count; ++i)
        {
            hits[i] = {owner.index, owner.generation, live_body, 23u, 41u, 0xffff000011112222ull,
                       requests[i].origin, requests[i].direction, requests[i].distance_or_radius, 1};
            summaries[i] = {0, 1, 1, 0};
        }
        return 0;
    }
    std::uint64_t FIXTURE_CALL audio_play(std::uint64_t scope, AssetId asset, int sourceKind, int mode,
                                         Float3 value, ObjectHandle owner, const AudioSettings* settings,
                                         const AudioParameter* parameters, int count)
    {
        if (!is_alive(owner) || scope != 91 || asset.first != 101 || asset.second != 202 || sourceKind != 3 ||
            mode != 4 || value.x != 9.f || !settings || settings->volume != .25f ||
            !parameters || count != 1 || parameters[0].integer != 55 || !parameters[0].name ||
            !parameters[0].text || std::strcmp(parameters[0].name, "gain") != 0 ||
            std::strcmp(parameters[0].text, owner_name) != 0) return 0;
        return 0xf0e0d0c0b0a09080ull;
    }
    int FIXTURE_CALL asset_request(const AssetLink* link, std::uint32_t type,
                                   const TextureVariant* variant, AssetToken* token)
    {
        if (!link || !variant || !token || link->asset.first != 101 || link->subasset.second != 404 ||
            link->kind != 3 || variant->role != 6) return 1;
        *token = {77u, 88u, type};
        return 0;
    }
    int FIXTURE_CALL simulation_status(TickStatus* output)
    {
        if (!output) return 1;
        *output = {8, 11, 12, 3, 1};
        return 0;
    }
    int FIXTURE_CALL simulation_submit(std::uint64_t epoch, std::uint64_t tick, std::uint32_t kind,
                                      const std::byte* bytes, int count, std::uint64_t* sequence)
    {
        if (epoch != 8 || tick != 13 || kind != 7 || !bytes || !sequence || count < 0 || count > 32) return 1;
        std::memcpy(payload_bytes, bytes, static_cast<std::size_t>(count));
        payload_size = count;
        *sequence = ++sequence_number;
        return 0;
    }
    int FIXTURE_CALL simulation_read(std::uint64_t epoch, std::uint64_t after, TickSnapshot* header,
                                    std::byte* bytes, int capacity)
    {
        if (epoch != 8 || after != 12 || !header || !bytes || capacity < payload_size) return 1;
        *header = {epoch, 13, 1, 0, static_cast<std::uint32_t>(payload_size), 7};
        std::memcpy(bytes, payload_bytes, static_cast<std::size_t>(payload_size));
        return 0;
    }
    int FIXTURE_CALL read_contact(Contact* output)
    {
        if (!output) return 1;
        *output = {};
        output->first.owner = live_owner;
        output->first.component = live_body;
        output->first.role = {101, 202};
        output->second.owner = {23, 11};
        output->second.role = {303, 404};
        output->tick = 9001;
        output->requiredContacts = 2;
        output->point = {3.f, 4.f, 5.f};
        return 0;
    }
    int FIXTURE_CALL read_message(ScriptMessage* output)
    {
        if (!output) return 1;
        *output = {};
        output->instanceId = 314;
        std::memcpy(output->name, owner_name, sizeof(owner_name));
        output->name[63] = 'Z';
        return 0;
    }
    int FIXTURE_CALL read_tree(TreeNode* node, BlackboardEntry* entry)
    {
        if (!node || !entry) return 1;
        *node = {};
        *entry = {};
        node->id = 0xfedcba9876543210ull;
        node->weight = .75f;
        node->name[0] = 'N';
        node->name[63] = 'n';
        node->scriptName[0] = 'S';
        node->scriptName[63] = 's';
        entry->intValue = 88;
        entry->w = 4.5f;
        entry->key[0] = 'K';
        entry->key[63] = 'k';
        entry->stringValue[0] = 'V';
        entry->stringValue[127] = 'v';
        return 0;
    }
    int FIXTURE_CALL read_pointer(const void* input)
    {
        return input ? *static_cast<const int*>(input) : -1;
    }
    int FIXTURE_CALL native_tick(float dt) { return static_cast<int>(dt * 1000.f); }

    ScriptApiTable table{
        CreatorScriptApiVersion, static_cast<int>(sizeof(ScriptApiTable)), find_name, is_alive, get_name,
        get_position, set_position, get_rotation, screen_size, field_of_view, frame_count,
        profiler_register, body_find, body_read, character_read, character_force, query_batch,
        audio_play, asset_request, simulation_status, simulation_submit, simulation_read,
        read_contact, read_message, read_tree, read_pointer, native_tick};

    // The native fixture intentionally retains this callback until clear_callback.
    // F# must keep the generated callback owner alive for exactly that interval.
    using TickCallback = int(FIXTURE_CALL*)(float);
    TickCallback retained_tick = nullptr;
}

FIXTURE_EXPORT void* fixture_get_table() { return &table; }
FIXTURE_EXPORT int fixture_table_size() { return static_cast<int>(sizeof(table)); }
FIXTURE_EXPORT void fixture_set_callback(void* address) { retained_tick = reinterpret_cast<TickCallback>(address); }
FIXTURE_EXPORT int fixture_invoke_callback(float dt) { return retained_tick ? retained_tick(dt) : -1; }
FIXTURE_EXPORT void fixture_clear_callback() { retained_tick = nullptr; }

// Independent sizeof/offsetof values validate the emitted F# representation.
FIXTURE_EXPORT int fixture_layout(int value)
{
    switch (value)
    {
    case 0: return static_cast<int>(sizeof(fixture::ObjectHandle));
    case 1: return static_cast<int>(sizeof(fixture::CharacterState));
    case 2: return static_cast<int>(offsetof(fixture::CharacterState, forced_remaining));
    case 3: return static_cast<int>(sizeof(fixture::Contact));
    case 4: return static_cast<int>(offsetof(fixture::Contact, tick));
    case 5: return static_cast<int>(sizeof(fixture::ScriptMessage));
    case 6: return static_cast<int>(sizeof(fixture::TreeNode));
    case 7: return static_cast<int>(sizeof(fixture::BlackboardEntry));
    case 8: return static_cast<int>(sizeof(fixture::AudioParameter));
    case 9: return static_cast<int>(offsetof(fixture::AudioParameter, text));
    case 10: return static_cast<int>(offsetof(fixture::ScriptApiTable, Entity_IsAlive));
    case 11: return static_cast<int>(sizeof(fixture::AssetLink));
    case 12: return static_cast<int>(sizeof(fixture::PhysicsHit));
    default: return -1;
    }
}

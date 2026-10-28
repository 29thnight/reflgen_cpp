#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define FIXTURE_CALL __stdcall
#else
#define FIXTURE_CALL
#endif

// Representative ABI shapes from CreatorEngine f32f8b0, not engine classes or a
// substitute implementation of the engine's object registry/physics/runtime.
namespace fixture
{
    inline constexpr int CreatorScriptApiVersion = 46;

    struct ObjectHandle
    {
        std::uint32_t index;
        std::uint32_t generation;
        bool IsValid() const { return generation != 0; }
    };
    struct Float2 { float x, y; };
    struct Float3 { float x, y, z; };
    struct Float4 { float x, y, z, w; };
    struct AssetId { std::uint64_t first, second; };
    struct AssetLink
    {
        AssetId asset, subasset;
        std::uint32_t kind, reserved;
    };
    struct AssetToken { std::uint32_t index, generation, type; };
    struct TextureVariant { std::uint32_t colorSpace, compress, role; };
    struct BodyState
    {
        std::int32_t kind;
        float mass;
        Float3 position;
        Float4 rotation;
        Float3 linear_velocity, angular_velocity;
    };
    struct CharacterState
    {
        Float3 position, foot_position, actual_displacement, desired_velocity;
        float fall_velocity;
        std::uint32_t flags;
        std::uint64_t tick;
        Float3 movement_velocity;
        double forced_remaining;
    };
    struct QueryRequest
    {
        std::int32_t kind;
        Float3 origin, direction;
        float distance_or_radius;
        std::uint32_t layers;
        std::int32_t include_sensors, offset, capacity;
    };
    struct PhysicsHit
    {
        std::uint32_t object_index, object_generation;
        std::uint64_t component;
        std::uint32_t shape, face;
        std::uint64_t layer;
        Float3 point, normal;
        float distance;
        std::int32_t has_location;
    };
    struct BatchResult { std::int32_t error, written, required_capacity, truncated; };
    struct AudioSettings
    {
        float volume, pitch;
        int priority, loop, bus;
        float spatialBlend, minimumDistance, maximumDistance;
        int rolloff, useReverbSend;
        float reverbSendDecibels;
        int ownerPolicy;
        std::uint32_t concurrencyGroup;
        int preemptSameClip, allowVirtualization;
    };
    struct AudioParameter
    {
        const char* name;
        int type, integer;
        float number;
        const char* text;
    };
    struct ContactEndpoint
    {
        ObjectHandle owner;
        std::uint64_t component;
        std::uint32_t shape, bodyGeneration, bodySlot, reserved;
        AssetId role;
    };
    struct Contact
    {
        ContactEndpoint first, second;
        std::uint64_t tick;
        int kind, contactCount, requiredContacts;
        Float3 point;
    };
    struct ScriptMessage { int instanceId; char name[64]; };
    struct TreeNode
    {
        std::uint64_t id, parentId;
        std::int32_t type, policy, isRoot, hasScript;
        float weight;
        std::int32_t childOrder;
        char name[64], scriptName[64];
    };
    struct BlackboardEntry
    {
        std::int32_t type, boolValue, intValue;
        float floatValue, x, y, z, w;
        char key[64], stringValue[128];
    };
    struct TickStatus
    {
        std::uint64_t epoch, committed, executing;
        std::uint32_t queued, active;
    };
    struct TickSnapshot
    {
        std::uint64_t epoch, tick;
        std::uint32_t applied, rejected, bytes, schema;
    };

    struct ScriptApiTable
    {
        int version;
        int structSize;
        ObjectHandle (FIXTURE_CALL* Entity_FindByName)(const char* name);
        int (FIXTURE_CALL* Entity_IsAlive)(ObjectHandle owner);
        int (FIXTURE_CALL* Entity_GetName)(ObjectHandle owner, char* output, int capacity);
        Float3 (FIXTURE_CALL* Transform_GetPosition)(ObjectHandle owner);
        void (FIXTURE_CALL* Transform_SetPosition)(ObjectHandle owner, Float3 value);
        Float4 (FIXTURE_CALL* Transform_GetRotation)(ObjectHandle owner);
        Float2 (FIXTURE_CALL* Camera_GetScreenSize)();
        float (FIXTURE_CALL* Camera_GetFov)(ObjectHandle owner);
        unsigned long long (FIXTURE_CALL* Engine_GetFrameCount)();
        std::uint32_t (FIXTURE_CALL* Profiler_Register)(const char* name);
        std::uint64_t (FIXTURE_CALL* Body_Find)(ObjectHandle owner);
        int (FIXTURE_CALL* Body_Read)(ObjectHandle owner, std::uint64_t instance, BodyState* output);
        int (FIXTURE_CALL* Character_Read)(ObjectHandle owner, std::uint64_t instance, CharacterState* output);
        int (FIXTURE_CALL* Character_Force)(ObjectHandle owner, std::uint64_t instance, Float3 velocity, double seconds);
        int (FIXTURE_CALL* Physics_QueryBatch)(ObjectHandle owner, const QueryRequest* requests, int requestCount,
                                             PhysicsHit* hits, int capacity, BatchResult* summaries);
        std::uint64_t (FIXTURE_CALL* Audio_Play)(std::uint64_t scope, AssetId asset, int sourceKind, int mode,
                                               Float3 position, ObjectHandle owner, const AudioSettings* settings,
                                               const AudioParameter* parameters, int parameterCount);
        int (FIXTURE_CALL* Asset_Request)(const AssetLink* link, std::uint32_t concreteType,
                                        const TextureVariant* variant, AssetToken* token);
        int (FIXTURE_CALL* Simulation_Status)(TickStatus* output);
        int (FIXTURE_CALL* Simulation_Submit)(std::uint64_t epoch, std::uint64_t tick, std::uint32_t kind,
                                            const std::byte* payload, int bytes, std::uint64_t* sequence);
        int (FIXTURE_CALL* Simulation_Read)(std::uint64_t epoch, std::uint64_t after,
                                          TickSnapshot* header, std::byte* payload, int capacity);
        int (FIXTURE_CALL* ReadContact)(Contact* output);
        int (FIXTURE_CALL* ReadMessage)(ScriptMessage* output);
        int (FIXTURE_CALL* ReadTree)(TreeNode* node, BlackboardEntry* entry);
        int (FIXTURE_CALL* ReadPointer)(const void* input);
        // The same int(float) signature is used by the engine's reverse tick
        // entry point. Its generated delegate is also used for the GC test.
        int (FIXTURE_CALL* Managed_Tick)(float dt);
    };

    using ScriptApiBinding [[reflgen::table("ScriptApi", "version", "structSize", CreatorScriptApiVersion)]] = ScriptApiTable;

    static_assert(sizeof(ObjectHandle) == 8 && sizeof(Float3) == 12 && sizeof(Float4) == 16);
    static_assert(sizeof(AssetLink) == 40 && sizeof(AssetToken) == 12);
    static_assert(sizeof(BodyState) == 60);
    static_assert(sizeof(CharacterState) == 88 && offsetof(CharacterState, tick) == 56 &&
                  offsetof(CharacterState, forced_remaining) == 80);
    static_assert(sizeof(QueryRequest) == 48 && sizeof(PhysicsHit) == 64 && sizeof(BatchResult) == 16);
    static_assert(sizeof(AudioSettings) == 60);
    static_assert(sizeof(ContactEndpoint) == 48 && sizeof(Contact) == 128);
    static_assert(sizeof(ScriptMessage) == 68 && sizeof(TreeNode) == 168 && sizeof(BlackboardEntry) == 224);
    static_assert(sizeof(TickStatus) == 32 && sizeof(TickSnapshot) == 32);
}

# Direct F# native-table bindings for CreatorEngine

This extension generates the native ABI portion needed to replace a C# binding
assembly with F# code. It does **not** migrate CreatorEngine's public scripting
types, host bootstrap, loader or lifecycle implementation by itself. The executable
fixture in this repository references the generated F# file and the .NET/F# runtime
only; it calls a real C++ shared library without a C# bridge.

## Engine state used for this work

The engine assessment uses
[CreatorEngine `f32f8b0f638f36a22034c1229014090e18b510a5`](https://github.com/29thnight/CreatorEngine/tree/f32f8b0f638f36a22034c1229014090e18b510a5).
At that revision:

- [`ScriptRuntime.fsproj`](https://github.com/29thnight/CreatorEngine/blob/f32f8b0f638f36a22034c1229014090e18b510a5/ScriptRuntime/ScriptRuntime.fsproj)
  builds the F# process-resident managed runtime, but still references
  `ScriptCore/ScriptCore.csproj`.
- [`ScriptCore/Native.cs`](https://github.com/29thnight/CreatorEngine/blob/f32f8b0f638f36a22034c1229014090e18b510a5/ScriptCore/Native.cs)
  owns the C# function-pointer table, native wrappers and bind checks.
  [`ClrHost.cpp`](https://github.com/29thnight/CreatorEngine/blob/f32f8b0f638f36a22034c1229014090e18b510a5/Engine/SceneRuntime/ClrHost.cpp)
  supplies that table from the executable. The table is flat: two `int` fields
  (`version`, `structSize`) followed by 205 function pointers. Loading a named DLL
  for every generated API is therefore not the engine's existing call model.
- The C++ version constant and `Native.ExpectedVersion` are **46**, while
  [`ScriptRuntime/Runtime.fs`](https://github.com/29thnight/CreatorEngine/blob/f32f8b0f638f36a22034c1229014090e18b510a5/ScriptRuntime/Runtime.fs)
  still reports **45**. `ManagedRuntime.LoadImplementation` rejects that mismatch.
  The API45 design/acceptance documents describe an earlier source identity; they
  do not establish that the current API46 runtime is consistent.
- [`BuildTool/GameCompiler.cs`](https://github.com/29thnight/CreatorEngine/blob/f32f8b0f638f36a22034c1229014090e18b510a5/BuildTool/GameCompiler.cs)
  explicitly ships and references `ScriptCore.dll`. The engine's overlay port pins
  reflgen commit `357971b7f07f48393c0995cdd7fa52bfe29c3c47`, before the separate F#
  P/Invoke extension on `feat/prsm-production-bindings`.

The existing `--interop` F# backend already supports a separate native DLL,
selected primitive methods, bounded value/pointer contracts, UTF-8 helpers and
explicit ownership helpers. Those features do not define the injected table's
layout, automatically declare the engine's nested POD types, or remove the C#
assembly reference. The `--prsm` receiver protocol is also a separate ABI: adopting
its handle bit interpretation or its per-call component lookup would change an
existing CreatorEngine contract.

## What the table backend generates

The engine exposes an explicitly selected C++ ABI table through a type alias:

```cpp
inline constexpr int CreatorScriptApiVersion = 46;

struct ScriptApiTable
{
    int version;
    int structSize;
    int (*Entity_IsAlive)(ObjectHandle owner);
    Float3 (*Transform_GetPosition)(ObjectHandle owner);
    void (*Transform_SetPosition)(ObjectHandle owner, Float3 value);
};

using ScriptApiBinding
    [[reflgen::table("ScriptApi", "version", "structSize", CreatorScriptApiVersion)]]
    = ScriptApiTable;
```

Use the actual calling convention from the engine declaration, including
`__stdcall` for its Windows boundary. The version is evaluated from the C++
constant; a second hard-coded F# version is unnecessary.

```sh
reflgen --module engine --output generated --fsharp-table \
  --fsharp-namespace CreatorEngine.Native ScriptApi.h -- -std=c++20
```

The generated outputs are `reflgen_engine_native.fs`,
`reflgen_engine_native.cpp`, and `reflgen_engine_native.json`. Compile the C++
proof into the native provider and place the F# file before its consumers in the
F# project's compile order.

For CMake, the helper compiles the generated C++ proof into the provider:

```cmake
reflgen_generate(engine_native
    MODULE engine
    HEADERS ScriptApi.h
    FSHARP_TABLE
    FSHARP_NAMESPACE CreatorEngine.Native
    NO_REGISTRATION)
get_target_property(native_fs engine_native REFLGEN_FSHARP_TABLE_SOURCE)
get_target_property(native_manifest engine_native REFLGEN_FSHARP_TABLE_MANIFEST)
```

The path properties can contain configuration generator expressions. Resolve them
for the selected configuration when passing the F# source path to a managed build;
the executable fixture's CMake registration demonstrates that wiring.

For an MSBuild C++ project using `reflgen.targets`:

```xml
<PropertyGroup>
  <ReflgenFSharpTable>true</ReflgenFSharpTable>
  <ReflgenFSharpNamespace>CreatorEngine.Native</ReflgenFSharpNamespace>
</PropertyGroup>
```

`ReflgenFSharpTableSource` and `ReflgenFSharpTableManifest` name the generated F# and
JSON files; the native proof is compiled automatically. Pass the resolved source
path from the native build to the managed project and include it explicitly:

```xml
<ItemGroup>
  <Compile Include="$(ReflgenFSharpTableSource)" />
  <Compile Include="NativeBootstrap.fs" />
</ItemGroup>
```

The native project's properties do not automatically propagate into an unrelated
F# project. Establish the native-generation dependency and the property/path handoff
in the solution's build orchestration.

The F# surface contains blittable value-type declarations for reachable ABI
records, with explicit native sizes and field offsets, including nested records,
record pointer members and inline fixed arrays. Fixed arrays use inline value
storage with `Item0` through `ItemN`; they are not managed array references or
`ByValArray` marshaling. Numeric widths and signedness follow the parsed target.
Borrowed pointers remain `nativeint`; their size, capacity and lifetime are not
inferred from adjacent integers.

Each table entry has a typed delegate with its native calling convention. Bind
constructs and caches these delegates once. Subsequent calls use the cached
delegate, without an API-name search or an intermediary C# assembly. This is a
.NET interop implementation, not a claim of zero transition cost, NativeAOT support
or complete CreatorEngine performance equivalence.

```fsharp
module Native = reflgen_engine_native

// The native host supplies the table, readable byte count, and fingerprint
// from the same native build. Do not substitute the generated constant for
// the fingerprint supplied by the provider.
let api = Native.ScriptApi.Bind(tableAddress, availableBytes, nativeFingerprint)
let alive = api.Entity_IsAlive.Invoke(owner) <> 0
```

Bind checks the readable extent before accessing the header, validates the native
version and complete table size, checks the generated ABI fingerprint and rejects
null required function slots. A rejected candidate does not mutate an already
created binding. All slots are required in this first table contract. This is
stricter than the engine's hand-written C# checks, which require only selected
later-added slots; partial modeled tables must provide every declared entry or
use a smaller, explicitly selected fixture table.

The fingerprint covers more than total size or field count: native target,
record layouts, field order/offsets, function signatures and calling conventions
are part of the contract. The generated native proof verifies the declaration
against the parsed facts and exposes `reflgen_engine_native_abi_fingerprint()`.
Exporting that single function is useful for tests or a DLL provider; an
executable host may instead pass its result alongside the table during bootstrap.
The fingerprint detects incompatible generated surfaces; it is not a security
authentication protocol or a proof that an arbitrary address is safe to read.

### Supported ABI boundary

The current table backend accepts **x64 Windows and x64 Linux** targets with 64-bit
pointers. Generation must use the provider's target and ABI options; generated
native assertions and F# target checks reject a mismatch. The separate method
interop backend's broader parse-target list does not extend this table contract.

The frontend rejects unsupported selected declarations instead of choosing a
marshaller: by-value `bool`, references, unions, bit fields, records with bases,
virtual members or nontrivial destructors, packed or over-aligned layouts outside
the supported natural layout, variadic functions, and unsupported calling
conventions such as vectorcall. The generated C++ proof additionally requires
every emitted record to be standard-layout and trivially copyable; a record with
incompatible C++ copy semantics therefore fails the native build. Raw borrowed
pointers do not turn unsupported by-value types into automatically marshaled
objects. Generation errors publish no new table outputs; existing output files
may remain from an earlier successful run and must not be compiled after a failed
generation command.

## ABI shapes covered by the executable fixture

`tests/generated/fsharp_table_runtime` deliberately models representative engine
signatures rather than including the renderer, physics SDK or the actual engine
host. The 25-entry fixture table covers every return-type family present in the
engine's 205-entry table and the following argument/storage forms:

| Engine boundary | Fixture coverage |
|---|---|
| `ScriptObjectHandle { index, generation }` | Value arguments/results; stale generation rejection by native code |
| Float2/Float3/Float4 | Native aggregate results and arguments, including a `void` setter |
| Body/CCT identity | Independent 64-bit component instance, stale instance failure, nested output records, double-aligned character state |
| Physics batch query | Read-only record arrays, writable hit arrays, parallel result arrays, explicit counts and capacities |
| Audio | Nine-argument call with multiple aggregate/scalar arguments and borrowed UTF-8 pointers nested inside a record |
| Asset requests | Nested asset/subasset identity records and concrete-type/generation token output |
| Simulation exchange | Explicit-length binary payload including NUL and non-UTF-8 bytes; 64-bit sequence output and snapshot header |
| Contact batches | Multiple levels of nested handle/identity/vector records, including tail offsets |
| Script messages and behavior trees | Inline 64-byte and 128-byte arrays, multiple array fields, first/last element checks |
| Reverse calls | A generated typed delegate used in both directions, native retention across full GC, explicit revocation before disposal, managed exception containment |
| Bind validation | Null pointer, truncated readable extent/header, wrong version, smaller/larger size, wrong fingerprint and missing function slot |
| Dependency boundary | Managed assembly references are limited to .NET and FSharp.Core; no ScriptCore or other C# bridge assembly |

The native fixture publishes independent `sizeof`/`offsetof` values and exercises
real F#→C++ and C++→F# calls. It does not validate CreatorEngine's current full
205-entry provider, its exact registry implementation, its hostfxr boot sequence,
its collectible loader, or an Editor/Player frame.

## Lifetime and error responsibilities

The generator preserves the bits and layout of engine handles; it does not replace
them with raw object pointers, a new registry or a `SafeHandle` that deletes an
engine object. CreatorEngine's
[`ScriptObjectRegistry`](https://github.com/29thnight/CreatorEngine/blob/f32f8b0f638f36a22034c1229014090e18b510a5/Engine/SceneRuntime/ScriptObjectRegistry.h)
uses its own generation domain, including DDOL transfers. Physics body and
character wrappers additionally retain native component-instance identity.
Engine migration must retain both checks, together with the existing game-thread
and lifecycle rules.

`RootedCallback<T>` keeps an explicitly supplied managed delegate alive and exposes
its native address. The owner must outlive every native invocation. Stop/unregister
native access and wait for any in-flight callback before disposing the owner;
disposal alone cannot revoke a pointer already retained by native code. Catch
managed exceptions inside a raw callback and translate them into the existing status
contract. Alternatively, use the generated `root_<Table>_<Field>_callback` factory
with an explicit fallback result. It catches managed exceptions, records
`LastException` and returns that fallback to native code. The helper does not infer error values, synchronize native threads,
schedule onto the game thread, or implement a reload generation manager.

Similarly, `Bind` does not own the native provider. Its cached delegates remain
valid only while that native code remains loaded. Borrowed strings and buffers
must stay alive for every call that uses them. Do not pass `ce::string`, a C++
container or a managed string object as if it were a plain ABI record. Existing
CreatorEngine text entries explicitly use UTF-8 bytes and NUL/buffer capacity;
future UTF-16 engine-string entries require their own pointer/code-unit-length and
ownership contract.

These calls use the supplied function addresses directly. Unlike a generated C++
method wrapper, the table backend does not add a `try`/`catch` around the provider
or reinterpret its return values. The native provider must contain C++ exceptions
and define its own failure/output policy; F# callers preserve and check that policy.

## Completing the engine migration

An engine integration can use this generated surface after extracting its current
flat table and reachable POD declarations into a compiler-independent ABI header.
Keep native implementations and registration in the engine, and compile the
generated proof with the same target/ABI flags. Pass table address, readable size
and generated native fingerprint from the host to the F# bootstrap.

The native-table backend currently uses the **libclang header frontend**. It does
not read these tables from MSVC IFC files; the existing F# method-interop and PrSM
IFC paths do not imply table-backend support. CreatorEngine's table currently lives
in an anonymous namespace in `ClrHost.cpp`, and several supporting engine headers
depend on C++ modules or `import std`. Passing that whole implementation to this
header generator is not an established integration path. Move the ABI declarations
into a named namespace in a small header with ordinary includes and no engine
module imports, and make both the real native provider and generator consume that
same header. Do not keep a second manually synchronized copy as the production ABI
source. The test header in this repository is an independent fixture, not that
extracted production header.

The remaining engine work includes F# equivalents of the public Component/Entity
and native wrapper types, the lifecycle/serialization interfaces currently defined
in ScriptCore, unmanaged bootstrap entry points, resident/collectible load-context
sharing, SDK packaging and game compilation references. Derive the runtime API
version from the generated contract to avoid the observed 46/45 drift. Preserve
the current static F# lifecycle registration and one native-to-managed entry per
batch; table generation does not require per-component reflection or a new tick
execution model.

After that integration, re-run the engine's lifecycle, failed-reload preservation,
DDOL, generation/native-instance, serialization, physics/contact, Editor and cooked
Player acceptance gates on the new binaries. Passing this generator's independent
native/F# fixture is evidence for the new ABI-generation features, not evidence
that those engine migration steps have already happened.

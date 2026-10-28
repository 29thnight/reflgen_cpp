# reflgen

Macro-free reflection for C++20, with a serialization library whose formats you can swap.

[한국어](README.ko.md)

Backend selection, mixed projects and version guards: [backend guide](docs/reflection-backends.md).
Compiler, runtime and live VS validation: [verification record](docs/verification-2026-10-17.md).

Version **1.2.0** adds direct F# bindings for native function-pointer tables: generated value types,
version/layout/signature checks, cached typed delegates and explicit callback lifetime support. It also integrates
the F# P/Invoke, PrSM production-binding and IFC engine-reflection extensions. See the
[1.2.0 release notes](docs/releases/1.2.0.md), [interop guide](INTEROP.md), and
[CreatorEngine ABI assessment and integration guide](docs/creatorengine-fsharp-abi.md).
Windows packages include both generators, module sources and MSBuild/CMake integration.

The published NuGet **1.1.0** package and released VSIX **1.1.0** were also verified together in
VS 2026 **18.11.0**: nuget.org restore, mixed header/module Debug and Release execution,
save regeneration, standard/reflgen completion, Error List navigation and module IntelliSense passed.
See the [released-package verification](docs/verification-2026-10-17.md#nuget-110-and-released-vsix-verification) for scope and evidence.

- **Macro-free reflection API** — no declaration macros. Feature guards isolate C++26 syntax and compiler-specific
  module TLS storage; backend selection uses an actual compile probe.
- **C++20 baseline, C++23 supported** — standard library only. With the optional C++26 standard backend, only the way
  descriptions are produced changes; the query and serialization APIs stay (see the roadmap).
- **The whole STL serializes** — every standard container, adapter and value wrapper, plus user containers shaped
  like the STL, automatically.
- **A format is an interface** — implement `reflgen::writer` / `reflgen::reader` and you have a new format. JSON and
  binary backends are included.
- Header-only library plus a code generator. Verified with MSVC 19.51 (VS 18) and clang-cl 22 × C++20 and C++23,
  `/W4 /WX` and `-Wextra -Wpedantic -Werror`.

## Install

Windows x64 with Visual Studio 2026 (18.x), the version reflgen is verified with. The release packages below are
published on [GitHub Releases](https://github.com/29thnight/reflgen_cpp/releases) (NuGet also on
[nuget.org](https://www.nuget.org/packages/reflgen)); the generator in them ships with its own `libclang.dll` and the
matching clang builtin headers.

| Channel | For | How |
|---|---|---|
| NuGet [`reflgen`](https://www.nuget.org/packages/reflgen) | `.vcxproj` projects | Install `reflgen` from nuget.org (`packages.config`), or add a folder holding the downloaded `.nupkg` as a package source. Installing imports `reflgen.targets` into the project; nothing else to set. |
| vcpkg overlay port | vcpkg manifest projects (MSBuild or CMake) | `./scripts/make-overlay-port.ps1 -Destination <overlay-ports>` writes a port that fetches the `v<version>` tag. Add `reflgen` to `vcpkg.json`. MSBuild: import `<installed>\share\reflgen\msbuild\reflgen.targets` (it finds `tools\reflgen\reflgen.exe` itself). CMake: `find_package(reflgen CONFIG REQUIRED)`. |
| zip `reflgen-<version>-windows-x64.zip` | anything else | A CMake install layout (`bin`, `include`, `lib\cmake`, `share`). CMake: add it to `CMAKE_PREFIX_PATH` and `find_package(reflgen)`. MSBuild: import `share\reflgen\msbuild\reflgen.targets`. |
| VS extension `Reflgen.VisualStudio.vsix` | Visual Studio | Run the file to install it ([below](#visual-studio-extension)). |

The vcpkg port builds from source, so it needs the Visual Studio component "C++ Clang tools for Windows" (the
generator links the libclang that ships with Visual Studio). vcpkg's MSBuild integration reinstalls only when
`vcpkg.json` or `vcpkg-configuration.json` changes — after pointing the overlay port at another version, make sure
the installed headers actually changed.

From source: `cmake --preset release`, `cmake --build --preset release`,
`cmake --install build/release --prefix <folder>` gives the same layout as the zip. CMake 3.25 or later.

## Importing the library as named modules

Use `import reflgen.json;` in place of `#include "reflgen/json.h"`. Available modules are `reflgen.core`,
`reflgen.runtime`, `reflgen.serialization`, `reflgen.json`, `reflgen.binary`, and the umbrella `reflgen`.
Format modules re-export the common API, so importing JSON alone also provides schemas and runtime registration.
Include the standard headers your own code uses before importing modules.

```cpp
#include <string>
import reflgen.json;
struct item {
    int count = 7;
    static consteval auto reflect() {
        return reflgen::schema<item>(reflgen::field<&item::count>);
    }
};
auto text = reflgen::json::to_string(item{});
auto value = reflgen::json::from_string<item>(text);
```

Requires CMake 3.28+ and a module-capable toolchain; verified with MSVC + Ninja in Debug and Release.
For `add_subdirectory`, enable `REFLGEN_BUILD_MODULES` and link `reflgen::modules`. For an installed package:

```cmake
find_package(reflgen CONFIG REQUIRED)
reflgen_add_modules(my_reflgen_modules)
target_link_libraries(app PRIVATE my_reflgen_modules)
set_target_properties(app PROPERTIES CXX_SCAN_FOR_MODULES ON)
```

Module sources are installed under `share/reflgen/modules`; consumer builds produce their own compiler/configuration
specific BMIs. Apply the same language standard to the module target when using C++23. In a normal `.vcxproj`, add the
module sources to a static-library project, configure its include directory and reference it from consumer projects.
Facades export the existing global-module declarations, preserving type/registry identity with header-based translation
units and allowing `reflection<T>`/`serializer<T>` specializations. See [tests/modules](tests/modules).
The header-only route remains available with the module build option off.
When mixing headers and imports in one translation unit, include headers containing reflgen library definitions before
`import reflgen...;`. MSVC reports redefinitions if the same library headers are included after importing it.

For IFC-generated schemas combined with the library modules, use the following order. It was verified in the VS 18.10
editor for build/run, JSON API completion and new-field completion after saving the provider module. Pre-including the
common support header prevents the generated schema header from redefining library entities after the imports:

```cpp
#include <iostream>
#include <string>
#include "reflgen/generated.h"
import reflgen.json;
import game.types;
#include "reflgen_game.h"
```

## MSVC C++20 module reflection from IFC

Optional: configure with `-DREFLGEN_BUILD_IFC=ON` to build `reflgen-ifc` (MSVC with C++23 support).
The provider and consumers remain C++20. Pinned Microsoft IFC SDK/GSL sources are fetched on first configure;
offline builds can set `FETCHCONTENT_SOURCE_DIR_REFLGEN_IFC_SDK` and `FETCHCONTENT_SOURCE_DIR_REFLGEN_IFC_GSL`.

The inheritance, friend, nested-type, overload and registration-dependency extensions below are included in 1.2.0.
Use matching tools and headers; the 1.1.0 binary does not include these extensions.

The frontend reads the primary module's compiler-produced IFC, without reparsing its source. Exported class/struct and
enum types are reflected by default, including public nested types. With metadata enabled, `ignore` excludes a type/member
and `reflect("name")` changes the schema name.

| Declaration | IFC behavior |
|---|---|
| Ordinary public fields and named methods | Reflected without an attribute or friend declaration |
| Public inheritance within this IFC | Adds the nearest reflected ancestors, including through unreflected public CRTP intermediaries |
| Private/protected members | Reflected when the class befriends the global `::reflgen::access`; otherwise default-exported types remain public-only |
| Explicit `reflect` with an inaccessible, non-ignored member | Fails with `RG0200` and asks for the friend declaration |
| Overloaded named methods | Uses an exact member-pointer cast, preserving parameter types, cv/ref qualifiers and supported `noexcept` specifications |
| Public nested class/struct and enum types | Generates schemas under their complete enclosing type names |

Declare the friend against the library's global entity, for example with `#include "reflgen/core/hook.h"` in the global
module fragment, followed by `friend struct ::reflgen::access;` inside the class. A same-named class declared inside the
named module does not grant access to the library. Imported IFC references are not resolved by this single-IFC reader;
this also limits friends, bases and overloaded method signature types obtained only from another named module.

Non-exported types are not registered. Non-reflected private/protected helper bases are excluded; inaccessible reflected
bases and virtual or pack-expanded public base paths are rejected rather than producing an incomplete schema.
Public bit-fields and reference fields still require `ignore`. Standalone class templates, member templates, operators, conversion functions,
static data fields and partitions/reexports remain outside this backend. Unsupported base/type/signature forms fail with
`RG0200`. Runtime invocation retains its existing qualifier restrictions; `find_method(name)` returns the first match,
so use the entries in `methods()` when selecting between overloads.

```cmake
# game must produce this IFC path. Keep the provider independent of its reflection target.
reflgen_generate_ifc(game_reflection
    PROVIDER game IFC "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/game.ifc"
    IMPORT game.types MODULE game)
target_link_libraries(app PRIVATE game_reflection)
```

For attributes, add `SOURCE path/to/types.ixx`. The helper preprocesses it with MSVC `/P` after the provider compiles,
using the provider's target definitions, include directories, options, runtime and configuration flags. Supply per-source
flags with `PREPROCESS_OPTIONS`, or supply your own compiler-produced sidecar with `METADATA path/to/types.i` instead.
Do not pass raw source or `/EP` output. The sidecar must be produced after its IFC with the same compiler settings.
IFC supplies types and visibility; `/P` supplies expanded attributes with original `#line` locations. Without `SOURCE` or
`METADATA`, the IFC-only mode still has no attributes.

```cmake
reflgen_generate_ifc(game_reflection
    PROVIDER game IFC "${game_ifc}" IMPORT game.types MODULE game
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/types.ixx"
    ATTRIBUTE_SCOPES editor
    ATTRIBUTE_MODULES "editor.attributes=${editor_attributes_ifc}")
```

All built-in metadata (`serialized_name`, `transient`, `required`, `range`, `display_name`, `description`, `category`,
`hidden`, `readonly`) is carried into the ordinary schema/runtime/serialization APIs. Custom constexpr attribute types
can be exported by another module: list its namespace in `ATTRIBUTE_SCOPES` and its import-to-IFC mapping in
`ATTRIBUTE_MODULES`. The generated header imports it explicitly, including when the provider only imports it privately.
Attribute argument names outside class static constants should be fully qualified; helper types/constants must be exported.
Ambiguous source-line matches fail with `RG0200`; put repeated declaration names on separate lines. Unsupported public
bit-fields/reference fields can be excluded with `ignore` before validation.

The registration translation unit must see the same custom serializers as the consumer. Supply registration-only headers
and imports explicitly; these are made visible before `registry::add<T>()` instantiates the descriptors:

```cmake
reflgen_generate_ifc(game_reflection
    PROVIDER game IFC "${game_ifc}" IMPORT game.types MODULE game
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/types.ixx"
    REGISTRATION_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/registration_serializers.h"
    REGISTRATION_MODULES "game.serialization=${serialization_ifc}")
```

`REGISTRATION_MODULES` supplies both the import name and MSVC `/reference` mapping. Use `REGISTRATION_IMPORTS` instead
when the consuming target already supplies that mapping. These options control registration visibility; they do not make
the IFC reader traverse dependency modules. At the CLI level, repeat `--registration-header` and `--registration-import`.
The generated source includes library headers, imports the provider and registration modules, includes the generated
schema and registration headers, then defines the registration functions. Do not include module-owned type declarations
again in a registration header.

Consumers first include their library/standard headers (for example `reflgen/json.h`), then write `import game.types;`
directly in their `.cpp`, then `#include "reflgen_game.h"` for schemas and registration
declarations. In a live VS 18.10 test, relying on the import inside the header alone did not activate member completion;
the direct import enabled it. Generated registration sources also import the provider directly.
Call `reflgen::generated::register_game(registry)` once at startup. Use that same generated header in every
translation unit that reflects these types. No forward declarations of module-owned types or forced includes are used.
The build order is provider → IFC extraction → separate reflection target → consumers.

For MSBuild, import `msbuild/reflgen.ifc.targets` after `Microsoft.Cpp.targets` in a separate reflection project. Add a
`ProjectReference` to the provider and one `ReflgenIfc` item whose `Include` is its IFC path and `Module` metadata is the
C++ import name. Set `ReflgenIfcExecutable`, `ReflgenModule`, and `ReflgenIfcSourceProject` (provider project path).
Other consumer projects need the generated include directory and the module reference mapping too.

For MSBuild attributes, import `msbuild/reflgen.ifc.metadata.targets` in the **provider**, after `Microsoft.Cpp.targets`.
Set `ModuleOutputFile` on each primary module source. This writes `<ModuleOutputFile>.i` using the source's compiler
settings, preserving unchanged timestamps. In the reflection project, set `Metadata` on `ReflgenIfc` to that path.
Use `ReflgenAttributeScope` items for custom namespaces and `ReflgenAttributeModule` items (`Include` = IFC path,
`Module` = import name). Attribute-only saves follow the same provider-build/generation/rescan route as member changes.
Module completion includes built-ins and custom attributes used in the preprocessed module; custom entries show `(...)`,
not constructor parameter documentation. See [the module attribute fixture](tests/ifc/attributes).

Both the mixed-project and standalone IFC targets forward the semicolon-separated `ReflgenRegistrationHeaders` and
`ReflgenRegistrationImports` properties. `ReflgenRegistrationModule` items (`Include` = IFC
path, `Module` = import name) also supply the compiler reference mapping. Add the corresponding provider project/build
dependencies as usual. The [engine-style fixture](tests/ifc/engine) exercises inheritance, private state, overloads,
nested types and custom serialization in CMake and MSBuild.

The VS extension routes `.ixx`/`.cppm`/`.mpp` saves to loaded reflection projects declaring that provider. Its existing
queue invokes `ReflgenGenerate`, which builds the provider first, reports diagnostics and refreshes IntelliSense after
schema changes. It does not insert friends into module sources. Provider compilation and IFC extraction/validation failures
preserve the previous successful output.

See [tests/ifc](tests/ifc) for executable CMake/MSBuild examples. Run
`ctest -R "^reflgen_ifc_" --output-on-failure --no-tests=error` after building. CI runs the IFC tests with MSVC in both
Debug and Release, including generation, compilation, runtime/serialization checks and rejection diagnostics.
The reader validates IFC version, integrity and import identity. Only the optional MSVC codegen traits
`evaluated-initializer` and `class-template-inheriting-ctors` are ignored; unknown semantic partitions are rejected.

## Quick start

```cpp
#include "reflgen/reflgen.h"
#include "reflgen/json.h"

struct item
{
    std::string name;
    int count = 1;

    static consteval auto reflect()
    {
        return reflgen::schema<item>(
            reflgen::field<&item::name>,
            reflgen::field<&item::count>.with(reflgen::range(1, 99)));
    }
};

std::string text = reflgen::json::to_string(item{ "potion", 3 }, 4);
item loaded = reflgen::json::from_string<item>(text);
```

The full example is [examples/quick_start.cpp](examples/quick_start.cpp). Writing `reflect()` by hand is optional —
the [code generator](#code-generator-reflgen) produces descriptions from attributes.

## Describing types

`reflgen::schema_of<T>` is the single entry point for descriptions, and there are two ways to supply one.

```cpp
// (1) In-class recipe — private fields work too (with friend struct reflgen::access; reflect() may be private)
class player
{
    friend struct reflgen::access;
    int hp_ = 100;

    static consteval auto reflect()
    {
        return reflgen::schema<player>(
            reflgen::base<entity>,                                     // direct base (multiple inheritance works)
            reflgen::field<&player::hp_>.named("hp")                   // rename
                .with(reflgen::range(0, 100), tooltip("health")),      // several attributes
            reflgen::method<&player::fire>.parameters("shots"))
            .named("game.player")                                      // registry key and polymorphic tag
            .with(reflgen::display_name("Player"));                    // type attribute
    }
};

// (2) External specialization — for types you cannot edit, and where the code generator's output goes
template<>
struct reflgen::reflection<vendor::config>
{
    static constexpr auto value = reflgen::schema<vendor::config>(
        reflgen::field<&vendor::config::port>, reflgen::field<&vendor::config::host>);
};
```

A recipe is **local** — it lists its own fields and direct bases only. Queries compute the walk that includes the
bases' fields (`reflgen::for_each_field<T>(f)`, `reflgen::for_each_field(object, f)`, bases first).

### Attributes

An attribute is a **constructor call expression**. A user-defined attribute is just a struct with a `constexpr`
constructor. Store strings in `reflgen::static_string` — it reads like `std::string_view` but is a structural type,
so it can become a C++26 annotation value ([C++26 migration](#c26-migration)). Storing a `std::string_view` works in
C++20/23.

```cpp
struct tooltip
{
    reflgen::static_string text;
    constexpr explicit tooltip(std::string_view value) : text(value) {}
};
```

| Built-in attribute | Meaning |
|---|---|
| `display_name("…")`, `description("…")` | Shown by editors |
| `category("…")` | A group an editor shows together (a collapsible inspector section, for example) |
| `range(min, max)` | Value range (for validators and editors; serialization does not check it) |
| `serialized_name("…")` | A serialization key different from the member name |
| `transient()` | Not serialized |
| `required()` | Must be present in deserialization input |
| `hidden()`, `readonly()` | Editor hints |

The `.value` of a string attribute is a `reflgen::static_string` — it converts implicitly to `std::string_view` and
supports comparison, `data()`, `size()`, `begin()` and `end()`.

Queries: at compile time `field.has_attribute<A>()` / `field.attribute<A>()`, at run time
`field_info::attributes().find<A>()`.

This notation carries over to C++26 unchanged — in C++20/23 the code generator copies the argument tokens of
`[[reflgen::range(0, 1)]]` to build the expression above, and in C++26 the same expression becomes the value of the
annotation `[[=reflgen::range(0, 1)]]`.

### Enumerations

`reflgen::enum_entries<E>`, `enum_name(e)`, `enum_cast<E>("name")`. By default the values in [-128, 128] are
scanned; widen that by specializing `enum_range<E>`, or give the exact table with `reflection<E>` (bit flags, for
example).

## Code generator (reflgen)

Instead of writing `reflect()` by hand, put attributes on declarations and the generator produces the descriptions
at build time. There are no macros — `[[…]]` is standard attribute syntax, and compilers ignore attributes they do
not know (`reflgen_generate()` turns that warning off).

**Your code carries no trace of it.** Headers do not include generated files (no `.generated.h`, no
`GENERATED_BODY()`), and nothing has to be registered in the project. Generated files live only in the build's
intermediate directory, like `.obj` and `.pch`, and the build force-includes (`/FI`, `-include`) the injection header
that gathers them (`reflgen_<module>.h`) into every translation unit.

```cpp
// player.h
#pragma once
#include "reflgen/reflgen.h"

namespace game
{
    enum class [[reflgen::reflect]] element
    {
      fire,
      water,
      wind = 1 << 10
    }; // exact table, even outside the scan range

    class [[reflgen::reflect("game.player")]] player : public entity               // argument = registry key and polymorphic tag
    {
        friend struct reflgen::access;                                             // needed to reflect private members

      public:
        static constexpr int max_level = 99;
        static constexpr float max_hp = 999.0f;

        [[reflgen::range(1, max_level)]]
        int level = 1;              // class member names are used

        [[reflgen::range(0.0f, max_hp), game::tooltip("HP")]]
        float hp = 100.0f;          // unqualified, as written

        [[reflgen::ignore]]
        int cache = 0;              // left out of reflection

        [[reflgen::transient]]
        int frame = 0;              // reflected but not saved

        [[reflgen::reflect]]
        void level_up(int amount);  // only marked methods

      private:
        int secret_ = 7;            // reflected without a mark
    };
} // namespace game
```

```cmake
reflgen_generate(my_game
    HEADERS include/game/player.h include/game/item.h
    MODULE game                       # registration function: reflgen::generated::register_game()
    ATTRIBUTE_SCOPES game             # attribute namespaces to carry over besides reflgen
    ATTRIBUTE_HEADERS include/game/attributes.h) # where those attribute types (game::tooltip) are defined
```

- **Codebases that only use descriptions**: the registration function (`reflgen_<module>.cpp`) builds a runtime
  descriptor (field table, write and read thunks) for each reflected type. An engine with its own serialization and
  registry that only uses descriptions (`schema_of`, `for_each_field`) can skip compiling the registration function
  with `NO_REGISTRATION` (CMake) or `ReflgenRegistration=false` (MSBuild) — it does not pay the compile time of
  descriptors it will not use. Generation and injection stay the same.

- **Headers the registration function must see**: registry descriptors are built in the registration function's
  translation unit, and that translation unit sees only the module's reflected headers. Hand it what it is missing,
  so the descriptions match the other translation units, with `REGISTRATION_HEADERS` (CMake) or
  `ReflgenRegistrationHeaders` (MSBuild) — the registration function includes them first.
  - `reflgen::serializer` specializations placed where the reflected type's header does not include them. Without
    them, those fields stay in the registry descriptor without a serializer.
  - The headers of described field types that the reflected type's header only forward-declares
    (`std::shared_ptr<Material>` and the like). Without them, compilation stops with `reflgen: 'Material' is
    described in …/Material.h but is an incomplete type where its description is used …` — the same message appears
    wherever a described type is used in a translation unit where it is incomplete (a `static_assert` in the
    generated `reflection<T>`).

  Descriptions from other modules (their injection headers) are seen automatically — in CMake through the forced
  includes of linked targets, in MSBuild through `reflgen_<module>.references.h`, which gathers the injection headers
  of the referenced reflgen projects. The registration function of a module without reflected classes does not
  include the registration headers (the same setting can be given to every project).

- **What is reflected**: the generator includes every non-static data member of a `[[reflgen::reflect]]` class.
  Leave one out with `[[reflgen::ignore]]`. Methods only when marked `[[reflgen::reflect]]`.
  Reflected `public` bases become `base<>`; bases that are not reflected (a CRTP middle layer, a third-party base) are
  skipped over to the nearest reflected ancestor above them.
- **Carrying attributes over**: only the `reflgen::` namespace and the `ATTRIBUTE_SCOPES` namespaces go into the
  schema. An attribute without arguments (`[[reflgen::transient]]`) is treated as a tag type and gets `{}`. An
  attribute at the front of a declaration applies to every member it declares; one after a name
  (`int x [[reflgen::ignore]], y;`) applies to that member only.
- **Names in arguments** resolve as if written inside the class. The generated code lives outside the class, so the
  generator qualifies members of the class (and its bases) as in `T::max_hp`. Other names, such as namespace
  constants, work too, but then that header's generated file includes the source header (see **Cost** below).
  Comments inside arguments are not carried over.
- **Error locations**: a typo in an attribute argument is reported at that line of the source header, not in the
  generated file (the generated code points back with `#line`).
- **Registration**: types that are read and written through polymorphic pointers need
  `reflgen::generated::register_<module>()` called once at startup. It is declared in the injection header, so there
  is nothing to include.
- **Build integration**: generated files are kept per configuration in `<OUTPUT_DIRECTORY>/<config>/`. Generation
  reruns when a file included by `HEADERS` changes (depfile). The parsing standard is the higher of `CXX_STANDARD` and
  the `cxx_std_NN` compile features. The injection (forced include) goes to the target and to targets that link it,
  as `PUBLIC` `BUILD_INTERFACE` — headers do not include generated files, so reflection reaches consumers only through
  the forced include. It does not reach consumers of an installed library. CMake 3.25 or later.
- **Cost (light injection)**: the injection header only **forward-declares** the reflected types, and a description
  is instantiated where the type is used (where it is complete). Source headers are not dragged into every
  translation unit, so editing one header recompiles only the translation units that include it, and macros from
  source headers (`windows.h` and the like) do not leak. Headers that cannot be handled with forward declarations —
  types nested in classes, arguments that use namespace constants, bases that are template specializations, unscoped
  enumerations without a fixed underlying type, user attributes used without `ATTRIBUTE_HEADERS` — make the generated
  file include the source header, and a comment says why. Only the registration function (`reflgen_<module>.cpp`)
  includes all source headers.
- **C sources**: C translation units in the same target (third-party C code) do not get the injection.
- **Requirements**: libclang. With Visual Studio it finds the bundled copy (`VC/Tools/Llvm`) automatically; otherwise
  set `REFLGEN_LIBCLANG_DIR`. The C API headers are in `third_party/clang-c` (LLVM 22.1.3, Apache-2.0 WITH
  LLVM-exception). The clang builtin headers of the same version as libclang (`immintrin.h`, `stddef.h` and so on) sit
  next to the generator in `clang/include` — the generator passes them with `-resource-dir`, and they are found
  before the MSVC and Windows SDK includes the build system gives (the clang-cl order).
- **Code that only builds with MSVC**: the generator reads code with clang, so MSVC-only code (a `static_assert` that
  pins `__FUNCSIG__` text, an enum cast clang does not accept as a constant expression) produces clang errors. Errors
  outside reflected declarations are skipped and only counted (`note RG0101`) — the generated code copies only the
  names and attributes of reflected declarations, and your own compiler does the compiling. An error inside a
  reflected declaration (RG0100), or a reflected declaration clang considers invalid because of an error outside it
  (a base class, for example — RG0102), is a failure — clang may have misread that declaration. On failure the skipped
  errors are reported too, with their locations. The expression nesting limit (`-fbracket-depth`), which MSVC does not
  have and whose default differs between libclang versions (256 in 20, 2048 in 22), is set to 4096 regardless of the
  version.

Diagnostics use the MSVC format (`file(line,col): error RG0002: …`), so the VS Error List jumps straight to the
source.
The generator also writes an attribute catalog for editor completion (`reflgen_<module>.attributes.tsv` — the types
in the attribute namespaces, their constructor signatures and the comments in front of them). Data types marked
`[[reflgen::reflect]]` are left out. If an `ATTRIBUTE_SCOPES` namespace mixes in types that are not attributes, mark
the attribute types `[[reflgen::attribute]]` — once any type is marked, only marked types are listed for that
namespace.

```cpp
namespace editor
{
    struct [[reflgen::attribute]] tooltip
    {
        std::string_view text;

        constexpr explicit tooltip(std::string_view value) : text(value) {}
    };
} // namespace editor
```

| Code | Meaning |
|---|---|
| RG0001 | Input or option error (a missing header, the same header twice, …) |
| RG0002 | A private member is reflected but `friend struct reflgen::access;` is missing |
| RG0004 | A member that cannot be reflected (bit-field, reference, anonymous union, static member, constructor or destructor) |
| RG0005 | An unsupported declaration (class or member function template, union, anonymous namespace) |
| RG0006 | An overloaded method |
| RG0007 | Generated file name collision (two headers with the same name) |
| RG0100 | A compile error reported by clang — inside a reflected declaration, or parsing stopped |
| RG0101 | (note) A clang error skipped because it is outside reflected declarations |
| RG0102 | A reflected declaration clang considers invalid because of an error outside it (base class, member type) |

### Optional declaration manifest

`--declarations-json FILE` adds one UTF-8 JSON declaration manifest per module for downstream generators and other
tools. It is opt-in and does not change reflection generation, injection or registration. Parent directories are
created as needed; unchanged content keeps its timestamp.

```sh
reflgen --module game --output build/reflgen --declarations-json build/game.declarations.json include/game/player.h -- -std=c++20 -Iinclude
```

The root identifies `format: "reflgen.declarations"` and `version: 1`, with `module`, `target` (`triple`,
`pointer_width`) and `headers`. Each header contains its `path`, reflected `classes` and `enums`. Declarations carry
names, source locations and attributes; classes include fields and reflected methods. Methods carry their owner,
access, signature, qualifiers, calling convention, exception specification, return type and parameters. Type records
include source and canonical spellings, qualifiers and recursive pointer/reference/array shape. Attributes retain
their scope, name, argument tokens, qualified expression and source location.

This is declaration metadata, not an ABI or exportability guarantee. Consumers must check the format/version and
apply their own selection, supported-type and marshaling rules. The existing reflection eligibility and diagnostics
still apply: this option does not enable otherwise unsupported declarations, overloads or templates.
The complete field and compatibility contract is in [DECLARATIONS.md](DECLARATIONS.md).

In CMake, add `DECLARATIONS_JSON FILE` to `reflgen_generate()`:

```cmake
reflgen_generate(my_game
    HEADERS include/game/player.h
    MODULE game
    DECLARATIONS_JSON game.declarations.json)
get_target_property(game_declarations my_game REFLGEN_DECLARATIONS_JSON)
```

Relative paths resolve inside `<OUTPUT_DIRECTORY>/<config>/`; absolute paths must stay inside that same directory.
Multi-config builds require `$<CONFIG>` in the resolved path to prevent configurations overwriting each other; this
is the only generator expression supported in optional output paths. `REFLGEN_DECLARATIONS_JSON` holds the resolved
absolute path (possibly containing `$<CONFIG>`) for a downstream command's arguments and `DEPENDS`. The JSON is a
tracked generated output, is regenerated if deleted, participates in
clean, and is never compiled. Omit the option to disable it; it also works with `NO_REGISTRATION`.

For MSBuild, set `ReflgenDeclarationsJson` to the desired file, preferably under the configuration-specific `IntDir`:

```xml
<PropertyGroup>
  <ReflgenDeclarationsJson>$(IntDir)reflgen\$(ProjectName).declarations.json</ReflgenDeclarationsJson>
</PropertyGroup>
```

An unset/empty property disables the manifest. Relative paths are project-relative, and the resolved path must stay
under `ReflgenOutputDirectory`. The path is passed through the response file, a missing manifest triggers generation
when MSBuild runs, and unchanged JSON does not force repeated generation. The same property works with NuGet.

Both integrations reject collisions with inputs and reserved outputs, and refuse existing optional files unless a
previous successful generation recorded ownership. Choose a new generated path, or remove an existing file only
after verifying it is stale generated output. Clean removes owned optional outputs even when generation was skipped.
The standalone CLI can still write a safe path outside the build output directory. Visual Studio's native fast
up-to-date check may skip MSBuild after an output-only deletion; invoke `ReflgenGenerate` explicitly in that case.

### Optional C ABI, C# and F# bindings

`--interop` adds `reflgen_<module>_interop.h`, `.cpp`, `.cs` and `.fs` under the normal output directory.
`--interop-library NAME` sets the native library name used by managed bindings (default: module name), and
`--interop-namespace NAME` sets its namespace (default: `Reflgen.Generated`).

CMake accepts `INTEROP`, `INTEROP_LIBRARY NAME` and `INTEROP_NAMESPACE NAME` in `reflgen_generate()`.
The `REFLGEN_INTEROP_CSHARP` and `REFLGEN_INTEROP_FSHARP` target properties expose the generated managed paths. MSBuild/NuGet use
`ReflgenInterop=true`, `ReflgenInteropLibrary` and `ReflgenInteropNamespace`. The generated C++ implementation is
compiled even with `NO_REGISTRATION` / `ReflgenRegistration=false`; the managed files are never passed to the C++ compiler.
Bindings are opt-in, independent of the JSON manifest, and use the same output ownership and cleanup checks.
See [INTEROP.md](INTEROP.md) for explicit selection attributes, the supported boundary types and lifetime rules.

### MSBuild (.vcxproj) integration

Import it at the end of the project (after `Microsoft.Cpp.targets`) or from `Directory.Build.targets`. That is all —
no header has to be registered or marked.

```xml
<Import Project="path\to\reflgen\msbuild\reflgen.targets" />
<PropertyGroup>
  <ReflgenExecutable>path\to\reflgen.exe</ReflgenExecutable>  <!-- omit with an install layout (<prefix>\bin) -->
  <ReflgenAttributeScopes>editor</ReflgenAttributeScopes>
  <ReflgenAttributeHeaders>include\editor\attributes.h</ReflgenAttributeHeaders>
</PropertyGroup>
```

- **Discovery**: every header of the project (`ClInclude`) is a candidate, and the generator parses only those with
  `[[reflgen::reflect]]` (`--discover`). Old generated files of headers that dropped reflection are deleted.
- **Injection**: before compiling, the `ReflgenGenerate` target generates into `$(IntDir)reflgen\` (per configuration)
  and force-includes `reflgen_<module>.h` into every `ClCompile`. Files that use a precompiled header (`/Yu`, `/Yc`)
  force-include the pch header first (`/Yu` skips whatever comes before the pch). IntelliSense (design-time builds)
  sees the same forced include. The generated registration function (`reflgen_<module>.cpp`) is added to the compile
  list as well (without a precompiled header).
- Parsing uses the project's ClCompile settings as they are — include paths, preprocessor definitions,
  `LanguageStandard`, system includes.
- It reruns only when a project header, a file those headers include (the list read by the previous run), the
  generator, or a setting changes.
- **Across projects**: the injection headers of reflgen projects referenced with `ProjectReference` are
  force-included too — a game executable can serialize types from an engine library. Referenced projects come first
  and the project's own comes last, so a type whose base lives in another project compiles. Indirect references are
  followed and duplicates are included once. The referenced project must import `reflgen.targets` as well
  (references that do not use reflgen are skipped).
- **Projects that include headers without a reference**: a translation unit that includes another project's headers
  must receive that project's injection too — otherwise the same type looks different in different translation units
  (`reflectable<T>` is true on one side only). Libraries tied only by include paths, without `ProjectReference`, and
  libraries that include each other's headers (cycles) are tied with `ReflgenReference`. It does not impose a build
  order: it runs that project's generation first and then receives that project's own injection header (it is not
  transitive). The configuration is chosen with the same rules as `ProjectReference`, so generation runs once per
  project in parallel solution builds. Linking is set up separately.

  ```xml
  <ItemGroup>
    <ReflgenReference Include="..\Render\Render.vcxproj" />
  </ItemGroup>
  ```
- Warning `C5030` (unknown attribute) is turned off, and the ClangCL toolset gets `-Wno-unknown-attributes`.

| Property | Default |
|---|---|
| `ReflgenExecutable` | `<prefix>\bin\reflgen.exe` of an install layout |
| `ReflgenIncludeDirectory` | `include` of the repository or the install layout |
| `ReflgenModule` | The project name (made an identifier) — `reflgen::generated::register_<module>()` |
| `ReflgenAttributeScopes` | None (`reflgen` only) |
| `ReflgenAttributeHeaders` | None — headers that define user attribute types. The injection header includes them |
| `ReflgenOutputDirectory` | `$(IntDir)reflgen\` |
| `ReflgenDeclarationsJson` | None — optional declaration manifest path, relative to the project or absolute (above) |
| `ReflgenInterop` | `false` — opt into C ABI / C# bindings and compile the generated C++ implementation |
| `ReflgenInteropLibrary` | Module name — native library name used by C# |
| `ReflgenInteropNamespace` | `Reflgen.Generated` — generated C# namespace |
| `ReflgenRegistration` | `true` — `false` skips compiling the registration function (codebases that only use descriptions, above) |
| `ReflgenRegistrationHeaders` | Headers the registration function includes before building descriptors (`;`-separated) — user serializer specializations and the like (above) |

### Visual Studio extension

MSVC mixed projects use both paths through the single `reflgen.targets` import above. `ClInclude` headers use libclang;
public types in `.ixx`, `.cppm`, `.mpp`, or `CompileAsCppModule` sources use IFC. Set `ReflgenIfcExecutable` to
`reflgen-ifc.exe` (by default beside `reflgen.exe`). Module providers and their `/P` metadata are built before extraction;
header injection is excluded from module sources. For `ReflgenModule=mixed` and `types.ixx`, the module schema is
`reflgen_mixed_types.h` and registration is `reflgen::generated::register_mixed_types`. Include that schema after the
consumer's import and call it alongside the header path's `register_mixed`. Normalized module source filenames must be
unique. Automatic routing currently applies to MSVC `.vcxproj` projects; CMake uses the separate `reflgen_generate`
and `reflgen_generate_ifc` helpers. The IFC public-type and attribute restrictions described above still apply.

VS 2026 (18.x), x64 — the version it is tested with. Run `Reflgen.VisualStudio.vsix` (a release asset, or build
`vs/`) to install it.

```powershell
& "<VS>\MSBuild\Current\Bin\amd64\MSBuild.exe" vs\src\Reflgen.VisualStudio\Reflgen.VisualStudio.csproj /restore /p:Configuration=Release
dotnet test vs\tests\Reflgen.VisualStudio.Core.Tests
```

It works in .vcxproj projects that import `reflgen.targets`. Generated output goes into neither headers nor project
files.

- **Inserting the friend** — when a class reflects members that are not public (non-static data members,
  `[[reflgen::reflect]]` methods, static members used in attribute arguments) and lacks
  `friend struct reflgen::access;`, it is inserted at the top of the class body with member indentation on save (it
  becomes part of what is saved). The generated code lives outside the class, so it needs this. That line is the only
  thing added to headers.
- **Generate on save** — saving a header that has (or dropped) `[[reflgen::reflect]]` runs only that project's
  `ReflgenGenerate` in a separate MSBuild process and puts the RG diagnostics in the Error List. It is skipped while a
  VS build is running (that build generates). Progress goes to the "reflgen" Output pane (selected once, on the
  first log after VS starts).
  Module saves in mixed projects use the same entry point. Each generation stage tracks its inputs separately: an
  independent header edit updates header reflection, and a module edit updates IFC reflection. Shared dependencies can
  invalidate both paths.
- **Generate on first open** — a project that has never generated does so when opened. Otherwise IntelliSense sees
  an empty injection header and underlines code that uses reflection.
- **IntelliSense refresh** — IntelliSense does not notice changes to forced-include files outside the project
  (`$(IntDir)`). When generated code changes, the extension calls "Rescan Solution" (Project > Rescan Solution). This
  can be heavy in large solutions, so it can be turned off.
- **Attribute completion** — after `[[` or `,` it suggests standard C++ attributes alongside `reflgen::` and custom
  namespaces; after `reflgen::` it suggests
  attributes with their constructor signatures and descriptions. The list comes from the generator's catalog (see
  `[[reflgen::attribute]]` above); before the first generation only the built-in attributes appear.
- Settings: Tools > Options > reflgen > General (friend insertion, generation, IntelliSense refresh, MSBuild.exe path).

| Code | Meaning |
|---|---|
| RG0901 | A header declares reflection but the project does not import `reflgen.targets` |
| RG0902 | The MSBuild run for generation failed in a way that could not be parsed (full text in the Output pane) |

## Serialization

```cpp
reflgen::serialize(writer, value);              // the writer implementation decides the format
reflgen::deserialize(reader, value);            // read in place
auto value = reflgen::deserialize<T>(reader);

reflgen::json::to_string(value, indent) / reflgen::json::from_string<T>(text)
reflgen::binary::to_bytes(value)        / reflgen::binary::from_bytes<T>(bytes)
```

Reading semantics assume that file formats outlive code.

- A field missing from the input **keeps its current value** (a field marked `required` fails).
- Unknown keys are **skipped**.
- Failures are `reflgen::serialization_error`, and `path()` holds the failure point as a JSON Pointer
  (`"/inventory/1/count"`). The input position (line and column, or byte offset) is in the message.
- Integers are range-checked against the target type — 300 is not silently truncated into a `uint8_t`.
- Cyclic references (`shared_ptr` A→B→A, a `reference_wrapper` that points to itself) stop with
  `serialization_error("cyclic reference")` when written. Sharing — the same object reached by two paths — is not a
  cycle, and each path writes it as a value.

### Supported types

| Category | Types | Shape |
|---|---|---|
| Scalars | `bool`, integers of every width, floating point, `std::byte`, `nullptr_t`, `monostate` | value |
| Characters | `char`, `wchar_t`, `char8/16/32_t` | one-character string |
| Enumerations | every enumeration | name (integer if there is none) |
| Strings | `basic_string` of every character type, character arrays | UTF-8 string |
| Write only | `string_view`, C strings, range views | |
| Sequences | `vector`, `deque`, `list`, `forward_list`, `(unordered_)(multi)set`, `valarray` | array |
| Fixed size | `array`, C arrays, `span` | array (element count must match) |
| Maps | unique keys with string, integer or enumeration keys | object `{"3": …}` |
| Maps | multimap, struct keys | `[[k, v], …]` |
| Adapters | `stack`, `queue`, `priority_queue` | array in the underlying container's order |
| Byte strings | contiguous ranges of `std::byte` | base64 (JSON) / raw (binary) |
| Wrappers | `optional`, `atomic`, `reference_wrapper` | null or value |
| Pointers | `unique_ptr`, `shared_ptr` — the pointee must be complete where the description is used (otherwise compilation stops, saying why and where to fix it) | null or value, `{"type","value"}` if polymorphic |
| Sums and products | `variant` → `[index, value]`, `pair`/`tuple`/tuple-like → array, `complex` → `[re, im]` | |
| Other | `bitset` → `"0101"`, `chrono` → tick count, `filesystem::path` → generic UTF-8 | |
| C++23 | `std::expected` (with `reflgen/serial/expected.h`) → `{"value"}`/`{"error"}` | |
| C++23 | `flat_map`, `flat_set` — recognized by shape | |

### User containers

Anything shaped like the STL (`begin/end` + `clear` + one of `emplace_back`, `push_back`, `insert`, `insert_after`;
maps also `key_type/mapped_type`) works **automatically, without a specialization**. For a different interface,
specialize `container_traits`:

```cpp
template<class T>
struct reflgen::container_traits<ring_buffer<T>>
{
    using value_type = T;
    static constexpr reflgen::container_kind kind = reflgen::container_kind::sequence;
    static std::size_t size(const ring_buffer<T>& c);
    template<class F> static void for_each(const ring_buffer<T>& c, F&& f);
    static void clear(ring_buffer<T>& c);
    static void add(ring_buffer<T>& c, T&& item);      // or assign(c, std::vector<T>&&)
    static void reserve(ring_buffer<T>& c, std::size_t n);   // optional
};
```

The map contract and details are in [container_traits.h](include/reflgen/serial/container_traits.h). For a type
whose shape itself differs (a color as `"#rrggbb"`, say), specialize `reflgen::serializer<T>` (two functions, `write`
and `read`).

To change only the envelope of a reflected type (header, version, hooks) and leave the fields to reflgen, use the
object body functions:

```cpp
template<>
struct reflgen::serializer<save_file>
{
    static void write(reflgen::writer& out, const save_file& value)
    {
        out.begin_object(1 + reflgen::serialized_field_count<save_file>());
        out.write_key("version");
        out.write_int(2);
        reflgen::write_fields(out, value);                     // a key and a value per field (bases first)
        out.end_object();
    }

    static void read(reflgen::reader& in, save_file& value)
    {
        in.begin_object();
        for (std::string key; in.next_key(key);)
        {
            if (key == "version") { in.read_int(); }
            else if (!reflgen::read_field(in, value, key)) { in.skip_value(); } // keys that are not fields are not consumed
        }
        in.end_object();
    }
};
```

## New format backends

Implement the pure virtual functions of `reflgen::writer` / `reflgen::reader`. The data model is the JSON shape plus
signed and unsigned integers and byte strings.

```
writer: write_null/bool/int/uint/float/string/bytes, begin_array(n)/end_array, begin_object(n)/write_key/end_object
        (optional) write_float32 — float values. Forwards to write_float by default
        (optional) prefer_inline — a layout hint that the next container may be written on one line. Ignored by default
reader: peek, read_*, begin_array → while(next_element) … → end_array,
        begin_object → while(next_key(key)) … → end_object, skip_value
        (optional) read_float32 — float values. Narrows read_float to float by default
```

- The size given to `begin_*` is exact when writing and a hint (for reserve) when reading. Sizes from untrusted input
  are passed on after being bounded by the remaining input length.
- Text formats override `write_float32` and `read_float32` to write a float in the shortest float form and read it
  straight into a float (the JSON backend does — `0.1f` is `0.1`, and reading does not round twice through double).
  User serializers call `prefer_inline` (small values such as vectors and colors as YAML flow or one-line JSON).
- Keep a nesting depth limit. The JSON and binary backends default to 512 for both reader and writer, changed through
  constructor arguments (`to_string(value, indent, max_depth)` and so on). The writer limit makes a very deep
  structure that is not a cycle end with an error instead of a stack overflow.

[json/](include/reflgen/json) and [binary/](include/reflgen/binary) are the reference implementations.

## Runtime descriptors and the registry

```cpp
const reflgen::type_descriptor& type = reflgen::type_descriptor_of<player>();
for (const reflgen::field_info& field : type.fields())   // inherited fields included, bases first
{
    field.name(); field.key(); field.type_name(); field.attributes().find<reflgen::range<int>>();
    void* address = field.address(&object);
    if (const reflgen::enum_descriptor* values = field.enumeration()) // the enumerator table of an enum field
    {
        values->write(address, values->find("angry")->value);      // values travel as long long
    }
}
for (const reflgen::method_info& method : type.methods()) // inherited methods included, bases first
{
    method.name(); method.parameters(); method.return_type(); method.is_const();
}
int amount = 5, total = 0;
void* arguments[] = {&amount};                            // each points to an object of the parameter type
type.find_method("heal")->invoke(&object, arguments, &total); // the result goes into total (nullptr discards it)
reflgen::register_type<fireball>();                      // a derived type read and written through polymorphic pointers
```

- Every table is `constexpr` — there is no static initialization order problem.
- Methods are called through type-erased pointers: each argument points to an object of the parameter type (with
  references and cv removed); a value parameter copies that object (or moves it when the type cannot be copied), a
  reference parameter receives it, and an rvalue reference parameter moves from it. A wrong argument count, or a
  result slot for a return type that cannot be assigned, is `std::invalid_argument`. Methods that cannot be called
  through argument slots (`&&` qualifier, `volatile`, C variadics) are not in `methods()` — they remain in the
  compile-time description (`schema_of`).
- Fields reflgen cannot write (a type without a serializer, or a reflected type that holds such a field) stay without
  write and read thunks — `field.is_serializable()` is false, and writing is a `serialization_error`. The check goes
  down into the fields of reflected types held inside (`reflgen::serializable<T>`). Writing the same type directly,
  as with `json::to_string`, is a compile error at the field that really cannot be written.
- Registration is an explicit call. Self-registering objects in static libraries can be discarded by the linker, so
  they are not used.
- Polymorphic serialization finds the dynamic type with RTTI. RTTI is used only for polymorphic types (the rest works
  in `-fno-rtti` builds).

## Building and testing

```powershell
./scripts/test.ps1                       # MSVC and clang-cl × C++20 and C++23, four combinations
./scripts/test.ps1 -Presets msvc-cpp20   # just one
```

CTest includes the generator's end-to-end, diagnostic and non-ASCII path tests, compile-failure tests for the
incomplete-type messages, and, when MSBuild is present, the `.vcxproj` integration test (generate, compile, run, and
check that building again does not rerun the generator). The VS extension's pure logic runs separately with
`dotnet test`. `./scripts/package.ps1` builds the zip and the NuGet package into `build\package`.

CMake consumers call `find_package(reflgen)` and link `reflgen::reflgen`. A `.vcxproj` imports
`<prefix>/share/reflgen/msbuild/reflgen.targets` from the install layout. The test harness is macro-free too, built
on `std::source_location` ([tests/harness.h](tests/harness.h)).

### Encoding

Comments are in Korean (UTF-8). Public headers carry a **UTF-8 BOM**, so Korean comments do not swallow code in MSVC
projects that build as CP949 without `/utf-8`. The CMake targets also propagate `/utf-8`.

## Coding conventions

Names follow the STL convention (snake_case, trailing `_` on members, PascalCase template parameters, constants in
snake_case too); braces, spacing and indentation follow CreatorEngine's [.clang-format](.clang-format). Comments are
written in Korean and say why, not what.

## C++26 migration

### Internal language and feature guards

`core/features.h` normalizes `_MSVC_LANG`/`__cplusplus` as `REFLGEN_CPLUSPLUS` and `REFLGEN_CXX20/23/26`.
`REFLGEN_HAS_IF_CONSTEVAL`, `REFLGEN_HAS_BYTESWAP` and `REFLGEN_HAS_STD_REFLECTION` test actual facilities.
The build's `REFLGEN_USE_STD_REFLECTION=0/1` is the final selection; forcing unavailable reflection is a compile
error. MSVC module TLS compatibility guards are centralized there too.

Binary floating-point I/O uses byte assembly in C++20 and fixed-width copies when C++23 byteswap is available.
Both retain the same eight little-endian payload bytes; no measured speedup is claimed. Compile named modules
and consumers with the same backend and language mode. See
[`tests/standard/check_matrix.cmake`](tests/standard/check_matrix.cmake) for reproducible version/backend checks.

### Standard backend selection

The optional standard backend builds schemas from `std::meta` and typed annotations. The runtime registry,
query, JSON and binary APIs stay the same. Existing header/libclang and module/IFC paths remain available.
`[[reflgen::…]]` is generator metadata; the standard backend reads `[[=reflgen::…]]`. AUTO selects a backend,
but does not rewrite source syntax. Supporting both in one source requires conditional declarations guarded by
`REFLGEN_USE_STD_REFLECTION`.

```cmake
set_property(TARGET game PROPERTY CXX_STANDARD 26)
reflgen_generate(game BACKEND AUTO HEADERS player.h TYPES game::player MODULE game)
# STD uses the provider's exported types without reading an IFC:
reflgen_generate_ifc(game_registration PROVIDER game_types IMPORT game.types
    BACKEND AUTO TYPES game::actor MODULE actors IFC "${actor_ifc}")
# Compile named library modules with the same backend as consumers:
reflgen_add_modules(reflgen_modules BACKEND STD)
```

`AUTO` probes reflection, annotations, member splicing and static strings when the target requests C++26 and
supplies `TYPES`. A failed probe keeps the legacy path. Calls without `TYPES` retain legacy behavior, including
existing C++26 projects. `STD` requires a successful probe; `LEGACY` explicitly opts out. The standard backend
generates only registration code: list class types in `TYPES`, since standard reflection cannot enumerate all
types in a namespace or module. Enums do not require registry entries. Use `BACKEND STD NO_REGISTRATION` for
schemas alone. Named library modules default to LEGACY; their backend must match consumers. The helper adds
GCC's `-freflection` after a successful probe.

For MSBuild, import `reflgen.targets` as before and add:

```xml
<PropertyGroup><ReflgenReflectionBackend>Auto</ReflgenReflectionBackend></PropertyGroup>
<ItemGroup>
  <ReflgenReflectType Include="game::player" />
  <!-- For a type exported by a module: -->
  <ReflgenStandardImport Include="game.types" />
</ItemGroup>
```

AUTO probes `stdcpplatest`/`stdcpp26` projects with a type manifest. `Std` fails with RG0261 when unsupported;
`Legacy` preserves generation. Results and `standard.probe.log` are cached beside generated files.
The VS extension reads the active configuration's C++ version: it filters standard attributes accordingly,
keeps `[[reflgen::…]]` completion, and handles `[[=reflgen::…]]` in C++26/latest mode. It inserts `{}` for
argument-free annotation values and `(` for values with arguments. Editor support does not imply that the
compiler implements reflection. Save uses the same backend-selecting MSBuild target.

The standard backend reflects public fields and explicitly annotated public methods, preserves typed user
annotations (including module-exported types), honors `ignore`, and obtains exact annotated enum entries.
Manual/external schemas take precedence. Non-ignored bit-fields, reference fields and public inheritance require
a manual schema; private members are excluded. Verified with GCC 16.2 in WSL, including `import reflgen.json`,
registration and serialization. Local MSVC 19.51 fails the probe and retains legacy generation.

| C++20/23 | C++26 |
|---|---|
| `[[reflgen::reflect]]` | `[[=reflgen::reflect{}]]` |
| `[[reflgen::reflect("game.player")]]` | `[[=reflgen::reflect("game.player")]]` |
| `[[reflgen::ignore]]` | `[[=reflgen::ignore{}]]` |
| `[[reflgen::range(0, 100)]]` | `[[=reflgen::range(0, 100)]]` |
| `[[reflgen::transient]]` | `[[=reflgen::transient{}]]` |
| `[[game::tooltip("HP")]]` | `[[=game::tooltip("HP")]]` |
| `[[using reflgen: transient, range(0, 1)]]` | `[[=reflgen::transient{}, =reflgen::range(0, 1)]]` |

Rule: with arguments, the expression is kept as written; without, `{}` is added — the same rule the generator uses
today when it carries attributes into schemas.

Already in place:
- The built-in attributes and the directive types (`reflect`, `ignore`, `attribute`, `core/directives.h`) are all
  structural types. The requirements on annotation values match those on C++20 class NTTPs, so
  `tests/annotation_readiness_tests.cpp` checks this at compile time by using them as template arguments.
- Strings are `static_string`. The C++26 backend makes them point to arrays built with `std::define_static_string`
  (a pointer in an annotation value cannot point to a string literal). Code that reads `.value` does not change.
- User-defined attributes must be structural types too in order to become annotation values — make the members public
  and put strings in `static_string`.
- Directive types are not attributes — `.with(reflgen::ignore{})` is a compile error (to leave a member out, do not
  list it in the schema).

Not yet available: a codemod that rewrites the notation (a VS extension command).

## Known limitations

- Compiler-generated type names differ between implementations for standard library types (MSVC writes default
  template arguments). Pin polymorphic tags that end up in files with `.named()`.
- `variant` is written by index — the order of the alternatives is part of the file format.
- `shared_ptr` sharing is not preserved (reading produces copies). Cycles are stopped with an error.
- Raw pointers and `weak_ptr` are not serialized (it cannot be known whether they own or refer). To write them as
  IDs or handles, specialize `reflgen::serializer<T*>`.
- `default_registry()` is one per module (DLL).
- The header generator does not handle class templates, member function templates, overloaded methods, enumerator
  attributes or unions (it reports them with diagnostics). The IFC backend supports typed method overloads and public
  CRTP ancestry within its supported type forms; see its separate restrictions above. Unnamed parameters keep an empty name.
- Reflecting private members requires `friend struct reflgen::access;` (the VS extension inserts it on save). The
  C++20 way of getting constant pointers to private members without a friend (injecting a friend function through
  the access-check exemption of explicit instantiation) works with Clang, but MSVC 19.51 cannot use that function in
  constant evaluation (C3779, C2131, measured). The standard backend uses unprivileged access and reflects public
  members only; use a manual schema for private state.
- The VS extension handles .vcxproj only. In CMake (Open Folder) projects, `reflgen_generate()` generates at build
  time. Completion appears only in the attribute name position — inside arguments it is up to C++ IntelliSense.
- Names in attribute arguments are looked up in the enclosing namespaces by opening every one of them with
  `using namespace`, from the outermost in, so a name that exists in both an outer and an inner namespace is
  ambiguous — qualify it in that case.
- Automatic name extraction works where `std::source_location` gives a signature that includes template arguments
  (MSVC STL, libstdc++). Elsewhere (libc++), `.named()` or the code generator is needed — check with
  `reflgen::name_extraction_supported`. GCC and libc++ are not yet verified in CI.
- Release packages (generator binaries) are Windows x64 only.

## License

[MIT](LICENSE) © 2026 29thnight. The generator packages include libclang (Apache-2.0 WITH LLVM-exception); see
`share/reflgen/licenses`.

## Roadmap

1. **Code generator** — CMake and MSBuild integration, NuGet package and vcpkg port done. Remaining: class templates,
   header-backend method overloads, enumerator attributes and cross-IFC declaration resolution.
2. **Visual Studio extension** — first version done (generate on save and open, Error List, IntelliSense refresh,
   completion, automatic friend insertion). Remaining: refreshing IntelliSense for the changed translation units
   instead of the whole solution, CMake (Open Folder) support, the C++26 migration codemod, Marketplace publishing.
3. **C++26 standard backend** — public types, typed annotations, registration and serialization implemented
   ([C++26 migration](#c26-migration)). Remaining: broader compiler coverage and inherited/private member support.
4. GCC and libc++ CI.

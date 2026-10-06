# reflgen

Macro-free, attribute-driven reflection for C++20/23. This package wires the reflgen code generator into Visual C++
(`.vcxproj`) builds: headers carry only `[[reflgen::…]]` attributes, and the build force-includes the generated
descriptions. No macros, no generated includes in your headers, nothing to register in the project.

## Install

Install `reflgen` into a `.vcxproj` (`packages.config`). Installing imports `build\native\reflgen.targets`, which runs
the generator before compilation and adds the headers to the include path — there is nothing else to set.

Requirements: Windows x64, Visual Studio 2026 (18.x), C++20 or later. The package includes the generator
(`reflgen.exe` with its own `libclang.dll`) and the header-only library.

## Use

Declare reflected types in a header of the project (a `ClInclude` item — the generator reads the project's headers):

```cpp
// player.h
#pragma once
#include "reflgen/reflgen.h"
#include <string>

class [[reflgen::reflect]] player
{
public:
    std::string name;

    [[reflgen::range(1, 99)]]
    int level = 1;

    [[reflgen::ignore]]
    int cache = 0; // left out of reflection
};
```

```cpp
// main.cpp
#include "player.h"
#include "reflgen/json.h"

std::string text = reflgen::json::to_string(player{ "ada", 7 }, 4);
player loaded = reflgen::json::from_string<player>(text);
```

Every non-static data member of a `[[reflgen::reflect]]` class is reflected; methods only when marked. The same
descriptions drive the runtime descriptors (`reflgen::type_descriptor_of<T>()`: fields, attributes, method invocation)
and the JSON and binary serializers, and you can plug in your own format through `reflgen::writer` / `reflgen::reader`.

For downstream generators, opt into a versioned JSON declaration manifest with the same property as the standalone
MSBuild integration:

```xml
<PropertyGroup>
  <ReflgenDeclarationsJson>$(IntDir)reflgen\$(ProjectName).declarations.json</ReflgenDeclarationsJson>
</PropertyGroup>
```

Leave it unset to disable it. This is declaration metadata, not an ABI or exportability guarantee; reflection
generation is unchanged. Relative paths are project-relative and must resolve under `ReflgenOutputDirectory`.
Unchanged content keeps its timestamp, a deleted manifest is regenerated when MSBuild runs, and Clean removes outputs
owned by a successful generation. Existing unowned files and input/output collisions are rejected. The package
imports the shared targets, so no extra import is needed.

`ReflgenInterop=true` also opts into C ABI / C# bindings (`reflgen_<module>_interop.h/.cpp/.cs`). Use
`ReflgenInteropLibrary` for the native library name (default: module name) and `ReflgenInteropNamespace` for the C#
namespace (default: `Reflgen.Generated`). The C++ implementation is compiled independently of
`ReflgenRegistration=false`; the C# output is not compiled by the C++ project.

## Learn more

- Documentation: https://github.com/29thnight/reflgen_cpp#readme
- Release notes: https://github.com/29thnight/reflgen_cpp/releases
- Visual Studio extension (generate on save, diagnostics in the Error List, attribute completion): a `.vsix` on the
  releases page

License: MIT. The package includes libclang (Apache-2.0 WITH LLVM-exception).

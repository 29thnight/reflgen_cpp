# Optional C ABI and C# bindings

Interop is an opt-in code generator. Reflection, serialization, and external
exposure are separate decisions. A reflected field or a reflection friend does
not authorize an external call.

## Declare the surface

```cpp
namespace sample
{
    class [[reflgen::interop("csharp"), reflgen::lifetime("borrowed")]] counter
    {
      public:
        [[reflgen::interop("csharp")]]
        int value() const { return value_; }

        [[reflgen::interop("csharp")]]
        void set_value(int value) { value_ = value; }

        // This method is not exported, even though its class is selected.
        void reset() { value_ = 0; }

      private:
        int value_ = 0;
    };
}
```

- `interop("c")` selects a C boundary only
- `interop("csharp")` selects the same C boundary and its C# declaration
- The type and **each method** must be explicitly selected
- A `csharp` type may contain methods selected for either target; a `c` type
  cannot contain a `csharp` method
- Every selected type must state `lifetime("borrowed")`; duplicate, conflicting,
  missing, or unsupported attribute arguments are diagnostics
- `[[reflgen::reflect]]` can be added independently. Export-only methods do not
  become reflection methods or Inspector actions

These are generator directives, not runtime attribute objects. They use the
existing C++ attribute scanner and do not require a reflection macro or a managed
runtime inside the native library.

Write these `[[...]]` annotations directly on the declaration. Expanding an
annotation through a preprocessor macro is not a supported selection mechanism.

## Generate and build

```sh
reflgen --module sample --output build/generated --interop \
  --interop-library sample_native --interop-namespace Sample.Native \
  counter.h -- -std=c++20
```

The optional arguments select the native library name used by C# and the managed
namespace. Defaults are the module name and `Reflgen.Generated`. Without
`--interop`, no interop files are written. Ordinary reflection generation remains
available with or without this option.

The outputs are:

- `reflgen_sample_interop.h`: C-compatible declarations, opaque receiver types,
  fixed-width ABI values, and status codes
- `reflgen_sample_interop.cpp`: C++ wrappers calling the original public methods
- `reflgen_sample_interop.cs`: C# P/Invoke declarations for `csharp` methods

All three files are written, including an empty managed surface for a C-only
module. Unchanged contents are not rewritten. Add the generated C++ source to the
native library that actually contains the original implementation; the generator
does not build, load, publish, or deploy that library. Add the generated C# source
to its consumer project. The declared library name must resolve to that native
library on the consumer's platform.

The header declarations carry the export attribute (`__declspec(dllexport)` on
Windows, default visibility elsewhere) only when the generated C++ source includes
them; that source defines `REFLGEN_<MODULE>_INTEROP_BUILD` first, and its
definitions inherit the attribute. Native C or C++ consumers include the same
header without that definition and link against the library normally.

```cmake
reflgen_generate(sample_native
    HEADERS counter.h
    INTEROP
    INTEROP_LIBRARY sample_native
    INTEROP_NAMESPACE Sample.Native)
get_target_property(sample_managed_source sample_native REFLGEN_INTEROP_CSHARP)
```

For MSBuild, set `ReflgenInterop` to `true`, with optional
`ReflgenInteropLibrary` and `ReflgenInteropNamespace`. The generated wrapper source
is independent of `ReflgenRegistration`: disabling reflection registration does
not disable explicitly requested interop wrappers. C# source remains a consumer
input, never a native compilation unit.

## First supported contract

The first backend supports public, unambiguous, non-static member methods with
primitive value parameters and a primitive or `void` return. Ordinary `const`
getters are supported. Integer values are lowered to fixed-width signed or
unsigned integers using the parsed target's type widths. Boolean values use
`uint8_t`: zero is false, any nonzero input is true, and output is normalized to
zero or one. Floating-point support is limited to supported `float` and `double`
representations.

Every generated call returns an `int32_t` status. Non-void results use a required
output pointer, checked before invoking the method:

- `0`: success
- `1`: null receiver or required output pointer
- `2`: a caught C++ exception

Exceptions never intentionally unwind across the generated C boundary. A caught
exception does not roll back side effects already performed by the original
method. The wrapper adds no synchronization or thread scheduling; the original
thread-affinity and synchronization requirements remain the caller's contract.

Calling convention, primitive lowering, selected symbols, target, receiver
ownership, and error convention form one deterministic generated contract.
Fingerprint information identifies matching generated native/managed surfaces;
it is not a claim of compatibility between different C++ target ABIs.

The generated C# requires C# 9 or later (`nint`) and uses `DllImport`, explicit
Cdecl, exact entry-point names, and blittable scalar arguments. It is ordinary static interop code; this work does
not implement or verify a NativeAOT hosting or deployment backend. In particular,
CreatorEngine's existing CoreCLR host is unchanged.

For module `sample`, compare the native `reflgen_sample_abi_fingerprint()` result
with the generated C# `reflgen_sample_interop.AbiFingerprint` before using the
bindings. The raw methods do not perform that check automatically. The fingerprint
is deterministic after sorting selected classes/methods by qualified name/name;
it is a compatibility check, not a cryptographic authenticity check.

## Borrowing is a lifetime obligation

An opaque receiver hides the class representation. It is **not a generational
handle**, does not validate stale pointers, and does not make arbitrary pointers
safe. Native code creates and owns the object and supplies the correctly typed
live address. The caller must keep that exact object alive throughout every call
and follow its thread rules. A dangling address, the address of another type, or
an invalid non-null output pointer violates the contract; a C++ `catch` cannot
make such memory errors safe.

There are no generated constructors, deletion functions, finalizers, or
`IDisposable` ownership wrappers. Engine-specific object registries, access
checks, or thread guards belong in an explicit adapter layer. Call existing
writer methods to preserve application side effects; direct reflected backing
fields are not exported.

## Deliberately unsupported

Unsupported selected declarations fail generation instead of guessing a marshaler:
private/protected members, overloads (including selected names imported with
`using Base::method`), static members, templates, nested classes, constructors,
destructors, operators, deleted or `consteval` functions, variadic functions, volatile or
ref-qualified receiver methods, references, raw pointer parameters/results,
strings, callbacks, enums, records/POD values, containers, and ownership transfer.
Wide character types and `long double` are not lowered. Unreadable/macro-obscured
receiver qualifiers and macro-expanded declaration specifiers are rejected rather
than assumed safe. Normal `constexpr` methods and primitive typedefs remain valid.
Record layouts, enum value validation, string encoding/buffer ownership, custom
handles, and owned objects need explicit future contracts.

The initial platform set is x86/x64/ARM64 Linux, x64/ARM64 macOS, and x64/ARM64
Windows. Unknown targets are rejected. In particular, 32-bit Windows export-name decoration is not implemented
and is rejected rather than emitting unreliable exact-name P/Invoke declarations.

This is the emitter's supported **parse-target** set, not a claim that every
cross-build configuration is detected or tested. The existing CMake/MSBuild
integration forwards include paths, definitions, and language standard to
libclang; it does not reproduce every compiler target or ABI option. For an
explicit cross-target generation, pass the matching target and toolchain flags
after `--`, for example `-- --target=aarch64-unknown-linux-gnu`. Provide that
target's include/sysroot configuration as needed. Generated native guards reject
architecture/OS and scalar-width mismatches rather than trusting host defaults.
Matching those guards is not a substitute for compiling and validating both sides
on the intended target.

## Declaration manifest is separate

`--declarations-json FILE` can be used alongside interop output. The
[version-1 declaration contract](DECLARATIONS.md) records
the currently reflected declarations and their parsed target/type/attribute facts;
it is not an ABI schema, does not itself select exports, and does not evaluate
attribute argument tokens. Export-only declarations do not silently become
reflection declarations through the manifest.

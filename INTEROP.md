# Optional C ABI, C# and F# bindings

## Injected native function tables (1.2.0)

For an executable-hosted API such as CreatorEngine's `ScriptApiTable`, use
`--fsharp-table` with an explicit `[[reflgen::table(...)]]` alias. This separate
backend generates F# POD declarations, cached function-pointer delegates, a native
ABI proof and a JSON manifest. It consumes a table supplied by the host and does
not require a C# binding assembly or a library-name lookup for each API entry.
See the [complete contract, example, validation and engine migration scope](docs/creatorengine-fsharp-abi.md).
The method-oriented C ABI / C# / F# route below remains available through `--interop`.


For the separate PrSM production handle-ABI foundation and shared header/module
Binding IR, see [PrSM production ABI](docs/prsm-production-abi.md). It does not
change the separate C/C#/F# interop contract described here.

Interop is an opt-in code generator for the libclang header frontend (`reflgen
--interop`). The MSVC IFC frontend currently has a separate PrSM path and does not
generate this C/C#/F# interop surface. Reflection, serialization, and external
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
- `interop("fsharp")` selects the same C boundary and its F# declaration
- The type and **each method** must be explicitly selected
- A `csharp` or `fsharp` type may contain methods selected for its own target or C;
  managed methods require the matching managed class target
- Every selected type must state a lifetime. `borrowed` is supported for all targets;
  `owned` is supported for F# receivers under the contract below. Duplicate,
  conflicting, missing, or unsupported attribute arguments are diagnostics
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

The optional arguments select the native library name used by managed bindings and the managed
namespace. Defaults are the module name and `Reflgen.Generated`. Without
`--interop`, no interop files are written. Ordinary reflection generation remains
available with or without this option.

The outputs are:

- `reflgen_sample_interop.h`: C-compatible declarations, opaque receiver types,
  fixed-width ABI values, and status codes
- `reflgen_sample_interop.cpp`: C++ wrappers calling the original public methods
- `reflgen_sample_interop.cs`: C# P/Invoke declarations for `csharp` methods
- `reflgen_sample_interop.fs`: F# P/Invoke declarations for `fsharp` methods

All four files are written, including empty managed surfaces for a C-only
module. Unchanged contents are not rewritten. Add the generated C++ source to the
native library that actually contains the original implementation; the generator
does not build, load, publish, or deploy that library. Add the appropriate generated managed source
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
get_target_property(sample_csharp_source sample_native REFLGEN_INTEROP_CSHARP)
get_target_property(sample_fsharp_source sample_native REFLGEN_INTEROP_FSHARP)
```

For MSBuild, set `ReflgenInterop` to `true`, with optional
`ReflgenInteropLibrary` and `ReflgenInteropNamespace`. The generated wrapper source
is independent of `ReflgenRegistration`: disabling reflection registration does
not disable explicitly requested interop wrappers. C# and F# sources remain consumer inputs, never native compilation units.

## Common call contract

The common backend supports public, unambiguous, non-static member methods with
primitive value parameters and a primitive or `void` return. F# additionally
supports the explicitly bounded contracts described below. Ordinary `const`
getters are supported. Integer values are lowered to fixed-width signed or
unsigned integers using the parsed target's type widths. Boolean values use
`uint8_t`: zero is false, any nonzero input is true, and output is normalized to
zero or one. Floating-point support is limited to supported `float` and `double`
representations.

Every generated call returns an `int32_t` status. Non-void results use a required
output pointer, checked before invoking the method:

- `0`: success
- `1`: invalid required pointer, null callback or invalid ownership transfer input
- `2`: a caught C++ exception

Exceptions never intentionally unwind across the generated C boundary. A caught
exception does not roll back side effects already performed by the original
method. The wrapper adds no synchronization or thread scheduling; the original
thread-affinity and synchronization requirements remain the caller's contract.

Calling convention, supported type lowering, selected symbols and value layouts, target,
receiver ownership, and error convention form one deterministic generated contract.
Fingerprint information identifies matching generated native/managed surfaces;
it is not a claim of compatibility between different C++ target ABIs.

The generated C# requires C# 9 or later (`nint`) and uses `DllImport`, explicit
Cdecl, exact entry-point names, and blittable scalar arguments. It is ordinary static interop code; this work does
not implement or verify a NativeAOT hosting or deployment backend. In particular,
CreatorEngine's existing CoreCLR host is unchanged.

For module `sample`, compare the native `reflgen_sample_abi_fingerprint()` result
with the generated C# or F# `reflgen_sample_interop.AbiFingerprint` before using
the bindings. The raw methods do not perform that check automatically. The fingerprint
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

Borrowed classes do not receive generated deletion or ownership wrappers. F# owned
classes have the separate SafeHandle and destruction contract below; constructors
and factory functions are not generated. Engine-specific object registries, access
checks, or thread guards belong in an explicit adapter layer. Call existing
writer methods to preserve application side effects; direct reflected backing
fields are not exported.

## Deliberately unsupported

Unsupported selected declarations fail generation rather than guessing a marshaler.
Private/protected members, overloads (including selected names imported with
`using Base::method`), static methods, templates, nested classes, constructors,
destructors, operators, deleted or `consteval` functions, variadic methods,
volatile or ref-qualified receiver methods, references, pointer-to-pointer
parameters, arbitrary pointer returns, containers, wide character values and
`long double` are unsupported. Normal `constexpr` methods and primitive typedefs
remain valid. Unreadable or macro-obscured qualifiers fail closed.

The F# sections below describe the bounded exceptions: explicitly selected POD
values and scoped enums, borrowed object pointers, a synchronous callback signature,
and explicitly declared unique ownership. Arbitrary strings, retained callbacks,
shared ownership and automatic stale-handle detection remain unsupported. Record
pointer layouts are unchecked unless explicitly selected for F# layout validation.

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

### F# P/Invoke

Select a borrowed class and its public methods with `[[reflgen::interop("fsharp")]]` and
`[[reflgen::lifetime("borrowed")]]` on the class. With interop generation enabled,
`reflgen_<module>_interop.fs` contains raw F# P/Invoke declarations for the same C ABI.
Compile the generated file in the F# project and load the configured native library.
Compare `AbiFingerprint` with the native fingerprint before using a borrowed receiver.
The caller must keep the native object alive and check the returned status before reading
the result. Boolean values use bytes; selected POD, scoped enum, pointer, callback
and owned-handle boundaries have the separate contracts below. The interop namespace
option also sets the F# namespace.

Opaque `void*` and `const void*` input parameters are passed as borrowed addresses
(`nativeint` in F#, `nint` in C#). The caller is responsible for validity,
alignment, access, and lifetime; no ownership transfer or bounds checking is inferred.

Object pointer input parameters to primitive, enum, or record types are also passed
as `void*` or `const void*`; their native pointee type and constness remain part of
the fingerprint. Function pointers are supported only by the explicit synchronous callback contract
described below. Pointer-to-pointer parameters and pointer returns are rejected. This is a raw borrowed address, not a marshalled or checked
object: nullability, bounds, alignment, layout, mutability, and lifetime follow the
selected native method's contract. In particular a `const char*` can be passed as a
caller-allocated NUL-terminated UTF-8 byte sequence only when that method specifies
UTF-8, does not retain the address, and cannot read beyond the terminator. The caller
must free it after the call; no UTF-8 validation or conversion is performed by the
generated binding. Without an explicit F# value-layout alias, record pointer layout is not checked or
captured by the ABI fingerprint; the caller must independently verify size, alignment,
field offsets and lifetime.

Scoped enum (`enum class` or `enum struct`) parameters and results use their fixed
underlying integer representation, not a managed enum type. The generated native
source verifies size and signedness. Values outside the declared enumerators are not
validated; the caller and native method must agree on which values are meaningful.
Unscoped enums remain rejected.

For F# callers, `withUtf8 text (fun address -> ...)` allocates a NUL-terminated
UTF-8 buffer for the duration of the callback, then frees it even if the callback
throws. It rejects null, embedded NUL, and invalid UTF-16. The native method must
not retain the address after the callback; use this only for an API documented as
accepting UTF-8. The helper does not support binary strings or string outputs.

### Opt-in F# POD layout checks

A value alias can opt a record into generated layout checks:

```cpp
struct point { int x; int y; };
using PointLayout [[reflgen::interop("fsharp"), reflgen::value("Point")]] = point;
```

The generated F# module provides `validatePointLayout typeof<Point>` and
`isPointAligned address`. Compare the native ABI fingerprint first, then require
both checks before passing a borrowed `point*`. The F# fields must use the native
field names and matching scalar representations. The generated native C++ checks
standard layout, trivial copyability, size, alignment, offsets and field types at
compile time; the fingerprint includes the selected layout. This does not allocate
or own an object or validate its lifetime. This first slice supports complete
records without bases and public non-bit-field integer or float fields. It does not
support nested records, arrays, unions, or bool fields.

### Synchronous F# callbacks

A selected method may use one `int (*)(int)` callback parameter when explicitly
marked `[[reflgen::callback("synchronous")]]` alongside its interop selection.
The C ABI uses a Cdecl callback taking and returning `int32_t`; the target's C++
`int` must be a signed 32-bit value. The native implementation must call it only
on the calling thread, finish all callbacks before returning, and never retain the
function pointer. Null callback pointers are rejected before entering the method.

Use the generated F# `withSynchronousCallback` helper around the native call. It
keeps the delegate alive for that call. If the managed callback throws, the thunk
returns zero to native code and rethrows after the native call returns; native
side effects are not rolled back. Native code must tolerate zero as the callback
result. Asynchronous, retained, multi-threaded, multi-parameter and other callback
signatures are unsupported; this contract cannot enforce behavior inside arbitrary
native code.

### Selected F# POD values

Methods selected for F# may accept or return by value a record that has an explicit
F# value-layout alias as described above. The generated C ABI accepts an input
`const void*` pointing to the selected record bytes and a non-null output `void*`
for a record result; F# declarations use `nativeint` for both. The wrapper copies
input bytes into a native value before the call and copies a returned value only
after successful completion. Null buffers are rejected; an exception leaves the
output buffer unchanged. The caller owns both buffers, must keep them alive for
the call, ensure they contain enough bytes and match the selected field layout,
and must check the ABI fingerprint and generated layout validator first. Buffer
aliasing is allowed because input is copied before the method executes.

The generated native checks require a non-union, standard-layout, trivially
copyable record and matching field types, offsets, size and alignment. The ABI
fingerprint includes the selected layout. No ownership of a caller buffer transfers
to native code. This is a bounded byte-copy ABI, not an automatic managed struct
marshaller. Pointer/reference fields, nested structures, arrays, bit fields, bool
and unions remain unsupported.

### Owned F# receivers

A class selected with `[[reflgen::interop("fsharp"), reflgen::lifetime("owned")]]`
exports a destroy function and an F# `Owned_<qualified_name>` SafeHandle type.
The native application must supply a factory that returns the exact complete C++
type allocated with a compatible single-object `new`; the generated code does not
generate a factory. The destructor must be public and nonthrowing, and `delete`
must be nonthrowing and valid in the generated native module. Arrays, borrowed objects, different
dynamic types and objects with a different deallocator are not supported.

`TakeOwnership(address)` checks non-null and the ABI fingerprint before adopting
the pointer. On failure the original owner retains responsibility. On success it
transfers one unique ownership reference; creating a second owner from the same
address or freeing it elsewhere is invalid and is not detected. Use
`withOwned_<qualified_name> owner (fun address -> ...)` for each call: it holds a
SafeHandle reference until the callback completes, even if another thread disposes
the owner during the call. Dispose the owner when done. Calls through raw pointers
must independently preserve the owner lifetime and synchronize with destruction.

The generated C destroy function accepts a pointer to the unique handle, rejects a
null argument or null handle, clears the caller's handle, and deletes the object.
It does not detect other copied or stale aliases. The native module must remain
loaded until every owner is released; SafeHandle finalization is a fallback and is
not deterministic. Release failures cannot be reported through `Dispose`; do not
unload the native module while owners remain. Retained or shared ownership is not supported. Explicitly annotated owned returns
and consuming parameters are described below.

### Owned method returns

A selected F# method may explicitly declare
`[[reflgen::return_ownership("owned")]]` when it returns a non-const pointer to
a class selected as F# `lifetime("owned")` in the same generation unit. The
native method must return either null or one unique object satisfying that type's
owned-receiver allocation and destruction contract. The generated C ABI returns a
status and writes the pointer to a required `void**` only on success; F# exposes
this as a `nativeint&`. A successful null return means no ownership was transferred.
On failure or exception the output is unchanged.

After a successful non-null result, the caller owns the raw pointer and must
transfer it exactly once to the matching generated `Owned_* .TakeOwnership` or
invoke the matching generated destroy function. If adoption fails, the caller
still owns the raw pointer and must destroy it. The library, pointer, and type
must satisfy the owned-receiver contract. Copying the raw value does not create
another valid owner; stale or duplicate ownership is not detected.

### Consuming an owned parameter

A selected F# method may mark `[[reflgen::consume_ownership("0")]]` when its
first parameter is a non-const pointer to a class selected as F# `lifetime("owned")`
in the same generation unit. Only parameter index 0 is supported. The native
method must unconditionally accept unique ownership on entry, on every path; it
should immediately wrap the pointer in an appropriate RAII owner. In particular,
it must destroy or deliberately transfer the object if it throws. The generator
cannot verify this native implementation obligation.

The C ABI takes `void**` and F# takes `nativeint&`. Before transferring, the
wrapper checks the receiver, result pointer when applicable, the address of the
input slot, and the owned pointer for null; it rejects the receiver itself as the
consumed object. A prevalidation failure leaves the slot unchanged. Once checks
pass, the wrapper clears the slot and calls the native method. The ownership
transfer remains committed if the method throws; on return the caller must inspect
the slot rather than infer ownership from the status code. The input slot must not
overlap the output buffer or other argument storage. Other aliases to the object
must not be used or destroyed by the caller after transfer.

Prefer the generated method-specific `transfer_<native_symbol>` function. It takes
the exact generated `Owned_*` type for the consuming parameter, binds the correct
native entry point, and returns status plus the scalar result when applicable.
The result is valid only for `StatusOk`. A POD result still uses a caller-owned
result buffer, while an owned result remains a raw pointer that must be adopted or
destroyed exactly once. The caller must keep the receiver valid for the call.

The lower-level `transferOwned_<qualified_name> owner (fun slot -> ...)` helper can
also transfer an existing matching `Owned_*` SafeHandle. Inside the callback, pass
`&slot.contents` directly as the consuming ABI's ownership parameter, without
altering the slot or retaining it. Use only the consuming method for the same
owned C++ type; the callback is user code and the helper cannot verify which native
function is called. The helper holds a SafeHandle reference throughout the call,
serializes generated `withOwned_*` and `transferOwned_*` operations on that owner,
and rejects reentrant operations. If native code clears the slot, it marks the
SafeHandle invalid even if the callback then throws. If the slot remains unchanged,
the handle retains ownership, subject to any concurrent `Dispose`.

Raw uniquely owned addresses may still be passed directly. Never pass
`DangerousGetHandle()` from a live SafeHandle to a consuming call outside the
transfer helper: that leaves a second owner and can cause double deletion. Direct
use of `DangerousAddRef`, raw aliases, externally closing the handle, altering the
transfer slot, or retaining it bypasses this protocol. Shared ownership and
multiple consuming parameters remain unsupported.

### Planned engine UTF-16 string boundary

The engine is expected to supply UTF-16 through its own string type. That engine
string is not yet integrated into this generator: the present `withUtf8` helper
applies only to explicitly documented NUL-terminated UTF-8 APIs and does not define
or convert the engine string ABI. A future adapter must use the engine's actual
string representation and explicitly establish code-unit length, embedded NUL
behavior, surrogate handling, mutability, lifetime, ownership and release rules.
No UTF-16 engine-string generation, native invocation, or production integration
is claimed by the current tests.

# PrSM production binding ABI v1 — common IR and generated bridges

This contract is distinct from borrowed-address `--interop` and from
`prsm.mock-bindings` / `prsm.mock-call-descriptor.v1`. It is the starting contract
for a production PrSM backend, not a claim of completed CreatorEngine integration.
PrSM consumer implementation baseline: `503315cc755b4b91df2d90c869cfd34b2d7c75bd`.

## Implemented boundary

`generator/src/binding_ir.h` defines the common, frontend-independent Binding IR.
`include/reflgen/prsm/binding_abi.h` publishes the C/C++ fixed-width handle/status
types, status allocation, version and validator function-pointer protocol. It
does not duplicate `PrsmModule`, declare engine implementations or change PrSM's
authoritative `abi/prsm_abi.h`.
`build_binding_module` validates explicitly selected, already lowered contracts
and returns an immutable module or `RG0300` with no module. Header and
IFC adapters use the same `lower_prsm` semantic lowering and builder; output backends consume its validated
result. The builder does not prove that a C++ declaration exists or that native
code implements the contract. Extraction supplies semantic facts; the generated
C++ member-pointer static assertions check the actual declaration at compilation.

Implemented: nominal handle receivers; scalar method/getter/setter contracts;
target/member/property validation; deterministic symbol ordering; versioned
canonical call descriptors; SHA-256; independent C++/Rust/.NET vectors; explicit
CLI selection; libclang/IFC declaration lowering; C/C++ bridge generation;
type-specialized engine access hooks; actual header/module ABI equivalence tests;
production JSON catalogs; compiled native signature/address tables; strict PrSM
catalog consumption, semantic checking and checked LLVM calls; explicit nominal
owner ingress; AOT execution through both generated provider paths.

Not implemented yet: CreatorEngine access-hook implementations,
module fingerprint aggregation, general free/static
functions, direct fields, conversions, strings, enums, records, callbacks,
handle-valued parameters/results beyond explicit owner ingress, ownership transfer or suspension.

The Rust crate under `generator/tests/prsm_abi_rust` is a standalone consumer-side
contract test. The production consumer lives in PrSM's `compiler/prsm-catalog`;
the existing mock protocol remains separately selectable.

## Responsibility and selection

Reflection, C++ `export`, and script exposure are independent. Every exposed type
and callable must be explicitly authorized. Private reflection friendship does
not authorize script access. Exported but unselected declarations grant nothing.

The frontend proves access, canonical types/widths, qualifiers, exact callable
identity, and annotation policy; lowering constructs the Binding IR. Methods in
this first slice must be public, ordinary, unambiguous, nonstatic, nondeleted,
nonvolatile, nonvariadic and have no ref qualifiers. Overloads are rejected.
`const` does not imply purity. Getters require no arguments, nonvoid result and
`read`; setters require one matching argument, void result and `write`. A setter
requires its getter. A getter alone is read-only. Effects do not authorize call
elision, reordering, automatic thread scheduling or suspension.

The IR retains logical module identity, nominal C++/script type names, source
include/import provenance and selected call contracts. Sources and diagnostic
positions are not ABI identity. Moving an API from a header into a named module
must preserve its logical binding module, nominal identities, symbols and
normalized declaration signature. Script aliases are excluded from native call
digests, but MUST participate in a future script-interface/cache fingerprint.

The runtime fixture compiles a real primary IFC and matching MSVC `/P` metadata,
generates/compiles/runs both header and module wrappers, and compares their ABI
tables byte-for-byte. The earlier model-level provenance test also remains.

## Selection and generation

Use ordinary declaration annotations; macros are not a supported header selection
mechanism. Type and each method require `interop("prsm")`. The type also requires
`lifetime("borrowed")`. `property_get("Name")` / `property_set("Name")` explicitly
map methods to one property; neither accessor is inferred from its C++ name.
`effect("read")` or `effect("write")` can be specified. Method effect defaults to
write, getter to read, setter to write; contradictory accessor effects fail.

```cpp
class [[reflgen::interop("prsm"), reflgen::lifetime("borrowed")]] Counter
{
public:
    [[reflgen::interop("prsm"), reflgen::property_get("Count")]]
    int Get() const noexcept;
    [[reflgen::interop("prsm"), reflgen::property_set("Count")]]
    void Set(int value);
    [[reflgen::interop("prsm")]]
    int Add(int value);
};
```

```powershell
reflgen --module demo --output generated --prsm `
    --prsm-adapter-header path/to/engine_adapter.h path/to/counter.h -- -std=c++20

# Compile provider first, then produce matching /P metadata with its exact settings.
reflgen-ifc --ifc counter.ifc --import demo.counter --module demo --output generated `
    --metadata counter.i --prsm --prsm-adapter-header path/to/engine_adapter.h
```

`--prsm` is separate from `--interop`; combining them is rejected. IFC requires
metadata and currently accepts Windows x64 primary modules only. This step does
not add `PRSM` options to CMake/MSBuild helper functions: call the CLI explicitly
in a subsequent build target. The repository test is an executable wiring example.

Outputs are `reflgen_<module>_prsm.h`, `_prsm.cpp`, `_prsm.abi.tsv`, and
`_prsm.bindings.json`. Add the
source to the engine bridge target, with include/import dependencies and adapter
header available. Its C declarations carry export visibility only while compiling
the generated implementation. Output is deterministic; unchanged files retain
their timestamps. Semantic errors publish no new bridge outputs. Existing files
from a previous successful run are retained on failure; do not compile them after
a failed generator command. Inputs and the adapter cannot be overwritten by the
optional bridge outputs.

The TSV is an inspection/equivalence artifact, **not** a production compiler catalog
or trusted runtime signature table. Its first line is `# reflgen.prsm.calls<TAB>1`;
each remaining line is symbol, nominal receiver, script member/property name,
SHA-256 hex, canonical descriptor, separated by TAB. Methods/accessors have
symbols `ce_prsm_bind_<hex(module)>_<hex(cpp receiver)>_<hex(cpp member)>` using
lowercase UTF-8 byte hex, avoiding flattening/name collisions. It intentionally
does not implement `ce_prsm_validate_binding`; the engine host supplies that single
context-aware function using the generated native tables described below.

`reflgen::prsm::binding_access<T>` in `reflgen/prsm/access.h` is an incomplete
primary template with no pointer fallback. The engine adapter must specialize it
for each exposed type. Required: constructor `(uint64_t)`, `status() noexcept`
returning `int32_t`, `get() noexcept` returning `T*`, and a noexcept destructor.
The object must validate context/thread/registry/type/generation and retain call
access until destruction. The generator checks the C++ method types and adapter
interface, not the correctness of the engine's registry. See `tests/prsm/adapter.h`
for a bounded test adapter; its identity check is not a production registry.

## C calling contract

Every synchronous generated wrapper will have C linkage, explicit cdecl, a signed
32-bit status return and no exception crossing the boundary. Example:

```c
int32_t ce_prsm_bind_actor_get(uint64_t receiver, int32_t* out_value);
int32_t ce_prsm_bind_actor_set(uint64_t receiver, int32_t value);
```

The receiver is scalar `uint64_t`, not a C struct passed by value and never an
address cast. Bit allocation is private to the engine registry. Every access must
validate registry domain, generation and nominal type, then hold an access guard
that keeps the object usable until the call and result conversion finish. The
guard may enforce an engine game-thread lifetime rule; an `is_alive` test followed
by an unprotected pointer dereference is insufficient. Borrowed script handles
do not retain objects across calls or coroutine suspension.

Parameters are fixed-width signed/unsigned 8/16/32/64-bit integers, `uint8_t` for
bool, IEEE binary32 `float`, or IEEE binary64 `double`. Zero bool input is false,
other inputs true; bool output is normalized to zero/one. Void is allowed only
as a result and has no output parameter. Nonvoid results have a required typed
output pointer last. Narrow-argument/result extension attributes must follow the
actual target C ABI, not an assumed LLVM integer call shape. Native floating
environment establishment/restoration is the engine/PrSM runtime boundary's
responsibility; bindings do not imply deterministic native arithmetic.

Status allocation (production v1; do not infer these from mock status values):

| Value | Meaning |
|---|---|
| 0 | Success |
| 1 | Invalid required argument/output |
| 2 | Caught C++ exception or failed native result conversion |
| 3 | Invalid/null/stale/foreign/wrong-nominal-type handle |
| 4 | Wrong thread |
| 5 | ABI signature mismatch or unknown binding |
| 6 | Missing/inactive engine call context |

All nonzero values are failures to the compiler, including future unknown codes.
On failure the wrapper leaves output storage untouched and the compiler must not
read it. Write output only after all conversions succeed. Native side effects
are not rolled back. Null output is checked before invocation; callers must still
supply valid writable storage. Catching C++ exceptions cannot validate arbitrary
native pointers. Runtime failures propagate through PrSM's checked failure path
at the source location, not as successful default values or a cross-ABI throw.

## Trusted signature handshake

Reserve this fixed production entry point (implementation is a later step):

```c
int32_t ce_prsm_validate_binding(const uint8_t* symbol_utf8,
                               uint32_t symbol_length,
                               const uint8_t* digest32);
```

`digest32` addresses exactly 32 SHA-256 bytes, not hex text. Generated script code
must validate before every imported call, after ordinary receiver/argument
evaluation and before native invocation. Failure must skip the adapter. The host
compares against a trusted table compiled with the actual wrapper definitions,
not a table loaded from the caller's catalog. Future table emission must include
C++ member and exported C function-pointer static assertions. Native JIT symbol
registration must bind this fixed validator separately from catalog input.
Matching metadata alone does not establish the native contract. This is an ABI
compatibility mechanism, not a native-code sandbox or authentication.

Do not remove per-call validation until load-time resolution can pin both the
validated addresses and their module lifetimes, including reload invalidation.
The current builder reserves callable names under `ce_prsm_bind_`; the validator
is outside that namespace and cannot be selected as an ordinary callable.

## Canonical call descriptor and SHA-256

Append every ordered field as `decimal_utf8_byte_length:value;`, without BOM,
newline, padding, hex conversion or locale-dependent formatting. An empty field
is `0:;`. Hash those exact bytes with SHA-256. The IR stores 64 lowercase hex
digits for tooling; the runtime handshake transmits the decoded 32 bytes.

The ordered fields for `reflgen.prsm.call.v1` are:

1. `reflgen.prsm.call.v1`
2. `cdecl`, `int32`, `0`, `typed_out_pointer`, `no_out_pointer`
3. `generational_u64`, `domain_generation_nominal_type`, `borrowed_call_guard`, `game_thread`
4. `caught_to_status`, `u8_nonzero_in_normalized_out`, `untouched`, `synchronous`
5. Logical binding module name, exact target triple, decimal pointer width (`64`)
6. C symbol, operation (`method`, `getter`, `setter`)
7. Canonical nominal C++ receiver, C++ member name, normalized declaration signature
8. `read` or `write`
9. Result type, decimal parameter count, then each parameter type in source order

Scalar tokens: `void`, `bool`, `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`,
`uint32`, `uint64`, `float32`, `float64`. Shared semantic lowering normalizes a
signature to `result(param,param)` using these target-resolved tokens, followed
by ` const` and/or ` noexcept` where present. Typedef spelling does not enter
the descriptor. Native canonical primitive spellings remain in the IR separately
for exact C++ member-pointer assertions (e.g. Windows `long` versus `int`).
Computed/unknown exception specifications and unsupported qualifiers fail rather
than guessing a signature. The original foundation vectors also directly test
the low-level descriptor builder with arbitrary signature bytes.
The UTF-8 signature vector is an encoder stress case, not a supported declaration
extraction fixture.

Initial accepted profiles: `x86_64-pc-windows-msvc`,
`x86_64-pc-linux-gnu`, `x86_64-unknown-linux-gnu`,
`aarch64-unknown-linux-gnu`, each with 64-bit pointers. The header adapter maps
libclang's numeric MSVC-version suffix (e.g. `x86_64-pc-windows-msvc19.51.36260`)
to the explicit `x86_64-pc-windows-msvc` profile used by IFC. The declaration
manifest continues to retain the original parse triple. Other triples are not
aliased; changing the profile changes the descriptor. These are
generation profiles, not a claim of executing on each platform.

Source paths, import spelling, timestamps, tool version, script aliases and
diagnostic positions are excluded. Logical module, target, native identity,
operation, effect and lowered types are included. An ABI-semantic change to a
fixed field or encoding requires a new descriptor version.

The existing interop FNV-1a fingerprint, raw declaration JSON SHA-256 and PrSM
`PrsmModule.engine_fingerprint[32]` are separate contracts. This call digest must
not be copied into those fields. Aggregate engine-interface hashing and catalog
schema versioning must be agreed with the production PrSM consumer separately.

## Modules and build order

Header and IFC adapters converge before ABI emission. IFC now extracts selected
method parameter/result scalar types, scalar aliases, const/volatile/ref traits,
exception specifications, access and disallowed method traits independently of
its reflection model. Unknown facts are diagnosed, not defaulted to a callable
scalar signature. Unmatched explicit PrSM metadata annotations fail. Annotation
metadata must come from matching provider build/preprocessor settings.

Build: provider -> IFC/metadata -> Binding IR -> bridge/catalog -> bridge native
compilation and PrSM compilation -> final linking/loading. A provider must not
depend on its generated bridge. Include/import provenance affects bridge build
dependencies; it does not rename a native API. Partitions/reexports need explicit
ownership and deduplication before they are supported.

External libraries are exposed through engine-owned thin adapter modules, where
foreign values/lifetimes are converted to the engine contract. reflgen does not
automatically expose transitive third-party declarations.

## Verification

`generator/tests/fixtures/prsm-call-v1.tsv` contains seven independently encoded
vectors: Windows/Linux/ARM64 profiles, getter/setter/method, all scalar widths,
bool, float32/float64 and UTF-8 byte lengths. Column order is its first comment
line. There is no escaping; case inputs contain no TAB/newline. Empty parameters
mean zero parameters; otherwise comma-separated scalar tokens.

```powershell
./scripts/generate-prsm-abi-vectors.ps1  # .NET oracle, verify only
cargo test --locked --manifest-path generator/tests/prsm_abi_rust/Cargo.toml
# CMake target reflgen_binding_contract_tests includes C/C++ vectors and negative cases.
```

Use `-Write` only to deliberately regenerate reviewed vectors. The .NET oracle,
C++ encoder/hash implementation and independent Rust encoder/RustCrypto SHA-256
must agree. Tests also cover standard SHA-256 padding/multi-block vectors,
signature mutations, input provenance equivalence and invalid contracts. Actual
production acceptance still requires source -> generated bridge -> PrSM type
checking -> LLVM -> AOT/ORC -> engine runtime tests, including stale handles,
wrong nominal types/threads, exceptions, output preservation and ABI mismatch.

Local foundation checks (2026-10-19, Windows x64): MSVC C++20 Debug and Release,
and clang-cl C++20 Debug each passed `reflgen_binding_contract_tests` and existing
`reflgen_generator_tests`, with warnings treated as errors. Rust's two tests and
Clippy (`--all-targets -- -D warnings`) passed; the .NET oracle verified all seven
vectors. C header inclusion and validator pointer typing were compiled as C and
called from the C++ test. These results do not establish Linux/ARM64 execution,
actual header/IFC extraction equivalence or CreatorEngine runtime behavior. The
new CI job is wired but has not been executed remotely in this change.

The second-step runtime gate is `reflgen_prsm_bridge` (Windows). It uses the
header CLI, and also the IFC CLI when that target is configured. It checks six
selected entries on one type including explicit owner ingress, property round trips, a scalar alias, bool normalization,
float32/float64 arguments, C header compilation, C symbol exports, exception and
failed-call output preservation, missing context, wrong thread, stale/foreign
handle identities, guard release and repeat-generation timestamps. Fourteen invalid
declaration/policy fixtures are rejected on both paths without a new bridge.
It also verifies that input/output collisions preserve adapter bytes and that a
native declaration changed after generation fails the member-pointer assertions.
This bridge-only gate does not execute PrSM source. The compiler gate below does;
neither gate establishes a validated CreatorEngine object registry.

Second-step local checks (2026-10-19): MSVC C++20 Debug and a fresh Release build
passed the common contract tests, generator tests and the complete header+IFC
bridge gate. clang-cl C++20 Debug passed the same tests with its header bridge
path (MSVC supplies IFC). Existing IFC runtime/generation/rejection/attribute/
metadata gates passed, as did existing generated reflection, declaration JSON,
attribute catalog and native C/C# interop checks. Rust tests and the independent
.NET ABI vectors also passed. The CI jobs are wired; remote CI was not run here.
Native builds of these annotated fixtures suppress only unknown-attribute warnings
(`/wd5030` for MSVC, `/clang:-Wno-unknown-attributes` for clang-cl), retaining `/W4 /WX`.

```powershell
cmake --preset msvc-cpp20 -DREFLGEN_BUILD_IFC=ON
cmake --build --preset msvc-cpp20 --target reflgen_cli reflgen_ifc reflgen_binding_contract_tests reflgen_generator_tests
ctest --preset msvc-cpp20 -R '^reflgen_(binding_contract_tests|generator_tests|prsm_bridge)$'
```

## Production catalog and native authority

The deterministic UTF-8 JSON has `format: "reflgen.prsm.bindings"`, `version: 1`,
`abi: "reflgen.prsm.abi.v1"`, logical `module`, `target` (`triple`,
`pointer_width`), `types` (`name`, `cpp_name`) and `callables`. Each callable has
`name`, `cpp_name`, required nullable `receiver`, `operation`, normalized
`signature`, `const`, `noexcept`, `effect`, `result`, `params`, `symbol`, and
`abi_sha256`. It contains no native addresses. PrSM reconstructs the canonical
descriptor, recomputes SHA-256 and rejects inconsistent symbols, nominal types,
properties, duplicates, unknown fields, unsupported versions and targets before
name lookup. Limits are 16 MiB, 4096 types/calls and 64 parameters per call.
The full catalog bytes participate in project cache identity, including aliases.
Numeric MSVC version suffixes are normalized only for target compatibility;
the actual descriptor is hashed exactly as supplied.

Each generated implementation also compiles a trusted native table alongside
the member/hook signature assertions. Its C accessor is
`ce_prsm_table_<hex(module)>(uint32_t* count)`. The returned entries contain
symbol bytes/length, 32 digest bytes, and the actual wrapper address as
`uintptr_t`. The storage remains owned by the loaded provider. Null count returns
null. Addresses are host registration data, never script integers or JSON data.

The host defines `ce_prsm_validate_binding` once, checks active context and game
thread, then calls `reflgen::prsm::validate_binding` from `validator.h` over its
trusted `binding_table_view` list. The utility rejects unknown/duplicate symbols,
missing addresses and mismatched digests. Repeating the same table also fails
for its duplicate symbols. A catalog's self-consistent hash cannot authorize a
different native signature. Keep providers loaded and tables immutable throughout
validation and invocation. For AOT, the linker resolves wrapper symbols; an ORC
host can register the trusted entry addresses, never catalog-provided addresses.

Script needs an explicit path to obtain its first nominal handle. Optional type
annotation `owner("demo::Owner")` generates a zero-argument script function and
an owner wrapper with symbol suffix `_owner`. The engine specializes
`binding_owner<T>::get(uint64_t*)`; the wrapper checks status, acquires
`binding_access<T>` for the returned handle and publishes it only on success.
The IR operation is `owner`, native identity is `binding_owner`, effect is `read`,
signature is `handle:<cpp_name>()`, result is `handle:<cpp_name>`, and receiver is
null. This is borrowed ingress, not allocation, lifetime extension or an inferred
singleton. Owner names cannot shadow types, builtins or their namespace prefixes.

## PrSM invocation and executable gate

```powershell
prsmc check tests/prsm/calls.prsm --bindings generated/reflgen_demo_prsm.bindings.json
prsmc build tests/prsm/calls.prsm --bindings generated/reflgen_demo_prsm.bindings.json -o calls.obj
prsmc build-module tests/prsm/component.prsm --bindings generated/reflgen_demo_prsm.bindings.json -o component.obj
```

`--bindings` is mutually exclusive with mock `--declarations`/`--binding-profile`.
It applies to single-source and project builds. Generated LLVM calls use the
fixed production validator before every native call and read outputs only after
success. The host supplies the engine context, validator and provider symbols;
the standalone runner does not automatically load an engine. See PrSM's
`compiler/PRODUCTION_BINDINGS.md` and `tests/prsm/compiler_host.cpp.in` here.

Set CMake `REFLGEN_PRSM_COMPILER` to a built native `prsmc` to enable
`reflgen_prsm_compiler`. It regenerates both header/IFC providers, compares JSON
and diagnostic signatures, compiles PrSM to real LLVM objects, links and executes
owner/method/property calls. It checks result 74, context/thread/stale-handle and
native-exception failures, hidden method rejection, malformed catalogs, project
cache reuse, component object emission, and a rehashed wrong-result catalog
blocked by the original native table before invoking that method.

The executable host is a bounded test registry. Actual CreatorEngine adapters,
production binding ORC execution and engine module deployment/hot reload remain
separate integration work. Generator output is not proof of those behaviors.

Local production checks (2026-10-19): MSVC Debug and fresh Release passed the
contract, generator and full header+IFC compiler gate. PrSM's catalog, semantics,
project, codegen and CLI test suites passed using the actual LLVM 22.1 C API.
Scoped library/binary Clippy passed with warnings denied. The broader all-target
Clippy gate still reports existing Windows-only unused test imports/constants;
it is not reported as passing. No remote CI or CreatorEngine host was run.

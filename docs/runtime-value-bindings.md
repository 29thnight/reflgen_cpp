# Runtime value graphs for native scripting

The runtime descriptor layer can expose a component and the types reachable from
its bases, fields, method parameters and method results without generating a
separate managed wrapper for every record. It supplies C++ descriptors and checked
value operations. An engine remains responsible for object identity, threading,
ownership, authorization of internal operations and its native transport.

## Separate runtime and authoring schemas

Use the header generator's `public_methods` directive to add public instance
methods to an existing reflected type. Existing field selection and authoring
serialization stay unchanged. Public overloads receive exact member-pointer casts;
`ignore` continues to remove a member.

```cpp
class [[reflgen::reflect, reflgen::public_methods]] LightComponent
{
public:
    float intensity = 1.0f;
    void SetIntensity(float value);
    void SetIntensity(float value, bool notify);
    [[reflgen::script_internal]] void OnAddedToScene();
};
```

Use `runtime` for a previously unreflected value type whose members scripts need
to inspect. It selects public data and public instance methods and emits a
`runtime_schema<T>` specialization. The type does not become `reflectable<T>` or
gain an authoring serializer as a side effect.

```cpp
struct [[reflgen::runtime]] TerrainLayer
{
    std::string name;
    float tiling = 1.0f;
};

static_assert(reflgen::runtime_reflectable<TerrainLayer>);
static_assert(!reflgen::reflectable<TerrainLayer>);
```

`runtime("qualified.name")` sets the public schema name. `public_fields` is an
explicit public-field filter for an existing reflected declaration; use it only
when changing that declaration's field selection is intended. Runtime-only types
already use that filter. The header and IFC generators support these directives;
they are not an automatic transitive discovery policy or a claim of standard
reflection backend parity.

Handwritten adapters can specialize `runtime_schema<T>` with a normal
`schema<T>(...)`. `runtime_schema_of<T>` chooses that schema if present and
otherwise uses the existing authoring schema. The two schemas are independent,
so an explicit runtime schema should list all fields intended for runtime use.

Runtime-only public reference data members use
`accessor_field<Owner, Getter>.named("member")`; the generated getter returns the
original lvalue. This preserves reference identity without creating an invalid
C++ pointer to a reference data member. Whole-record replacement is disabled for
such records because copying them would retain aliases into the original object.

## Descriptors and live views

```cpp
const auto& type = reflgen::type_descriptor_of<Component>();
auto root = type.value().view(&component);
auto gain = root.select_member("settings").select_member("gain");

std::string text;
reflgen::json::writer output(text);
gain.serialize(output);

// Validate the entire bounded document before allowing a native write.
const std::string input_text = "0.75";
reflgen::json::reader validation(input_text);
validation.skip_value();
validation.finish();
reflgen::json::reader input(input_text);
gain.deserialize(input);
input.finish();
```

`value_descriptor` reports shape, child types, shape completeness, serialization
and assignment capabilities, and unsupported reasons. `value_type_closure(roots)` walks the type
graph with cycle detection and deterministic ordering. It keeps unsupported
types visible, so a consumer can distinguish an opaque type from an omitted one.
Use type IDs and declaring-type IDs for identity; a schema's public name can
deliberately differ from its canonical C++ spelling.

Views support record members, sequence/tuple indices, map keys and values, optional
and variant contents, and explicitly supported pointer-like targets. C++23
`expected<T,E>` exposes its active value or error. Read-only state propagates to
children. Map keys and proxy elements such as `vector<bool>` are read-only views.
For maps, `select_index(i)` selects the mapped value and `select_key(i)` selects
its key. An optional, variant, or expected exposes its active object at index zero;
an empty optional and successful `expected<void,E>` have no child storage.
Expected snapshots use `{"value": ...}` or `{"error": ...}`, with null for void
success, matching the existing optional `serial/expected.h` wire format.

A view is a native-operation-scoped borrow. Re-resolve an engine handle and its
entire member path before each request. Do not retain a data address over object
removal, vector growth, scene changes or a managed reload.

Custom `runtime_value_traits<T>` describe engine strings, containers and ownership
types. A pointer adapter must explicitly provide `get`, its child type, and
`mutable_borrow` consent. Persistent assignment and pointer-like method arguments
are refused unless their traits explicitly permit a safe representation. Custom
container `size`, `at` and `key` hooks support containers without STL aliases.
`size(const T&)` returns the element count; `at(const T&, size_t)` returns a native
const element reference, and an optional mutable overload permits child writes.
Map `key(const T&, size_t)` returns a native key reference that is always exposed
read-only. Declare their recursive types in `children = type_list<...>`: one type
for a sequence, or key and mapped types for a map. These navigation hooks alone
do not authorize container reconstruction. The default map snapshot for this
adapter is an array of `[key, value]` pairs.

Traits may provide `write(writer&, const T&)` and `read(reader&, T&)` to override
runtime serialization without replacing authoring serializers. A custom writer
takes precedence over the default snapshot format. Set `assignment_safe = true`
only when copying the value creates independent mutable state and committing with
a nonthrowing swap is safe. Pointer and owning wrappers normally keep it false.
Traits must be visible in every canonical registration translation unit; differing
trait visibility would create inconsistent inline descriptors.

For an engine whose authoring serializers execute lifecycle or asset hooks,
`runtime_value_traits<T>::prefer_record_fields = true` selects direct runtime
record traversal and recursive capability checks. It bypasses those authoring
serializers only in runtime value operations. The original `serialize`/YAML path
is unchanged. Pointer or owner wrappers whose authoring serializers also run
hooks should disable whole-wrapper runtime serialization and expose their checked
borrowed target instead.

## Method invocation and retained results

`method_info::invoke_serialized` reads and validates the complete argument array
before calling native code once. It returns a record containing `result` and the
post-call `arguments`, including mutable reference outputs. String views and
explicitly permitted C strings receive temporary owned backing for that call.
`std::span` arguments use invocation-local vector backing; fixed extents are
checked before calling native code and mutable elements are copied back into the
returned argument array. Boolean and volatile span elements are unsupported.
Assigning an entire persistent range view is disabled because copying the view
would still alias the original storage; select a writable element instead.
Pointers to arbitrary caller-selected addresses are never reconstructed from JSON.

The reader interface validates values as they are consumed; it cannot guarantee
that a caller's stream has no trailing document. Validate the entire bounded JSON
input, including `reader.finish()`, before invoking or assigning to native storage.
Calling `finish()` only after assignment or invocation is too late to prevent
side effects from a valid value followed by malformed trailing input. After the
native call begins, exceptions do not roll back arbitrary native side effects;
the transport must not retry a call automatically to obtain its response.

`script_internal` keeps a method in metadata while refusing erased calls,
serialized calls and result borrowing. Engine lifecycle, identity replacement,
ownership transfer and serialization callbacks are typical uses. It does not
affect ordinary C++ calls or authoring serialization.

`script_once` keeps explicit invocation available but disables both getter-borrow
capabilities. Annotate destructive zero-argument operations such as
`TakePreparedTextureImagePins()` with it: their pointer/owner return type must not
make them replayable during later path reads. Serialized and bound invocation
still execute the operation once and return its retained response.

`can_borrow_result()` identifies getters whose result can be inspected as a view.
For no-argument getters, lvalue-reference returns preserve the referent. Movable
value returns are retained in an owned, read-only snapshot. An owner wrapper can
explicitly opt into `retained_borrow` so its pointee remains navigable and mutable
according to the pointee's constness. A detached record return is never presented
as a writable alias of the engine's original record.

`borrow_result(object, reader&)` accepts an argument array for a const lookup such
as `FindStateByName(string_view) const`. Check
`can_borrow_result_with_arguments()` and `borrow_unavailable_reason()` before
offering this operation. The array is parsed and its arity checked before native
execution. As with serialized invocation, the transport must validate the entire
input document before supplying its argument reader. Mutable reference/output
parameters, rvalue references, mutable spans, native-address inputs and callback
inputs are excluded. Explicitly opted-in const character pointers use owned text
backing; string views, const spans and independent value arguments are also
supported.

Argument storage remains alive until the returned view and all its children are
released. Results from lookups with one or more arguments are always read-only,
including after pointer dereference: the result may alias temporary input storage
instead of persistent engine state. The core does not infer that a const method
is pure or returns an owner-backed object, so the engine must still exclude
stateful internal APIs. Passing an empty argument array to a no-argument getter
preserves that getter's existing mutable/const behavior.

When chaining a getter after an owned result, retain the parent view with
`value_view::retain_parent` as well as the returned storage. A reference into a
snapshot must not outlive its backing object during the request. Ordinary member,
index and dereference navigation propagates that storage automatically.

```cpp
auto next = getter.borrow_result(current.data()); // const getter on a readonly view
next.retain_parent(current);
current = std::move(next);
```

### Calls with resolved native arguments

`method_info::invoke_bound(receiver, arguments, native_arguments)` supports a
single synchronous native call with a mixture of serialized values and resolved
native views. Check `is_bound_invocable()` and `bound_unavailable_reason()`;
this capability is independent of serialized invocation and getter borrowing.
For example, a mutating `AddState(name)` can return a native pointer even though
it is neither a serialized-value call nor a const getter.

The receiver is a `value_view` of the exact type whose method table supplied the
descriptor. A non-const method requires a writable receiver. The optional native
binding span is either empty or has one entry per parameter. An empty entry
deserializes its JSON argument using the existing safe value rules. A valid entry
occupies an explicit JSON `null` placeholder and supplies a trusted native view
that the engine resolved from its own generation-checked handle and member path.
The library never interprets a number as an address, ignores no conflicting
serialized input, and performs no implicit numeric or derived-to-base conversion.

```cpp
// Engine-resolved state identity, not an address supplied by a script.
std::array native_arguments{state_view};
reflgen::json::reader arguments("[null]");
auto call = remove_state.invoke_bound(component_view, arguments, native_arguments);
arguments.finish(); // The transport must also validate the full input before the call.
```

A native `T&`/`const T&` argument requires an exact `T` view; mutable references
require writable storage. Copyable values can be copied from exact native views
without moving from the original object. A `T*`/`const T*` parameter accepts an
exact pointee view, or a pointer/owner view whose adapter declares exactly that
pointee type. Non-null targets are resolved through the explicit dereference
adapter and `T*` requires a writable target. Empty adapted owners preserve a typed
null; an exact typed null pointer view is accepted even without an adapter.
Unrelated or derived pointee types are not implicitly converted. An unbound JSON `null` does
not fabricate a native pointer. Void pointers, raw function pointers,
`std::function`, pointer-to-pointer
arguments, pointer-slot references, volatile arguments, consuming rvalue references
and noncopyable value arguments remain unavailable. Ownership transfer needs an
explicit engine operation with its own lifetime policy.

A host may resolve its own copyable callback wrapper to an exact native view.
Such a wrapper is an ordinary bound value; the host must implement rooted
registration, signature matching, cancellation and unload barriers. The runtime
does not deserialize callbacks or accept function addresses from JSON.

A readonly native owner, mutable-element span, or record containing unsafe
indirection cannot be copied or passed by const reference to recover writable
aliases. Those graphs require a writable binding even when the C++ parameter's
top-level type is const. Plain independently safe values and const-element spans
retain their normal readonly input support. This check also applies to readonly
records passed through a const object pointer.

The complete argument array, its arity, the receiver type, and every native
binding's type and constness are validated before the native method executes.
The transport must still validate the entire bounded document before calling,
including trailing input. Exceptions after native execution begins do not roll
back effects and must not cause an automatic retry.

The returned `method_call_result` contains `result` and `arguments`. A void result
is an empty view. Independently materialized arguments expose retained, read-only
post-call views, including mutable-reference outputs. Externally bound native
argument entries are deliberately empty: a method such as `RemoveState` may have
destroyed them, so constructing a response must not traverse those addresses.
Returned results retain receiver ownership, native argument ownership and
temporary string/span backing. Results with arguments are read-only, even when
the method is mutating or returns a mutable pointer; their target may alias
temporary input storage. No-argument results preserve the existing getter rules.

These views are scoped to the native request. Retaining a receiver or its backing
storage does not stop a method from deleting a child, reallocating a container,
or logically destroying an engine entity. The caller must preserve the native
API's aliasing and lifetime preconditions, reject destructive alias combinations,
and revalidate owner liveness after mutations. Do not persist a returned raw
pointer as a script handle. A persistent result requires an explicit engine
adapter that supplies independently validated object identity and lifetime.

The legacy runtime method list still requires an lvalue-invocable member
signature. Volatile, rvalue-qualified (`&&`), and C-varargs methods remain outside
that list. A source inventory must account for those exclusions when a consumer
claims to expose every public native API. Within the list, unsupported serialized
arguments/results and `script_internal` methods remain visible with capability
flags and a reason.

Writes stage a separate value before committing with a nonthrowing swap. Records
with read-only children, reference fields or unsafe pointer-bearing graphs refuse
whole-value replacement; select a writable child instead. The engine must still
perform its property-change notifications and revalidate object liveness after
native calls.

## CreatorEngine integration

CreatorEngine appends Execute/Read/Release entries to its injected native table.
Execute reserves a bounded response ticket before a potentially mutating call;
Read only copies that response, so buffer sizing cannot execute a method twice.
The F# layer builds paths and consumes copied JSON metadata. There is no C# runtime
binding assembly between F# and this native service.

CreatorEngine's native argument paths permit fields, container indices/keys and
dereferences. Getter segments are rejected before resolving any binding: even a
const C++ getter can invalidate an earlier argument's nested storage. The core
`invoke_bound` API can accept independently retained native views, but the engine
wire protocol deliberately does not infer such lifetime guarantees from a getter.
Mutating native return values are copied once within the Execute request and
arrive in F# as detached snapshots; persistent identity requires an engine adapter.

This general reflection path allocates and serializes values. Existing typed
native-table calls remain useful for per-frame hot paths. Complete Editor/Player
behavior, object lifetime and notification correctness need engine integration
tests in addition to this library's generated-code and runtime-value tests.

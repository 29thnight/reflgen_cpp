# Declaration manifest, version 1

`reflgen --declarations-json FILE` optionally writes one UTF-8 JSON file without a
BOM. It is a declaration interchange format for downstream tools. It is not a C++
ABI description, a serialization schema, or a promise that a declaration can be
exported to another language. The optional [interop pass](INTEROP.md) has its own
selection and validation rules.

The usual generated reflection headers, injection header and registration source
are unchanged by this option. Only classes, fields, methods and enums selected by
the existing reflection rules appear in the manifest. An interop-only class does
not become reflected and is not added to this manifest. Unsupported reflection
declarations retain their existing diagnostics; the option does not enable
overloads, static methods or templates.

## Root and ordering

The root object has these fields:

- `format`: always `"reflgen.declarations"`
- `version`: integer `1`; consumers must check both `format` and `version`
- `module`: the module identifier passed to the generator
- `target`: `{ "triple": string, "pointer_width": integer }` from the parsed
  libclang translation unit; pointer width is in bits
- `headers`: an array of header objects, sorted by normalized absolute `path`

A header object contains `path`, `classes` and `enums`. Declaration arrays retain
extraction order; field/method/parameter and attribute arrays retain their source
order. Reordering input headers does not reorder the header array. Output has no
timestamp and identical content is not rewritten. Paths remain absolute, so
determinism is for the same sources, paths, parse options and libclang behavior,
not byte identity across different checkouts or toolchains.

When discovery selects no input headers, `headers` is empty and no translation
unit is parsed: target triple is empty and pointer width is `0`. An explicitly
parsed header with no reflected declarations still has its header object and
empty declaration arrays.

All objects may acquire additional fields in a compatible version. Consumers
should ignore unknown fields, but must not silently reinterpret an unsupported
format version, type kind, qualifier state or calling convention.

## Declarations

A class object contains:

- `name`, `qualified_name`, `class_key` (`"class"` or `"struct"`)
- `schema_name`: the C++ expression used as the reflection registry name; this is
  not an evaluated string (for example, its JSON value may contain `"game.player"`
  including the C++ quotation marks)
- `source`: source position described below, and `nested`: whether it is nested
- `bases`: qualified names of the nearest reflected public ancestors, with the
  same selection as reflection generation; this is not a complete base-class graph
- `attributes`, `fields`, `methods`

A field object contains `name`, `owner` (fully qualified class), `access`
(`"public"`, `"protected"`, `"private"`, or `"none"`), `source`, `type`, and
`attributes`. Ignored fields and unsupported field forms are not included.

A method object contains:

- `name`, `owner`, `access`, `source`, `attributes`
- `signature`: libclang's C++ function-type spelling
- `return_type` and `parameters`; each parameter contains `name`, `type`, `source`
  and an unnamed parameter keeps the empty string
- `is_static`, `is_const`, `is_volatile`, `is_variadic`, `is_deleted`, `is_overloaded`
- `ref_qualifier`: `"none"`, `"lvalue"`, or `"rvalue"`
- `qualifiers_known`: whether source tokens establish the method cv suffix;
  `false` means the volatile flag is not conclusive, for example when a macro
  hides the declarator or its qualifiers
- `calling_convention`: `"default"`, `"c"`, `"x86_stdcall"`, `"x86_fastcall"`,
  `"x86_thiscall"`, `"win64"`, `"x86_64_sysv"`, `"x86_vectorcall"`, or
  `"unsupported:N"` containing the unhandled libclang C API value
- `exception_specification`: `"none"`, `"dynamic_none"`, `"dynamic"`, `"ms_any"`,
  `"basic_noexcept"`, `"computed_noexcept"`, `"unevaluated"`, `"uninstantiated"`,
  `"unparsed"`, `"nothrow"`, or `"unknown"`

Exception specifications are recorded, not evaluated by the generator. In
particular, `computed_noexcept` is not a guarantee that the method cannot throw.
The manifest is not a complete set of C++ declaration specifiers or attributes.
Static and overloaded methods remain excluded by the existing reflection rules;
their metadata flags do not change that selection.

An enum object contains `name`, `qualified_name`, `source`, `nested`, `scoped`,
`underlying_type`, and `enumerators` (names only, not evaluated integer values).
`underlying_type` is the canonical spelling of an explicitly written enum base;
an unwritten base is the empty string. Enumerator attributes are not supported by
the existing reflection model.

## Type records

Each type object contains:

- `spelling`: libclang spelling retaining aliases where exposed
- `canonical_spelling`: canonical C++ spelling with aliases resolved
- `kind`: one of `void`, `bool`, `signed_integer`, `unsigned_integer`, `character`,
  `floating_point`, `record`, `enum`, `pointer`, `lvalue_reference`,
  `rvalue_reference`, `member_pointer`, `array`, `function`, `nullptr`, `unsupported`
- `declaration`: qualified record/enum declaration name, otherwise empty
- `is_const`, `is_volatile`: top-level canonical qualifiers at this node
- `size_bytes`: libclang's size query, or `-1` when unknown/not applicable
- `pointee`: another type object for pointers/references/member pointers, otherwise
  `null`
- `element_type`: another type object for arrays, otherwise `null`
- `array_size`: the constant array bound, or `-1` if unavailable/not applicable

This recursive shape distinguishes `const T*` from `T* const` and `T&` from `T&&`.
Function pointees are identified as `function`; their full function signature is
in their spellings, not decomposed into another method object. Unsupported type
forms keep their spellings and are never silently normalized to a supported kind.

Sizes and canonical types are specific to the recorded parse target. Size is
the libclang C++ size query, not an interop storage layout: notably, the size query
for a reference describes its referent rather than a portable reference handle.
No record offsets, packing, inheritance layout, ownership, handle validity,
thread-safety or marshaling rules are supplied. A consumer must not infer ABI
compatibility from a matching size or target pointer width alone.

## Attributes and locations

An attribute object contains `scope`, `name`, `has_arguments`, `argument_tokens`,
`expression`, and `source`.

- `argument_tokens` is the flat array of original argument token spellings,
  excluding comments and the enclosing parentheses. Nested punctuation and
  string literals remain tokens; the generator does not evaluate or split C++
  expressions into semantic values
- `has_arguments` distinguishes a tag from an empty `()` argument list
- `expression` is the existing reflection-generation expression, including
  synthesized `{}` for a tag and qualifications such as `T::max_count`; it is not
  the raw argument text
- Only attributes carried by the existing allowed scopes are present. Generator
  directives such as `reflect`, `ignore`, `interop` and `lifetime` do not become
  runtime reflection attributes

A source position is `{ "file": string, "line": integer, "column": integer }`.
File paths use `/` separators and are absolute. Positions come from libclang or
the scanned attribute token; declaration extents may start before the name and
can differ between libclang versions. Consumers should use them for diagnostics,
not stable IDs. Source files must be valid in the caller's parse environment.

## Build integration and validation

Use CMake `DECLARATIONS_JSON FILE` and the `REFLGEN_DECLARATIONS_JSON` target
property, or MSBuild `ReflgenDeclarationsJson`. See the [README](README.md#optional-declaration-manifest)
for path rules, configuration isolation, dependencies and clean behavior.

The generator creates a requested parent directory, writes only changed content,
and rejects collisions with known input files, parsed include dependencies,
response/clang-argument files and other generated outputs. The manifest can be
omitted without removing or rewriting a previously requested file; build-system
clean owns removal of tracked outputs.

Serializer and generator-integration regression sources are in
`generator/tests/declarations_tests.cpp` and
`tests/generated/check_declarations.cmake`. They are executed only by the normal
test workflow, not by requesting a manifest.

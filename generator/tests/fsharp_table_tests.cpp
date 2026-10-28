#include "fsharp_table.h"
#include "harness.h"
#include <algorithm>
#include <string>
#include <utility>

namespace
{
    using namespace reflgen::generator;
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::require;
    using reflgen_test::test;
    const target_model linux_x64{"x86_64-unknown-linux-gnu", 64};
    const fsharp_table_options options{"sample", "Sample.Native"};

    declaration_type scalar(std::string name = "int", std::string kind = "signed_integer", long long size = 4)
    {
        declaration_type result;
        result.spelling = result.canonical_spelling = std::move(name);
        result.kind = std::move(kind);
        result.size_bytes = result.align_bytes = size;
        return result;
    }

    declaration_type pointer(declaration_type pointee)
    {
        auto result = scalar(pointee.canonical_spelling + "*", "pointer", 8);
        result.pointee = std::make_shared<declaration_type>(std::move(pointee));
        return result;
    }

    declaration_type record_type(const std::string& name, long long size, long long alignment)
    {
        auto result = scalar(name, "record", size);
        result.declaration = name;
        result.align_bytes = alignment;
        return result;
    }

    native_table_model example()
    {
        native_table_model table;
        table.alias = "sample::TableBinding";
        table.managed_name = "ScriptApi";
        table.version_field = "version";
        table.size_field = "structSize";
        table.version_expression = "::sample::ApiVersion";
        table.version = 46;
        table.position = {"/src/sample.h", 23, 1};
        table.table.target = record_type("sample::Table", 16, 8);
        table.table.align_bytes = 8;
        table.table.position = table.position;
        table.table.fields = {{"version", scalar(), 0, true, false}, {"structSize", scalar(), 4, true, false}};
        auto function = scalar("int (int)", "function", -1);
        function.function_result = std::make_shared<declaration_type>(scalar());
        function.function_parameters.push_back(scalar());
        function.function_has_prototype = true;
        function.function_calling_convention = "c";
        table.table.fields.push_back({"Add", pointer(std::move(function)), 8, true, false});
        return table;
    }

    bool contains(const std::string& text, const std::string& fragment)
    {
        return text.find(fragment) != std::string::npos;
    }

    template<class Change> void rejected(Change change, const std::string& code)
    {
        auto table = example();
        change(table);
        diagnostics report;
        const auto output = emit_fsharp_tables({table}, linux_x64, options, report);
        require(report.has_errors());
        check(std::any_of(report.entries().begin(), report.entries().end(), [&](const auto& entry) { return entry.code == code; }));
        check(output.fsharp_source.empty());
        check(output.cpp_source.empty());
        check(output.manifest_json.empty());
        check(output.fingerprint.empty());
    }

    const test direct_binding("F# tables: cached typed delegates, bounded headers and compiled native signatures", [] {
        diagnostics report;
        const auto output = emit_fsharp_tables({example()}, linux_x64, options, report);
        require(!report.has_errors());
        check(contains(output.fsharp_source, "type ``ScriptApi_Add_Delegate`` = delegate of int32 -> int32"));
        check(contains(output.fsharp_source, "static member Bind(address: nativeint, availableBytes: unativeint, fingerprint: uint64)"));
        const auto header_bound = output.fsharp_source.find("availableBytes < 8un");
        const auto header_read = output.fsharp_source.find("Marshal.ReadInt32");
        require(header_bound != std::string::npos && header_read != std::string::npos);
        check(header_bound < header_read);
        check(contains(output.fsharp_source, "Native table address range overflows"));
        check(contains(output.fsharp_source, "Required native function is null: Add"));
        check(contains(output.fsharp_source, "OSPlatform.Linux"));
        check(!contains(output.fsharp_source, "DllImport"));
        check(contains(output.cpp_source, "decltype(::sample::TableBinding::Add), int (REFLGEN_NATIVE_CDECL*)(int)"));
        check(contains(output.cpp_source, "static_assert(::sample::ApiVersion == 46"));
        check(contains(output.cpp_source, "offsetof(::sample::TableBinding, Add) == 8"));
        check(contains(output.cpp_source, "reflgen_sample_native_abi_fingerprint(void)"));
        check(contains(output.manifest_json, "\"format\":\"reflgen.fsharp-native\""));
        check(contains(output.manifest_json, "\"calling_convention\":\"c\""));
        check(contains(output.manifest_json, "\"version\":46"));
    });

    const test contract_identity("F# tables: same-width signatures, version, layout and target affect ABI identity", [] {
        auto table = example();
        diagnostics baseline_report;
        const auto baseline = emit_fsharp_tables({table}, linux_x64, options, baseline_report);
        require(!baseline_report.has_errors());
        table.table.fields.back().type.pointee->function_parameters.front() = scalar("unsigned int", "unsigned_integer");
        diagnostics signature_report;
        const auto signature = emit_fsharp_tables({table}, linux_x64, options, signature_report);
        require(!signature_report.has_errors());
        check(signature.fingerprint != baseline.fingerprint);
        table = example();
        table.version = 47;
        diagnostics version_report;
        check(emit_fsharp_tables({table}, linux_x64, options, version_report).fingerprint != baseline.fingerprint);
        table = example();
        diagnostics windows_report;
        check(emit_fsharp_tables({table}, {"x86_64-pc-windows-msvc", 64}, options, windows_report).fingerprint != baseline.fingerprint);
        require(!windows_report.has_errors());
    });

    const test invalid_tables("F# tables: malformed headers, ambiguous declarations and unsafe signatures fail atomically", [] {
        rejected([](auto& t) { t.table.fields[0].type = scalar("unsigned int", "unsigned_integer"); }, "RG0401");
        rejected([](auto& t) { t.table.fields[1].offset_bytes = 0; }, "RG0401");
        rejected([](auto& t) { t.table.fields[2].offset_bytes = 9; t.table.target.size_bytes = 24; }, "RG0401");
        rejected([](auto& t) { t.table.fields[2].is_public = false; }, "RG0401");
        rejected([](auto& t) { t.table.has_bases = true; }, "RG0401");
        rejected([](auto& t) { t.table.is_union = true; }, "RG0401");
        rejected([](auto& t) { t.table.fields[2].type.pointee->function_variadic = true; }, "RG0402");
        rejected([](auto& t) { t.table.fields[2].type.pointee->function_has_prototype = false; }, "RG0402");
        rejected([](auto& t) { t.table.fields[2].type.pointee->function_calling_convention = "x86_vectorcall"; }, "RG0402");
        rejected([](auto& t) { t.table.fields[2].type.pointee->function_exception_specification = "unsupported:computed"; }, "RG0402");
        rejected([](auto& t) { t.table.fields[2].type.pointee->function_parameters[0] = scalar("bool", "bool", 1); }, "RG0402");
        rejected([](auto& t) { t.table.fields[2].name = "Bind"; }, "RG0403");
        rejected([](auto& t) { t.version_expression = "1 + 45"; }, "RG0401");
        rejected([](auto& t) { t.version = -1; }, "RG0401");
        rejected([](auto& t) { t.managed_name = "bad.name"; }, "RG0400");
    });

    const test unsupported_targets("F# tables: unverified architectures are rejected rather than guessed", [] {
        for (const auto& target : {target_model{"aarch64-unknown-linux-gnu", 64}, target_model{"i686-pc-windows-msvc", 32},
                                   target_model{"x86_64-apple-darwin", 64}, target_model{"x86_64-pc-windows-msvc", 32}})
        {
            diagnostics report;
            const auto output = emit_fsharp_tables({example()}, target, options, report);
            require(report.has_errors());
            check(output.fsharp_source.empty());
        }
    });

    const test nested_inline_values("F# tables: nested values and fixed buffers are actual blittable F# structs", [] {
        auto table = example();
        value_model point;
        point.target = record_type("sample::Point", 8, 4);
        point.align_bytes = 4;
        point.fields = {{"x", scalar("float", "floating_point"), 0, true, false},
                        {"type", scalar("float", "floating_point"), 4, true, false}};
        point.position = table.position;
        value_model payload;
        payload.target = record_type("sample::Payload", 12, 4);
        payload.align_bytes = 4;
        payload.position = table.position;
        auto buffer = scalar("unsigned char[4]", "array", 4);
        buffer.align_bytes = 1;
        buffer.array_size = 4;
        buffer.element_type = std::make_shared<declaration_type>(scalar("unsigned char", "unsigned_integer", 1));
        payload.fields = {{"point", point.target, 0, true, false}, {"bytes", buffer, 8, true, false}};
        table.values = {payload, point};
        table.table.fields[2].type.pointee->function_parameters = {payload.target};
        diagnostics report;
        const auto output = emit_fsharp_tables({table}, linux_x64, options, report);
        require(!report.has_errors());
        check(contains(output.fsharp_source, "type ``sample_Point``"));
        check(contains(output.fsharp_source, "val mutable ``type``: float32"));
        check(contains(output.fsharp_source, "type ``sample_Payload_bytes_Array``"));
        check(contains(output.fsharp_source, "val mutable Item3: uint8"));
        check(!contains(output.fsharp_source, "ByValArray"));
        check(output.fsharp_source.find("type ``sample_Point``") < output.fsharp_source.find("type ``sample_Payload``"));
        check(contains(output.fsharp_source, "val mutable ``point``: ``sample_Point``"));
        check(contains(output.cpp_source, "std::is_trivially_copyable_v<::sample::Payload>"));
        auto reversed = table;
        std::reverse(reversed.values.begin(), reversed.values.end());
        diagnostics reordered_report;
        const auto reordered = emit_fsharp_tables({reversed}, linux_x64, options, reordered_report);
        check_equal(output.fingerprint, reordered.fingerprint);
        check_equal(output.fsharp_source, reordered.fsharp_source);
    });

    const test borrowed_pointer_values("F# tables: pointed-to POD declarations do not introduce false recursive value dependencies", [] {
        auto table = example();
        value_model a;
        a.target = record_type("sample::A", 8, 8);
        a.align_bytes = 8;
        a.position = table.position;
        value_model b;
        b.target = record_type("sample::B", 8, 8);
        b.align_bytes = 8;
        b.position = table.position;
        a.fields = {{"next", pointer(b.target), 0, true, false}};
        b.fields = {{"value", a.target, 0, true, false}};
        table.values = {a, b};
        table.table.fields[2].type.pointee->function_parameters = {a.target};
        diagnostics report;
        const auto output = emit_fsharp_tables({table}, linux_x64, options, report);
        require(!report.has_errors());
        check(output.fsharp_source.find("type ``sample_A``") < output.fsharp_source.find("type ``sample_B``"));
        check(contains(output.fsharp_source, "val mutable ``next``: nativeint"));
    });

    const test packed_rejection("F# tables: packed, over-aligned and non-POD by-value layouts are not inferred", [] {
        for (const long long alignment : {1LL, 8LL, 16LL})
            rejected([&](auto& t) {
                value_model value;
                value.target = record_type("sample::Bad", 16, alignment);
                value.align_bytes = alignment;
                value.fields = {{"value", scalar(), 0, true, false}};
                t.values = {value};
                t.table.fields[2].type.pointee->function_parameters = {value.target};
            }, "RG0402");
    });

    const test collision_rejection("F# tables: flattened native names cannot silently alias managed types", [] {
        rejected([](auto& t) {
            value_model first;
            first.target = record_type("a::b_c", 4, 4);
            first.align_bytes = 4;
            first.fields = {{"value", scalar(), 0, true, false}};
            auto second = first;
            second.target = record_type("a_b::c", 4, 4);
            t.values = {first, second};
            t.table.fields[2].type.pointee->function_parameters = {first.target, second.target};
        }, "RG0403");
    });

    const test callbacks("F# tables: typed callback factories contain exceptions and retain delegates until explicit disposal", [] {
        diagnostics report;
        const auto output = emit_fsharp_tables({example()}, linux_x64, options, report);
        require(!report.has_errors());
        check(contains(output.fsharp_source, "let ``root_ScriptApi_Add_callback`` (callback: int32 -> int32) (fallback: int32)"));
        check(contains(output.fsharp_source, "with error ->"));
        check(contains(output.fsharp_source, "member _.LastException = failure()"));
        check(contains(output.fsharp_source, "GCHandleType.Normal"));
        check(output.fsharp_source.find("Marshal.GetFunctionPointerForDelegate(callback)") < output.fsharp_source.find("GCHandle.Alloc(callback"));
    });

    const test native_primitive_contracts("F# tables: primitive signedness and noexcept function types are proven by the host compiler", [] {
        auto table = example();
        table.table.fields[2].type.pointee->function_exception_specification = "noexcept";
        table.table.fields[2].type.pointee->function_parameters[0] = scalar("char", "unsigned_integer", 1);
        diagnostics report;
        const auto output = emit_fsharp_tables({table}, linux_x64, options, report);
        require(!report.has_errors());
        check(contains(output.cpp_source, "int (REFLGEN_NATIVE_CDECL*)(char) noexcept"));
        check(contains(output.cpp_source, "std::numeric_limits<std::remove_cv_t<char>>::is_signed == false"));
        check(contains(output.manifest_json, "\"exception_specification\":\"noexcept\""));
    });

    const test empty_selection("F# tables: an empty opt-in emits a documented empty manifest without DLL imports", [] {
        diagnostics report;
        const auto output = emit_fsharp_tables({}, {}, options, report);
        require(!report.has_errors());
        check(contains(output.manifest_json, "\"tables\":[]"));
        check(contains(output.manifest_json, "\"values\":[]"));
        check(!contains(output.fsharp_source, "DllImport"));
        check(!output.fingerprint.empty());
    });
}

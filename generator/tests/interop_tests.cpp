// 선택적 primitive interop — 소스 계약 검사. 이 파일의 추가가 빌드나 실행 검증을 뜻하지는 않는다.
#include "interop.h"
#include "emit.h"
#include "harness.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace reflgen::generator;
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::require;
    using reflgen_test::test;

    const target_model linux_x64{"x86_64-unknown-linux-gnu", 64};
    const interop_options example_options{"sample", "sample_native", "Sample.Native"};

    void contains(const std::string& text, const std::string& expected)
    {
        check(text.find(expected) != std::string::npos, expected + " was not found in " + text);
    }

    void excludes(const std::string& text, const std::string& unexpected)
    {
        check(text.find(unexpected) == std::string::npos, unexpected + " was found in " + text);
    }

    attribute_use annotation(const std::string& name, const std::string& argument)
    {
        attribute_use result;
        result.scope = "reflgen";
        result.name = name;
        result.has_arguments = true;
        result.argument_tokens = {"\"" + argument + "\""};
        result.expression = "reflgen::" + name + "(" + result.argument_tokens.front() + ")";
        result.position = {"C:/src/counter.h", 7, 9};
        return result;
    }

    declaration_type primitive(const std::string& name, const std::string& kind, long long size)
    {
        declaration_type result;
        result.spelling = name;
        result.canonical_spelling = name;
        result.kind = kind;
        result.size_bytes = size;
        return result;
    }

    class_model example_class(const std::string& qualified_name = "game::counter")
    {
        class_model result;
        result.qualified_name = qualified_name;
        const std::size_t separator = qualified_name.rfind("::");
        result.name = separator == std::string::npos ? qualified_name : qualified_name.substr(separator + 2);
        result.class_key = "class";
        result.schema_name = "\"" + qualified_name + "\"";
        result.position = {"C:/src/counter.h", 5, 7};
        result.attributes = {annotation("interop", "csharp"), annotation("lifetime", "borrowed")};
        method_model method;
        method.name = "read";
        method.owner = qualified_name;
        method.access = "public";
        method.position = {"C:/src/counter.h", 10, 13};
        method.attributes = {annotation("interop", "csharp")};
        method.is_const = true;
        method.return_type = primitive("int", "signed_integer", 4);
        method.signature = "int () const";
        method.calling_convention = "c";
        method.exception_specification = "none";
        result.methods.push_back(std::move(method));
        return result;
    }

    bool has_code(const diagnostics& report, const std::string& code)
    {
        return std::any_of(report.entries().begin(), report.entries().end(),
                           [&](const diagnostic& entry) { return entry.code == code; });
    }

    template<class Change>
    void rejected(Change change, const std::string& code)
    {
        class_model model = example_class();
        change(model);
        diagnostics report;
        const interop_output output = emit_interop({model}, linux_x64, example_options, report);
        require(report.has_errors(), "invalid selected declaration was accepted");
        check(has_code(report, code), "expected diagnostic " + code);
        check(output.c_header.empty());
        check(output.cpp_source.empty());
        check(output.csharp_source.empty());
        check(output.fingerprint.empty());
        check(std::any_of(report.entries().begin(), report.entries().end(), [](const diagnostic& entry)
                          { return entry.position.file == "C:/src/counter.h" && entry.position.line != 0; }),
              "declaration diagnostics must retain source positions");
    }

    const test signatures("interop: C and C# share borrowed receivers, fixed values and status results", [] {
        class_model model = example_class();
        model.methods.front().parameter_declarations.push_back(
            {"enabled", primitive("bool", "bool", 1), {"C:/src/counter.h", 10, 18}});
        diagnostics report;
        const interop_output output = emit_interop({model}, linux_x64, example_options, report);
        require(!report.has_errors());
        contains(output.c_header, "#ifdef __cplusplus\nextern \"C\"");
        contains(output.c_header, "typedef struct reflgen_sample_game_counter_handle reflgen_sample_game_counter_handle;");
        contains(output.c_header, "reflgen_sample_game_counter_read(reflgen_sample_game_counter_handle* receiver, "
                                  "uint8_t argument_0, int32_t* result);");
        contains(output.c_header, "reflgen_sample_status_ok = 0");
        contains(output.c_header, "reflgen_sample_status_invalid_argument = 1");
        contains(output.c_header, "reflgen_sample_status_exception = 2");
        contains(output.c_header, "#undef REFLGEN_SAMPLE_INTEROP_CDECL");
        contains(output.cpp_source, "#include \"reflgen_sample.h\"\n#include \"reflgen_sample_interop.h\"");
        contains(output.cpp_source, "#include \"C:/src/counter.h\"");
        contains(output.cpp_source, "#if !(defined(__linux__)) || !(defined(__x86_64__) || defined(_M_X64))");
        contains(output.cpp_source, "#error reflgen interop: regenerate with the matching compiler --target");
        contains(output.cpp_source, "if (receiver == nullptr || result == nullptr)");
        contains(output.cpp_source, "(reinterpret_cast<::game::counter*>(receiver)->read)((argument_0 != 0))");
        contains(output.cpp_source, "*result = static_cast<int32_t>(value);");
        contains(output.cpp_source, "catch (...)\n    {\n        return reflgen_sample_status_exception;");
        check(output.cpp_source.find("if (receiver == nullptr || result == nullptr)") <
              output.cpp_source.find("reinterpret_cast<::game::counter*>"));
        contains(output.csharp_source, "namespace @Sample.@Native");
        contains(output.csharp_source, "EntryPoint = \"reflgen_sample_game_counter_read\", ExactSpelling = true");
        contains(output.csharp_source, "CallingConvention = global::System.Runtime.InteropServices.CallingConvention.Cdecl");
        contains(output.csharp_source, "public static extern int reflgen_sample_game_counter_read(nint receiver, "
                                       "byte argument_0, out int result);");
        excludes(output.csharp_source, "IDisposable");
        excludes(output.cpp_source, "delete ");
    });

    const test void_and_bool("interop: void uses status alone and bool returns a normalized byte", [] {
        class_model model = example_class();
        model.methods.front().return_type = primitive("void", "void", -1);
        diagnostics void_report;
        const interop_output void_output = emit_interop({model}, linux_x64, example_options, void_report);
        require(!void_report.has_errors());
        contains(void_output.c_header, "reflgen_sample_game_counter_read(reflgen_sample_game_counter_handle* receiver);");
        contains(void_output.cpp_source, "if (receiver == nullptr)");
        excludes(void_output.cpp_source, "result == nullptr");
        excludes(void_output.cpp_source, "*result =");
        contains(void_output.csharp_source, "public static extern int reflgen_sample_game_counter_read(nint receiver);");

        model.methods.front().return_type = primitive("bool", "bool", 1);
        diagnostics bool_report;
        const interop_output bool_output = emit_interop({model}, linux_x64, example_options, bool_report);
        require(!bool_report.has_errors());
        contains(bool_output.c_header, "uint8_t* result");
        contains(bool_output.cpp_source, "*result = static_cast<uint8_t>(value ? 1 : 0);");
        contains(bool_output.csharp_source, "out byte result");
    });

    const test primitive_widths("interop: supported integers and floating point map to blittable fixed widths", [] {
        struct example
        {
            const char* native;
            const char* kind;
            long long size;
            const char* c;
            const char* csharp;
        };
        const std::vector<example> examples = {
            {"signed char", "signed_integer", 1, "int8_t", "sbyte"},
            {"unsigned char", "unsigned_integer", 1, "uint8_t", "byte"},
            {"short", "signed_integer", 2, "int16_t", "short"},
            {"unsigned short", "unsigned_integer", 2, "uint16_t", "ushort"},
            {"int", "signed_integer", 4, "int32_t", "int"},
            {"unsigned int", "unsigned_integer", 4, "uint32_t", "uint"},
            {"long long", "signed_integer", 8, "int64_t", "long"},
            {"unsigned long long", "unsigned_integer", 8, "uint64_t", "ulong"},
            {"float", "floating_point", 4, "float", "float"},
            {"double", "floating_point", 8, "double", "double"}};
        for (const example& value : examples)
        {
            class_model model = example_class();
            model.methods.front().return_type = primitive(value.native, value.kind, value.size);
            diagnostics report;
            const interop_output output = emit_interop({model}, linux_x64, example_options, report);
            require(!report.has_errors(), value.native);
            contains(output.c_header, std::string(value.c) + "* result");
            contains(output.csharp_source, std::string("out ") + value.csharp + " result");
            contains(output.cpp_source, std::string("static_assert(sizeof(") + value.native + ") == " +
                                            std::to_string(value.size));
        }
    });

    const test target_dependent_scalars("interop: native aliases and char signedness are target checked", [] {
        class_model model = example_class();
        model.methods.front().return_type = primitive("long", "signed_integer", 4);
        model.methods.front().return_type.spelling = "counter_value";
        parameter_model parameter;
        parameter.type = primitive("char", "unsigned_integer", 1);
        model.methods.front().parameter_declarations.push_back(parameter);
        diagnostics report;
        const interop_output output =
            emit_interop({model}, {"x86_64-pc-windows-msvc", 64}, example_options, report);
        require(!report.has_errors());
        contains(output.c_header, "uint8_t argument_0, int32_t* result");
        contains(output.cpp_source, "static_assert(sizeof(long) == 4");
        contains(output.cpp_source, "#if !(defined(_WIN32)) || !(defined(__x86_64__) || defined(_M_X64))");
        contains(output.cpp_source, "::std::numeric_limits<char>::is_signed == false");
        contains(output.cpp_source, "static_cast<char>(argument_0)");
        excludes(output.cpp_source, "counter_value");
    });

    const test function_macro_names("interop: method calls do not expand function-like min or max macros", [] {
        class_model model = example_class();
        model.methods.front().name = "min";
        diagnostics report;
        const interop_output output = emit_interop({model}, linux_x64, example_options, report);
        require(!report.has_errors());
        contains(output.cpp_source, "(reinterpret_cast<::game::counter*>(receiver)->min)()");
        excludes(output.cpp_source, "->min(");
    });

    const test explicit_selection("interop: type selection does not expose unselected methods", [] {
        class_model model = example_class();
        method_model hidden = model.methods.front();
        hidden.name = "hidden";
        hidden.access = "private";
        hidden.attributes.clear();
        hidden.return_type = primitive("std::string", "record", 32);
        model.methods.push_back(hidden);
        diagnostics report;
        const interop_output output = emit_interop({model}, linux_x64, example_options, report);
        require(!report.has_errors());
        excludes(output.c_header, "_hidden(");
        excludes(output.csharp_source, "_hidden(");

        model.methods.front().attributes = {annotation("interop", "c")};
        diagnostics c_report;
        const interop_output c_output = emit_interop({model}, linux_x64, example_options, c_report);
        require(!c_report.has_errors());
        contains(c_output.c_header, "_read(");
        excludes(c_output.csharp_source, "_read(");
    });

    const test attributes("interop: target and borrowed policy grammar fails closed", [] {
        rejected([](class_model& model) { model.attributes.clear(); }, "RG0200");
        rejected([](class_model& model) { model.attributes.pop_back(); }, "RG0200");
        rejected([](class_model& model) { model.attributes.back() = annotation("lifetime", "owned"); }, "RG0200");
        rejected([](class_model& model) { model.attributes.push_back(model.attributes.front()); }, "RG0200");
        rejected([](class_model& model) { model.attributes.push_back(annotation("interop", "c")); }, "RG0200");
        rejected([](class_model& model) { model.attributes.front() = annotation("interop", "python"); }, "RG0200");
        rejected([](class_model& model) { model.attributes.front().has_arguments = false; }, "RG0200");
        rejected([](class_model& model) { model.attributes.front().argument_tokens = {"target_name"}; }, "RG0200");
        rejected([](class_model& model) { model.attributes.front().argument_tokens = {"\"c\"", "\"sharp\""}; }, "RG0200");
        rejected([](class_model& model) { model.attributes.front().argument_tokens = {"R\"(csharp)\""}; }, "RG0200");
        rejected([](class_model& model) { model.attributes.front().argument_tokens = {"\"c\\x73harp\""}; }, "RG0200");
        rejected([](class_model& model) { model.attributes.front() = annotation("interop", "c"); }, "RG0200");
        rejected([](class_model& model) { model.methods.front().attributes.push_back(annotation("lifetime", "borrowed")); },
                 "RG0200");
        rejected([](class_model& model) { model.methods.front().attributes.push_back(annotation("interop", "csharp")); },
                 "RG0200");
    });

    const test unsupported_methods("interop: unsafe or ambiguous selected members are rejected", [] {
        rejected([](class_model& model) { model.methods.front().access = "private"; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().access = "protected"; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().is_static = true; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().is_deleted = true; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().is_consteval = true; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().declaration_specifiers_known = false; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().is_overloaded = true; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().is_volatile = true; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().is_variadic = true; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().qualifiers_known = false; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().ref_qualifier = "lvalue"; }, "RG0201");
        rejected([](class_model& model) { model.methods.front().name = "operator+"; }, "RG0201");
        rejected([](class_model& model) { model.methods.push_back(model.methods.front()); }, "RG0201");
        rejected([](class_model& model) { model.nested = true; }, "RG0201");
        rejected([](class_model& model) { model.qualified_name = "game::counter<int>"; }, "RG0201");
        rejected([](class_model& model)
                 {
                     field_model field;
                     field.position = model.position;
                     field.attributes = {annotation("interop", "c")};
                     model.fields.push_back(field);
                 },
                 "RG0201");
    });

    const test unsupported_types("interop: records, references and noncontract primitive widths are rejected", [] {
        const std::vector<declaration_type> types = {
            primitive("std::string", "record", 32), primitive("game::value", "enum", 4),
            primitive("int*", "pointer", 8), primitive("int&", "lvalue_reference", 8),
            primitive("int&&", "rvalue_reference", 8), primitive("int[2]", "array", 8),
            primitive("void (*)()", "pointer", 8), primitive("long double", "floating_point", 8),
            primitive("wchar_t", "character", 4), primitive("char16_t", "character", 2),
            primitive("__int128", "signed_integer", 16), primitive("int", "signed_integer", -1),
            primitive("float", "floating_point", 8)};
        for (const declaration_type& type : types)
        {
            rejected([&](class_model& model) { model.methods.front().return_type = type; }, "RG0202");
        }
        rejected([](class_model& model) { model.methods.front().return_type.is_volatile = true; }, "RG0202");
        rejected([](class_model& model)
                 {
                     parameter_model parameter;
                     parameter.position = model.position;
                     parameter.type = primitive("void", "void", -1);
                     model.methods.front().parameter_declarations.push_back(parameter);
                 },
                 "RG0202");
    });

    const test target_validation("interop: only explicitly supported target combinations are accepted", [] {
        const std::vector<target_model> supported = {
            linux_x64, {"i686-unknown-linux-gnu", 32}, {"aarch64-unknown-linux-gnu", 64},
            {"x86_64-pc-windows-msvc", 64}, {"aarch64-pc-windows-msvc", 64},
            {"x86_64-apple-darwin", 64}, {"arm64-apple-macosx14.0.0", 64}};
        for (const target_model& target : supported)
        {
            diagnostics report;
            const interop_output output = emit_interop({example_class()}, target, example_options, report);
            check(!report.has_errors(), target.triple);
            check(!output.c_header.empty(), target.triple);
            if (target.triple.find("-apple-") != std::string::npos)
            {
                contains(output.cpp_source, "#include <TargetConditionals.h>");
                contains(output.cpp_source, "#if !TARGET_OS_OSX");
            }
        }
        const std::vector<target_model> unsupported = {
            {}, {"i686-pc-windows-msvc", 32}, {"wasm32-unknown-unknown", 32},
            {"x86_64-unknown-none", 64}, {"armv7-unknown-linux-gnueabihf", 32},
            {"x86_64-unknown-linux-gnu", 32}, {"arm64ec-pc-windows-msvc", 64}};
        for (const target_model& target : unsupported)
        {
            diagnostics report;
            const interop_output output = emit_interop({example_class()}, target, example_options, report);
            check(report.has_errors(), target.triple);
            check(has_code(report, "RG0202"), target.triple);
            check(output.c_header.empty(), target.triple);
        }
    });

    const test collisions("interop: flattened class and reserved infrastructure symbols cannot collide", [] {
        diagnostics report;
        emit_interop({example_class("a::b_c"), example_class("a_b::c")}, linux_x64, example_options, report);
        check(has_code(report, "RG0203"));
        rejected([](class_model& model)
                 {
                     model.qualified_name = "abi";
                     model.methods.front().name = "fingerprint";
                 },
                 "RG0203");
        rejected([](class_model& model) { model.methods.front().name = "handle"; }, "RG0203");
    });

    const test fingerprint("interop: ordered contract fingerprints are exposed on both sides", [] {
        class_model first = example_class("game::alpha");
        class_model second = example_class("game::zeta");
        method_model additional = first.methods.front();
        additional.name = "another";
        first.methods.push_back(additional);
        diagnostics forward_report;
        const interop_output forward = emit_interop({first, second}, linux_x64, example_options, forward_report);
        require(!forward_report.has_errors());
        std::reverse(first.methods.begin(), first.methods.end());
        diagnostics reverse_report;
        const interop_output reverse = emit_interop({second, first}, linux_x64, example_options, reverse_report);
        require(!reverse_report.has_errors());
        check_equal(forward.c_header, reverse.c_header);
        check_equal(forward.cpp_source, reverse.cpp_source);
        check_equal(forward.csharp_source, reverse.csharp_source);
        check_equal(forward.fingerprint, reverse.fingerprint);
        check_equal(forward.fingerprint.size(), std::size_t{16});
        contains(forward.cpp_source, "return 0x" + forward.fingerprint + "ULL;");
        contains(forward.csharp_source, "AbiFingerprint = 0x" + forward.fingerprint + "UL;");
        contains(forward.csharp_source, "public static extern ulong reflgen_sample_abi_fingerprint();");
        first.methods.front().return_type = primitive("double", "floating_point", 8);
        diagnostics changed_report;
        const interop_output changed = emit_interop({first, second}, linux_x64, example_options, changed_report);
        require(!changed_report.has_errors());
        check(forward.fingerprint != changed.fingerprint);
        diagnostics target_report;
        const interop_output changed_target =
            emit_interop({first, second}, {"aarch64-unknown-linux-gnu", 64}, example_options, target_report);
        require(!target_report.has_errors());
        check(changed.fingerprint != changed_target.fingerprint);
    });

    const test escaping("interop: namespace keywords and loader string escaping produce safe C#", [] {
        interop_options options = example_options;
        options.csharp_namespace = "class.CallingConvention";
        options.library_name = "path\\native\"library";
        diagnostics report;
        const interop_output output = emit_interop({example_class()}, linux_x64, options, report);
        require(!report.has_errors());
        contains(output.csharp_source, "namespace @class.@CallingConvention");
        contains(output.csharp_source, "LibraryName = \"path\\\\native\\\"library\";");
        contains(output.csharp_source, "[global::System.Runtime.InteropServices.DllImport(");
        for (const std::string& invalid : std::vector<std::string>{"broken\nname", "bad\xc2\x85name",
                                                                  "bad\xe2\x80\xa8name", "bad\xe2\x80\xa9name"})
        {
            options.library_name = invalid;
            diagnostics invalid_report;
            check(emit_interop({example_class()}, linux_x64, options, invalid_report).csharp_source.empty());
            check(has_code(invalid_report, "RG0200"));
        }
        options = example_options;
        options.csharp_namespace = "Sample..Native";
        diagnostics namespace_report;
        emit_interop({example_class()}, linux_x64, options, namespace_report);
        check(has_code(namespace_report, "RG0200"));
        options = example_options;
        options.module_name = "bad-module";
        diagnostics module_report;
        emit_interop({example_class()}, linux_x64, options, module_report);
        check(has_code(module_report, "RG0200"));
        rejected([](class_model& model) { model.position.file = "C:/src/\"counter.h"; }, "RG0201");
        rejected([](class_model& model) { model.position.file = "C:/src/line\xe2\x80\xa8name.h"; }, "RG0201");
    });

    const test empty_surfaces("interop: no exports and C-only types keep valid empty managed output", [] {
        diagnostics empty_report;
        const interop_output empty = emit_interop({}, {}, example_options, empty_report);
        require(!empty_report.has_errors());
        contains(empty.c_header, "reflgen_sample_abi_fingerprint(void);");
        contains(empty.cpp_source, "#include \"reflgen_sample.h\"");
        contains(empty.csharp_source, "// No C# types were selected for this module.");
        excludes(empty.c_header, "_handle");

        class_model model = example_class();
        model.attributes.front() = annotation("interop", "c");
        model.methods.front().attributes.front() = annotation("interop", "c");
        diagnostics c_report;
        const interop_output c_only = emit_interop({model}, linux_x64, example_options, c_report);
        require(!c_report.has_errors());
        contains(c_only.c_header, "_read(");
        contains(c_only.csharp_source, "// No C# types were selected for this module.");
        model.methods.clear();
        diagnostics marker_report;
        const interop_output marker_only = emit_interop({model}, linux_x64, example_options, marker_report);
        require(!marker_report.has_errors());
        contains(marker_only.c_header, "_handle;");
        excludes(marker_only.c_header, "_read(");
    });

    const test reflection_is_separate("interop: emitting exports does not mutate reflection or source models", [] {
        class_model reflected = example_class();
        reflected.attributes.clear();
        reflected.methods.front().attributes.clear();
        header_model header{"C:/src/counter.h", {reflected}, {}};
        const std::string before = emit_header(header, "C:/out/counter.reflgen.h");
        const class_model exported = example_class();
        const std::vector<class_model> exports{exported};
        diagnostics report;
        emit_interop(exports, linux_x64, example_options, report);
        require(!report.has_errors());
        check_equal(before, emit_header(header, "C:/out/counter.reflgen.h"));
        check_equal(exports.front().attributes.front().argument_tokens.front(), "\"csharp\"");
        check_equal(exports.front().methods.front().name, "read");
        check_equal(exports.front().methods.size(), std::size_t{1});
    });
} // namespace

// 선택적 선언 manifest — JSON escaping, 안정된 순서, 타입과 attribute 정보를 검사한다.
#include "declarations.h"
#include "harness.h"
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace reflgen::generator;
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::test;

    void contains(const std::string& text, const std::string& expected)
    {
        check(text.find(expected) != std::string::npos, expected + " was not found in " + text);
    }

    header_model example_header()
    {
        header_model header;
        header.path = "C:/src/sample.h";
        class_model type;
        type.name = "sample";
        type.qualified_name = "game::sample";
        type.class_key = "class";
        type.schema_name = "\"game.sample\"";
        type.position.file = header.path;
        type.position.line = 12;
        type.position.column = 7;
        header.classes.push_back(type);
        return header;
    }

    const test empty_manifest("declarations: an empty module has a version and explicit empty arrays", [] {
        const std::string text = emit_declarations_json("empty", {}, target_model{});
        contains(text, "\"format\":\"reflgen.declarations\"");
        contains(text, "\"version\":" + std::to_string(declarations_format_version));
        contains(text, "\"module\":\"empty\"");
        contains(text, "\"target\":{\"triple\":\"\",\"pointer_width\":0}");
        contains(text, "\"headers\":[]");
        check(text.starts_with('{'), text);
        check(text.ends_with("\n"), text);

        header_model header;
        header.path = "C:/src/empty.h";
        contains(emit_declarations_json("empty", {header}, target_model{}),
                 "\"headers\":[{\"path\":\"C:/src/empty.h\",\"classes\":[],\"enums\":[]}]");
    });

    const test json_escaping("declarations: strings escape quotes, backslashes and every control byte", [] {
        std::string value = "quote\" slash\\ controls:";
        for (unsigned char c = 0; c < 0x20; ++c)
        {
            value += static_cast<char>(c);
        }
        value += " 한글";
        const std::string escaped =
            R"(quote\" slash\\ controls:\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007)"
            R"(\u0008\u0009\u000a\u000b\u000c\u000d\u000e\u000f)"
            R"(\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017)"
            R"(\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f 한글)";
        header_model header = example_header();
        header.path = value;
        header.classes.front().schema_name = value;
        attribute_use attribute;
        attribute.scope = "game";
        attribute.name = "label";
        attribute.has_arguments = true;
        attribute.argument_tokens = {value};
        attribute.expression = value;
        header.classes.front().attributes.push_back(attribute);
        const std::string text = emit_declarations_json(value, {header}, target_model{});
        contains(text, "\"module\":\"" + escaped + '"');
        contains(text, "\"path\":\"" + escaped + '"');
        contains(text, "\"schema_name\":\"" + escaped + '"');
        contains(text, "\"argument_tokens\":[\"" + escaped + "\"]");
        contains(text, "\"expression\":\"" + escaped + '"');
        check(text.find('\0') == std::string::npos, "the JSON contains an unescaped NUL");
    });

    const test stable_order("declarations: header order is deterministic without changing the input", [] {
        header_model first = example_header();
        first.path = "C:/src/a.h";
        header_model last;
        last.path = "C:/src/z.h";
        const std::vector<header_model> reverse{last, first};
        const std::string text = emit_declarations_json("game", reverse, target_model{});
        check_equal(text, emit_declarations_json("game", {first, last}, target_model{}));
        check_equal(text, emit_declarations_json("game", reverse, target_model{}));
        check_equal(reverse.front().path, last.path);
        check(text.find("C:/src/a.h") < text.find("C:/src/z.h"), text);
    });

    const test typed_attributes("declarations: typed attributes retain raw tokens, expressions and positions", [] {
        header_model header = example_header();
        attribute_use limits;
        limits.scope = "game";
        limits.name = "limits";
        limits.has_arguments = true;
        limits.argument_tokens = {"0", ",", "max_count"};
        limits.expression = "game::limits(0, T::max_count)";
        limits.position.file = header.path;
        limits.position.line = 15;
        limits.position.column = 11;
        attribute_use marker;
        marker.scope = "game";
        marker.name = "marker";
        marker.expression = "game::marker{}";
        header.classes.front().attributes = {limits, marker};
        const std::string text = emit_declarations_json("game", {header}, target_model{});
        contains(text, "\"scope\":\"game\",\"name\":\"limits\",\"has_arguments\":true,"
                       "\"argument_tokens\":[\"0\",\",\",\"max_count\"]");
        contains(text, "\"expression\":\"game::limits(0, T::max_count)\","
                       "\"source\":{\"file\":\"C:/src/sample.h\",\"line\":15,\"column\":11}");
        contains(text, "\"scope\":\"game\",\"name\":\"marker\",\"has_arguments\":false,"
                       "\"argument_tokens\":[],\"expression\":\"game::marker{}\"");
    });

    const test type_and_method_metadata("declarations: recursive type nodes and method qualifiers are preserved", [] {
        header_model header = example_header();
        declaration_type scalar;
        scalar.spelling = "count_type";
        scalar.canonical_spelling = "unsigned int";
        scalar.kind = "unsigned_integer";
        scalar.size_bytes = 4;
        declaration_type record;
        record.spelling = "const volatile game::sample";
        record.canonical_spelling = record.spelling;
        record.kind = "record";
        record.declaration = "game::sample";
        record.is_const = true;
        record.is_volatile = true;
        declaration_type pointer;
        pointer.spelling = "const volatile game::sample *const";
        pointer.canonical_spelling = pointer.spelling;
        pointer.kind = "pointer";
        pointer.is_const = true;
        pointer.size_bytes = 8;
        pointer.pointee = std::make_shared<declaration_type>(record);
        field_model field;
        field.name = "observed";
        field.owner = "game::sample";
        field.access = "private";
        field.type = pointer;
        header.classes.front().fields.push_back(field);
        field_model lanes;
        lanes.name = "lanes";
        lanes.type.spelling = "count_type[3]";
        lanes.type.canonical_spelling = "unsigned int[3]";
        lanes.type.kind = "array";
        lanes.type.array_size = 3;
        lanes.type.element_type = std::make_shared<declaration_type>(scalar);
        header.classes.front().fields.push_back(lanes);

        method_model method;
        method.name = "read";
        method.owner = "game::sample";
        method.access = "public";
        method.signature = "count_type (count_type, ...) const volatile & noexcept";
        method.calling_convention = "c";
        method.exception_specification = "basic_noexcept";
        method.is_const = true;
        method.is_volatile = true;
        method.is_variadic = true;
        method.is_overloaded = true;
        method.is_deleted = true;
        method.ref_qualifier = "lvalue";
        method.return_type = scalar;
        parameter_model unnamed;
        unnamed.type = scalar;
        method.parameter_declarations.push_back(unnamed);
        header.classes.front().methods.push_back(method);
        target_model target;
        target.triple = "x86_64-test-none";
        target.pointer_width = 64;
        const std::string text = emit_declarations_json("game", {header}, target);
        contains(text, "\"target\":{\"triple\":\"x86_64-test-none\",\"pointer_width\":64}");
        contains(text, "\"name\":\"observed\",\"owner\":\"game::sample\",\"access\":\"private\"");
        contains(text, "\"kind\":\"pointer\",\"declaration\":\"\",\"is_const\":true,"
                       "\"is_volatile\":false,\"size_bytes\":8,\"pointee\":{");
        contains(text, "\"kind\":\"record\",\"declaration\":\"game::sample\",\"is_const\":true,"
                       "\"is_volatile\":true");
        contains(text, "\"element_type\":{\"spelling\":\"count_type\","
                       "\"canonical_spelling\":\"unsigned int\"");
        contains(text, "\"array_size\":3");
        contains(text, "\"calling_convention\":\"c\",\"exception_specification\":\"basic_noexcept\"");
        contains(text, "\"is_static\":false,\"is_const\":true,\"is_volatile\":true,\"is_variadic\":true");
        contains(text, "\"qualifiers_known\":true");
        contains(text, "\"is_overloaded\":true");
        contains(text, "\"is_deleted\":true");
        contains(text, "\"ref_qualifier\":\"lvalue\"");
        contains(text, "\"parameters\":[{\"name\":\"\",\"type\":{\"spelling\":\"count_type\"");
        contains(text, "\"pointee\":null,\"element_type\":null,\"array_size\":-1");
    });
} // namespace

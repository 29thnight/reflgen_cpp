#include "declarations.h"
#include <algorithm>
#include <string_view>

namespace reflgen::generator
{
    namespace
    {
        std::string quoted(std::string_view text)
        {
            constexpr char hex[] = "0123456789abcdef";
            std::string result = "\"";
            for (const unsigned char c : text)
            {
                if (c == '"' || c == '\\')
                {
                    result += '\\';
                    result += static_cast<char>(c);
                }
                else if (c < 0x20)
                {
                    result += "\\u00";
                    result += hex[c >> 4];
                    result += hex[c & 0x0f];
                }
                else
                {
                    result += static_cast<char>(c);
                }
            }
            return result + '"';
        }

        std::string boolean(bool value)
        {
            return value ? "true" : "false";
        }

        template<class Values, class Emit>
        std::string array(const Values& values, Emit emit)
        {
            std::string result = "[";
            bool first = true;
            for (const auto& value : values)
            {
                if (!first)
                {
                    result += ',';
                }
                first = false;
                result += emit(value);
            }
            return result + ']';
        }

        std::string source_json(const source_position& source)
        {
            return "{\"file\":" + quoted(source.file) + ",\"line\":" + std::to_string(source.line) +
                   ",\"column\":" + std::to_string(source.column) + '}';
        }

        std::string type_json(const declaration_type& type)
        {
            return "{\"spelling\":" + quoted(type.spelling) + ",\"canonical_spelling\":" +
                   quoted(type.canonical_spelling) + ",\"kind\":" + quoted(type.kind) + ",\"declaration\":" +
                   quoted(type.declaration) + ",\"is_const\":" + boolean(type.is_const) + ",\"is_volatile\":" +
                   boolean(type.is_volatile) + ",\"size_bytes\":" + std::to_string(type.size_bytes) +
                   ",\"pointee\":" + (type.pointee ? type_json(*type.pointee) : "null") + ",\"element_type\":" +
                   (type.element_type ? type_json(*type.element_type) : "null") + ",\"array_size\":" +
                   std::to_string(type.array_size) + '}';
        }

        std::string attribute_json(const attribute_use& attribute)
        {
            return "{\"scope\":" + quoted(attribute.scope) + ",\"name\":" + quoted(attribute.name) +
                   ",\"has_arguments\":" + boolean(attribute.has_arguments) + ",\"argument_tokens\":" +
                   array(attribute.argument_tokens, quoted) + ",\"expression\":" + quoted(attribute.expression) +
                   ",\"source\":" + source_json(attribute.position) + '}';
        }

        std::string field_json(const field_model& field)
        {
            return "{\"name\":" + quoted(field.name) + ",\"owner\":" + quoted(field.owner) + ",\"access\":" +
                   quoted(field.access) + ",\"source\":" + source_json(field.position) + ",\"type\":" +
                   type_json(field.type) + ",\"attributes\":" + array(field.attributes, attribute_json) + '}';
        }

        std::string parameter_json(const parameter_model& parameter)
        {
            return "{\"name\":" + quoted(parameter.name) + ",\"type\":" + type_json(parameter.type) +
                   ",\"source\":" + source_json(parameter.position) + '}';
        }

        std::string method_json(const method_model& method)
        {
            return "{\"name\":" + quoted(method.name) + ",\"owner\":" + quoted(method.owner) + ",\"access\":" +
                   quoted(method.access) + ",\"source\":" + source_json(method.position) + ",\"signature\":" +
                   quoted(method.signature) + ",\"calling_convention\":" + quoted(method.calling_convention) +
                   ",\"exception_specification\":" + quoted(method.exception_specification) + ",\"is_static\":" +
                   boolean(method.is_static) + ",\"is_const\":" + boolean(method.is_const) + ",\"is_volatile\":" +
                   boolean(method.is_volatile) + ",\"is_variadic\":" + boolean(method.is_variadic) +
                   ",\"qualifiers_known\":" + boolean(method.qualifiers_known) +
                   ",\"is_overloaded\":" + boolean(method.is_overloaded) + ",\"is_deleted\":" + boolean(method.is_deleted) +
                   ",\"ref_qualifier\":" + quoted(method.ref_qualifier) + ",\"return_type\":" +
                   type_json(method.return_type) + ",\"parameters\":" +
                   array(method.parameter_declarations, parameter_json) + ",\"attributes\":" +
                   array(method.attributes, attribute_json) + '}';
        }

        std::string class_json(const class_model& type)
        {
            return "{\"name\":" + quoted(type.name) + ",\"qualified_name\":" + quoted(type.qualified_name) +
                   ",\"class_key\":" + quoted(type.class_key) + ",\"schema_name\":" + quoted(type.schema_name) +
                   ",\"source\":" + source_json(type.position) + ",\"nested\":" + boolean(type.nested) +
                   ",\"bases\":" + array(type.bases, [](const type_reference& base) {
                       return quoted(base.qualified_name);
                   }) +
                   ",\"attributes\":" + array(type.attributes, attribute_json) + ",\"fields\":" +
                   array(type.fields, field_json) + ",\"methods\":" + array(type.methods, method_json) + '}';
        }

        std::string enum_json(const enum_model& type)
        {
            return "{\"name\":" + quoted(type.name) + ",\"qualified_name\":" + quoted(type.qualified_name) +
                   ",\"source\":" + source_json(type.position) + ",\"nested\":" + boolean(type.nested) +
                   ",\"scoped\":" + boolean(type.scoped) + ",\"underlying_type\":" + quoted(type.underlying_type) +
                   ",\"enumerators\":" + array(type.enumerators, quoted) + '}';
        }

        std::string header_json(const header_model& header)
        {
            return "{\"path\":" + quoted(header.path) + ",\"classes\":" + array(header.classes, class_json) +
                   ",\"enums\":" + array(header.enums, enum_json) + '}';
        }
    } // namespace

    std::string emit_declarations_json(const std::string& module_name, const std::vector<header_model>& headers,
                                       const target_model& target)
    {
        // 빌드 시스템이 입력 header 순서를 바꿔도 manifest 의 순서는 그대로 둔다.
        std::vector<const header_model*> ordered;
        for (const header_model& header : headers)
        {
            ordered.push_back(&header);
        }
        std::ranges::sort(ordered, [](const header_model* left, const header_model* right) {
            return left->path < right->path;
        });
        return "{\n  \"format\":\"reflgen.declarations\",\n  \"version\":" +
               std::to_string(declarations_format_version) + ",\n  \"module\":" + quoted(module_name) +
               ",\n  \"target\":{\"triple\":" + quoted(target.triple) + ",\"pointer_width\":" +
               std::to_string(target.pointer_width) + "},\n  \"headers\":" +
               array(ordered, [](const header_model* header) { return header_json(*header); }) + "\n}\n";
    }
} // namespace reflgen::generator

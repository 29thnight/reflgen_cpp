// IFC supplies semantic declarations; optional MSVC /P metadata preserves tool attributes.
#include "emit.h"
#include "metadata.h"
#include <ifc/reader.hxx>
#include <ifc/tooling.hxx>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

void ifc_assert(const char* text, const char*, int)
{
    throw std::runtime_error(std::string("invalid IFC: ") + text);
}

namespace
{
    using namespace ifc;
    using namespace ifc::symbolic;
    using namespace reflgen::generator;

    struct diagnostic_failure : std::runtime_error
    {
        source_position source;
        diagnostic_failure(source_position location, const std::string& message)
            : std::runtime_error(message), source(std::move(location))
        {
        }
    };

    bool has(BasicSpecifiers flags, BasicSpecifiers bit)
    {
        return (to_underlying(flags) & to_underlying(bit)) != 0;
    }

    bool identifier(std::string_view text)
    {
        if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_'))
            return false;
        return std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }

    struct extractor
    {
        Reader& reader;
        header_model model;
        module_metadata* metadata = nullptr;
        std::set<std::string> scopes{"reflgen"};

        std::vector<attribute_group> attributes(SourceLocation locus, std::string_view member, bool type = false)
        {
            if (!metadata)
                return {};
            try
            {
                return metadata->declaration(position(locus), member, type);
            }
            catch (const std::exception& e)
            {
                fail(locus, e.what());
            }
        }

        bool ignored(const std::vector<attribute_group>& groups)
        {
            return metadata && metadata->directive(groups, "ignore");
        }

        std::vector<attribute_use> uses(const std::vector<attribute_group>& groups, const qualifier_lookup& lookup = {})
        {
            return metadata ? metadata->uses(groups, scopes, lookup) : std::vector<attribute_use>{};
        }

        std::string name(NameIndex index)
        {
            if (index.sort() == NameSort::Identifier)
                return reader.get(TextOffset(to_underlying(index.index())));
            return {}; // operators, conversions and specializations are outside this first backend.
        }

        source_position position(SourceLocation locus)
        {
            if (index_like::null(locus.line))
                return {};
            const auto& line = reader.get(locus.line);
            std::string file;
            if (line.file.sort() == NameSort::SourceFile)
                file = reader.get(reader.get<SourceFileName>(line.file).name);
            std::replace(file.begin(), file.end(), '\\', '/');
            return {file, static_cast<unsigned>(to_underlying(line.line)),
                    static_cast<unsigned>(to_underlying(locus.column))};
        }

        [[noreturn]] void fail(SourceLocation locus, const std::string& message)
        {
            throw diagnostic_failure(position(locus), message);
        }

        void members(const ScopeDecl& scope, class_model& result)
        {
            const auto* body = reader.try_get(scope.initializer);
            if (!body)
                return;
            std::map<std::string, int> method_counts;
            std::set<std::string> constants;
            for (const auto& entry : reader.sequence(*body))
                if (entry.index.sort() == DeclSort::Variable)
                    constants.insert(name(reader.get<VariableDecl>(entry.index).identity.name));
            const qualifier_lookup lookup = [&constants](std::string_view value) {
                return constants.contains(std::string(value)) ? "T::" : "";
            };
            for (const auto& entry : reader.sequence(*body))
            {
                if (entry.index.sort() == DeclSort::Field)
                {
                    const auto& field = reader.get<FieldDecl>(entry.index);
                    if (field.access != Access::Public)
                        continue;
                    const std::string field_name = reader.get(field.identity.name);
                    const auto groups = attributes(field.identity.locus, field_name);
                    if (ignored(groups))
                        continue;
                    if (field.type.sort() == TypeSort::LvalueReference ||
                        field.type.sort() == TypeSort::RvalueReference)
                        fail(field.identity.locus, "reference field is unsupported: " + result.qualified_name);
                    if (!identifier(field_name))
                        fail(field.identity.locus, "anonymous field is unsupported: " + result.qualified_name);
                    result.fields.push_back({field_name, position(field.identity.locus), uses(groups, lookup)});
                }
                else if (entry.index.sort() == DeclSort::Bitfield)
                {
                    const auto& bit = reader.get<BitfieldDecl>(entry.index);
                    if (bit.access == Access::Public &&
                        !ignored(attributes(bit.identity.locus, reader.get(bit.identity.name))))
                        fail(bit.identity.locus, "public bit-field is unsupported: " + result.qualified_name);
                }
                else if (entry.index.sort() == DeclSort::Method)
                {
                    const auto& method = reader.get<NonStaticMemberFunctionDecl>(entry.index);
                    const std::string method_name = name(method.identity.name);
                    if (!identifier(method_name))
                        continue;
                    const auto groups = attributes(method.identity.locus, method_name, true);
                    if (ignored(groups))
                        continue;
                    if (identifier(method_name))
                        ++method_counts[method_name];
                    if (method.access == Access::Public && identifier(method_name))
                    {
                        result.methods.push_back(
                            {method_name, {}, position(method.identity.locus), uses(groups, lookup)});
                    }
                }
                else if (entry.index.sort() == DeclSort::Scope)
                {
                    const auto& nested = reader.get<ScopeDecl>(entry.index);
                    if (nested.access == Access::Public)
                        fail(nested.identity.locus, "public nested type is unsupported: " + result.qualified_name);
                }
                else if (entry.index.sort() == DeclSort::Enumeration)
                {
                    const auto& nested = reader.get<EnumerationDecl>(entry.index);
                    if (nested.access == Access::Public)
                        fail(nested.identity.locus, "public nested enum is unsupported: " + result.qualified_name);
                }
            }
            for (const auto& [method, count] : method_counts)
                if (count > 1 &&
                    std::ranges::any_of(result.methods, [&method](const auto& item) { return item.name == method; }))
                    fail(scope.identity.locus,
                         "overloaded method is unsupported: " + result.qualified_name + "::" + method);
        }

        void walk(ScopeIndex index, const std::string& prefix = {})
        {
            const auto* scope = reader.try_get(index);
            if (!scope)
                return;
            for (const auto& entry : reader.sequence(*scope))
            {
                if (entry.index.sort() == DeclSort::Enumeration)
                {
                    const auto& type = reader.get<EnumerationDecl>(entry.index);
                    if (has(type.basic_spec, BasicSpecifiers::NonExported) ||
                        has(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule))
                        continue;
                    enum_model result;
                    result.name = reader.get(type.identity.name);
                    if (!identifier(result.name))
                        continue;
                    result.qualified_name = prefix + result.name;
                    result.position = position(type.identity.locus);
                    if (ignored(attributes(type.identity.locus, result.name, true)))
                        continue;
                    for (const auto& value : reader.sequence(type.initializer))
                        result.enumerators.emplace_back(reader.get(value.identity.name));
                    model.enums.push_back(std::move(result));
                }
                else if (entry.index.sort() == DeclSort::Scope)
                {
                    const auto& type = reader.get<ScopeDecl>(entry.index);
                    const auto* fundamental = reader.get_if<FundamentalType>(type.type);
                    if (!fundamental)
                        continue;
                    const std::string short_name = name(type.identity.name);
                    if (!identifier(short_name) || has(type.basic_spec, BasicSpecifiers::IsMemberOfGlobalModule))
                        continue;
                    if (fundamental->basis == TypeBasis::Namespace)
                        walk(type.initializer, prefix + short_name + "::");
                    else if (fundamental->basis == TypeBasis::Struct || fundamental->basis == TypeBasis::Class)
                    {
                        if (has(type.basic_spec, BasicSpecifiers::NonExported) || !reader.try_get(type.initializer))
                            continue;
                        const auto groups = attributes(type.identity.locus, short_name, true);
                        if (ignored(groups) || (metadata && metadata->directive(groups, "attribute")))
                            continue;
                        if (!index_like::null(type.base))
                            fail(type.identity.locus,
                                 "inheritance is unsupported by the initial IFC backend: " + prefix + short_name);
                        class_model result;
                        result.name = short_name;
                        result.qualified_name = prefix + short_name;
                        result.schema_name = "\"" + result.qualified_name + "\"";
                        result.position = position(type.identity.locus);
                        result.attributes = uses(groups);
                        if (metadata)
                            if (const auto* reflect = metadata->directive(groups, "reflect");
                                reflect && reflect->has_arguments)
                                result.schema_name = metadata->arguments(*reflect);
                        members(type, result);
                        model.classes.push_back(std::move(result));
                    }
                }
            }
        }
    };

    void write_changed(const std::filesystem::path& path, const std::string& text)
    {
        std::ifstream old(path, std::ios::binary);
        if (old && std::string(std::istreambuf_iterator<char>(old), {}) == text)
            return;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        if (!output)
            throw std::runtime_error("cannot write " + path.string());
    }
} // namespace

int main(int argc, char** argv)
{
    std::string input, module, registration, directory, metadata_path;
    std::set<std::string> scopes{"reflgen"};
    std::vector<std::string> attribute_imports;
    const char* stage = "arguments";
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (i + 1 == argc)
                throw std::runtime_error("missing option value");
            const std::string value = argv[++i];
            if (option == "--ifc")
                input = value;
            else if (option == "--import")
                module = value;
            else if (option == "--module")
                registration = value;
            else if (option == "--output")
                directory = value;
            else if (option == "--metadata")
                metadata_path = value;
            else if (option == "--attribute-scope")
            {
                if (!identifier(value))
                    throw std::runtime_error("attribute scope must be an identifier");
                scopes.insert(value);
            }
            else if (option == "--attribute-import")
            {
                if (value.empty() || !std::ranges::all_of(value, [](unsigned char c) {
                        return std::isalnum(c) || c == '_' || c == '.';
                    }))
                    throw std::runtime_error("invalid attribute module name");
                attribute_imports.push_back(value);
            }
            else
                throw std::runtime_error("unknown option: " + option);
        }
        if (input.empty() || directory.empty() || !identifier(registration))
            throw std::runtime_error(
                "usage: reflgen-ifc --ifc FILE --import MODULE --module REGISTRATION --output DIR "
                "[--metadata MSVC_PREPROCESSED_FILE] [--attribute-scope NAMESPACE] [--attribute-import MODULE]");
        stage = "open";
        ifc::tool::InputFile container{std::filesystem::path(input)};
        InputIfc file{container.contents()};
        const auto u8path = std::filesystem::path(input).u8string();
        stage = "validate";
        if (!file.validate<UnitSort::Primary>(Pathname{u8path.c_str()}, Architecture::Unknown, Pathname{},
                                              IfcOptions(to_underlying(IfcOptions::IntegrityCheck) |
                                                         to_underlying(IfcOptions::AllowAnyPrimaryInterface))))
            throw std::runtime_error("not an IFC primary module interface");
        const auto actual_module = std::string(file.get(file.header()->unit.module_name()));
        if (module.empty()) module = actual_module;
        if (file.header()->unit.sort() != UnitSort::Primary)
            throw std::runtime_error("only primary named module interfaces are supported");
        if (module != actual_module)
            throw std::runtime_error("module mismatch: expected " + module + ", IFC contains " + actual_module);
        stage = "read partitions";
        Reader reader = [&file] {
            try
            {
                return Reader(file);
            }
            catch (const InvalidPartitionName& error)
            {
                throw std::runtime_error("unsupported IFC partition: " + std::string(error.name));
            }
        }();
        std::unique_ptr<module_metadata> metadata;
        if (!metadata_path.empty())
        {
            metadata = std::make_unique<module_metadata>(metadata_path);
            if (metadata->module() != module)
                throw std::runtime_error("metadata module mismatch");
            if (std::filesystem::last_write_time(metadata_path) < std::filesystem::last_write_time(input))
                throw std::runtime_error("metadata predates IFC; regenerate MSVC /P output after compiling the module");
        }
        extractor extract{reader, {input, {}, {}}, metadata.get(), scopes};
        stage = "extract";
        extract.walk(file.header()->global_scope);
        const auto output = std::filesystem::absolute(directory);
        std::filesystem::create_directories(output);
        const std::string stem = "reflgen_" + registration;
        const auto header = output / (stem + ".h");
        const std::string header_text =
            emit_ifc_header(extract.model, module, registration, header.generic_string(), attribute_imports);
        // Keep registration source tied to the complete schema, even when its list of types is unchanged.
        std::uint64_t fingerprint = 14695981039346656037ull;
        for (const unsigned char byte : header_text)
            fingerprint = (fingerprint ^ byte) * 1099511628211ull;
        std::string source =
            "// <auto-generated/> Generated by reflgen IFC\n// schema: " + std::to_string(fingerprint) +
            "\n#include \"reflgen/runtime/registry.h\"\nimport " + module + ";\n#include \"" + stem +
            ".h\"\nnamespace reflgen::generated {\nvoid register_" + registration + "(::reflgen::registry& target) {\n";
        for (const auto& type : extract.model.classes)
            source += "    target.add<::" + type.qualified_name + ">();\n";
        source += "    static_cast<void>(target);\n}\nvoid register_" + registration + "() { register_" + registration +
                  "(::reflgen::default_registry()); }\n}\n";
        // Do not publish partial output if IFC extraction or code generation failed.
        write_changed(header, header_text);
        write_changed(output / (stem + ".cpp"), source);
        write_changed(output / (stem + ".attributes.tsv"),
                      metadata ? metadata->catalog(scopes) : "reflgen-attributes\t1\n");
        std::cout << "reflgen-ifc: " << module << ": " << extract.model.classes.size() << " class(es), "
                  << extract.model.enums.size() << " enum(s)\n";
        return 0;
    }
    catch (const diagnostic_failure& error)
    {
        std::cerr << error.source.file << '(' << error.source.line << ',' << error.source.column
                  << "): error RG0200: " << error.what() << '\n';
    }
    catch (const IntegrityCheckFailed&)
    {
        std::cerr << input << "(1,1): error RG0200: IFC integrity check failed\n";
    }
    catch (const UnsupportedFormatVersion&)
    {
        std::cerr << input << "(1,1): error RG0200: unsupported IFC version; rebuild with a compatible MSVC/IFC SDK\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << input << "(1,1): error RG0200: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << input << "(1,1): error RG0200: invalid or incompatible IFC (" << stage << ")\n";
    }
    return 1;
}

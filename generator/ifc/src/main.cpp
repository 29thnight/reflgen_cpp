// IFC supplies semantic declarations; optional MSVC /P metadata preserves tool attributes.
#include "emit.h"
#include "extractor.h"
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

    using namespace reflgen::generator::ifc_frontend;

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
    std::vector<std::string> registration_headers;
    std::vector<std::string> registration_imports;
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
            else if (option == "--registration-header")
            {
                if (value.empty() || value.find_first_of("\"\r\n") != std::string::npos)
                    throw std::runtime_error("invalid registration header path");
                std::string header = value;
                std::replace(header.begin(), header.end(), '\\', '/');
                registration_headers.push_back(std::move(header));
            }
            else if (option == "--attribute-import" || option == "--registration-import")
            {
                if (value.empty() || !std::ranges::all_of(value, [](unsigned char c) {
                        return std::isalnum(c) || c == '_' || c == '.';
                    }))
                    throw std::runtime_error("invalid attribute module name");
                if (option == "--attribute-import") attribute_imports.push_back(value);
                else registration_imports.push_back(value);
            }
            else
                throw std::runtime_error("unknown option: " + option);
        }
        if (input.empty() || directory.empty() || !identifier(registration))
            throw std::runtime_error(
                "usage: reflgen-ifc --ifc FILE --import MODULE --module REGISTRATION --output DIR "
                "[--metadata MSVC_PREPROCESSED_FILE] [--attribute-scope NAMESPACE] [--attribute-import MODULE] "
                "[--registration-header HEADER] [--registration-import MODULE]");
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
        extractor extract{reader, input, metadata.get(), scopes};
        stage = "extract";
        extract.walk(file.header()->global_scope);
        const auto output = std::filesystem::absolute(directory);
        std::filesystem::create_directories(output);
        const std::string stem = "reflgen_" + registration;
        const auto header = output / (stem + ".h");
        const std::string header_text =
            emit_ifc_header(extract.model, module, registration, header.generic_string(), attribute_imports);
        const std::string source = emit_ifc_source(extract.model, module, registration, header_text,
                                                   registration_headers, registration_imports);
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


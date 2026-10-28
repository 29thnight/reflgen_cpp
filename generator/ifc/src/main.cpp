// IFC supplies semantic declarations; optional MSVC /P metadata preserves tool attributes.
#include "emit.h"
#include "extractor.h"
#include "prsm.h"
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
#include <type_traits>

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

    std::string collision_key(const std::filesystem::path& path)
    {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(path, error);
        if (error) canonical = std::filesystem::absolute(path).lexically_normal();
        auto key = canonical.generic_string();
#if defined(_WIN32)
        std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        return key;
    }

    void protect_inputs(const std::vector<std::filesystem::path>& outputs,
                        const std::vector<std::filesystem::path>& inputs)
    {
        for (const auto& output : outputs)
        {
            const auto output_key = collision_key(output);
            for (const auto& input : inputs)
            {
                if (input.empty()) continue;
                std::error_code error;
                if (std::filesystem::equivalent(output, input, error) || output_key == collision_key(input))
                    throw std::runtime_error("IFC output collides with an input: " + output.string());
            }
        }
    }
} // namespace

int main(int argc, char** argv)
{
    std::string input, module, registration, directory, metadata_path, prsm_adapter, interface_module;
    bool prsm = false;
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
            if (option == "--prsm")
            {
                prsm = true;
                continue;
            }
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
            else if (option == "--module-interface")
                interface_module = value;
            else if (option == "--metadata")
                metadata_path = value;
            else if (option == "--prsm-adapter-header")
                prsm_adapter = value;
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
                "[--registration-header HEADER] [--registration-import MODULE] "
                "[--module-interface MODULE] [--prsm --prsm-adapter-header FILE]");
        if (!interface_module.empty())
        {
            std::size_t begin = 0;
            while (begin < interface_module.size())
            {
                const auto end = interface_module.find('.', begin);
                const auto part = interface_module.substr(begin, end == std::string::npos ? end : end - begin);
                if (!identifier(part)) throw std::runtime_error("invalid generated module name");
                if (end == std::string::npos) break;
                begin = end + 1;
                if (begin == interface_module.size()) throw std::runtime_error("invalid generated module name");
            }
            if (prsm) throw std::runtime_error("PrSM module-interface output is unsupported");
            if (!registration_headers.empty() || !registration_imports.empty())
                throw std::runtime_error("module-interface output does not support registration-header/import; use header/source output");
        }
        stage = "open";
        if (prsm && (metadata_path.empty() || prsm_adapter.empty()))
            throw std::runtime_error("--prsm requires --metadata and --prsm-adapter-header");
        if (!prsm && !prsm_adapter.empty())
            throw std::runtime_error("--prsm-adapter-header requires --prsm");
        if (prsm)
        {
            if (!std::filesystem::is_regular_file(prsm_adapter))
                throw std::runtime_error("--prsm-adapter-header must name an existing file");
            prsm_adapter = std::filesystem::absolute(prsm_adapter).lexically_normal().generic_string();
        }
        ifc::tool::InputFile container{std::filesystem::path(input)};
        InputIfc file{container.contents()};
        const auto u8path = std::filesystem::path(input).u8string();
        stage = "validate";
        if (!file.validate<UnitSort::Primary>(Pathname{u8path.c_str()}, Architecture::Unknown, Pathname{},
                                              IfcOptions(to_underlying(IfcOptions::IntegrityCheck) |
                                                         to_underlying(IfcOptions::AllowAnyPrimaryInterface))))
            throw std::runtime_error("not an IFC primary module interface");
        const auto actual_module = std::string(file.get(file.header()->unit.module_name()));
        if (module.empty())
            module = actual_module;
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
        extract.prsm = prsm;
        stage = "extract";
        extract.walk(file.header()->global_scope);
        std::optional<prsm_output> bridge;
        if (prsm)
        {
            metadata->validate_prsm_selection();
            if (file.header()->arch != Architecture::X64)
                throw std::runtime_error("PrSM IFC currently requires Windows x64");
            diagnostics report;
            auto bindings = lower_prsm(extract.exports, {"x86_64-pc-windows-msvc", 64}, registration,
                                       {{binding_source_kind::named_module, module}}, report);
            if (bindings)
                bridge = emit_prsm(*bindings, prsm_adapter, report);
            if (report.has_errors())
            {
                report.print(stderr);
                return 1;
            }
        }
        const auto output = std::filesystem::absolute(directory);
        std::filesystem::create_directories(output);
        const std::string stem = "reflgen_" + registration;
        // Validate the entire output set before publishing even the first file. The IFC source-path
        // record survives empty/all-ignored modules, where extracted reflection positions do not.
        std::vector<std::filesystem::path> inputs{input, metadata_path, prsm_adapter};
        if (!index_like::null(file.header()->src_path)) inputs.emplace_back(file.get(file.header()->src_path));
        for (const auto& source_file : reader.partition<SourceFileName>())
            // MSVC includes a sentinel source-file entry with a null TextOffset. The SDK maps
            // that value to nullptr, which is not a valid filesystem::path constructor argument.
            if (!index_like::null(source_file.name)) inputs.emplace_back(reader.get(source_file.name));
        for (const auto& source : registration_headers) inputs.emplace_back(source);
        for (const auto& type : extract.model.classes)
        {
            inputs.emplace_back(type.position.file);
            for (const auto& field : type.fields) inputs.emplace_back(field.position.file);
            for (const auto& method : type.methods) inputs.emplace_back(method.position.file);
        }
        for (const auto& type : extract.model.enums) inputs.emplace_back(type.position.file);
        for (const auto& type : extract.exports)
        {
            inputs.emplace_back(type.position.file);
            for (const auto& method : type.methods) inputs.emplace_back(method.position.file);
        }
        std::vector<std::filesystem::path> outputs{output / (stem + ".attributes.tsv")};
        if (!interface_module.empty()) outputs.push_back(output / (interface_module + ".ixx"));
        else
        {
            outputs.push_back(output / (stem + ".h"));
            outputs.push_back(output / (stem + ".cpp"));
            if (prsm)
                for (const auto suffix : {"_prsm.h", "_prsm.cpp", "_prsm.abi.tsv", "_prsm.bindings.json"})
                    outputs.push_back(output / (stem + suffix));
        }
        protect_inputs(outputs, inputs);
        if (!interface_module.empty())
        {
            if (interface_module == module)
                throw std::runtime_error("generated module must differ from the imported module");
            const auto target = output / (interface_module + ".ixx");
            const auto text = emit_ifc_module_interface(extract.model, module, interface_module, registration,
                                                        target.generic_string(), attribute_imports);
            write_changed(target, text);
            write_changed(output / (stem + ".attributes.tsv"),
                          metadata ? metadata->catalog(scopes) : "reflgen-attributes\t1\n");
            std::cout << "reflgen-ifc: " << module << ": " << extract.model.classes.size() << " class(es), "
                      << extract.model.enums.size() << " enum(s)\n";
            return 0;
        }
        const auto header = output / (stem + ".h");
        const std::string header_text =
            emit_ifc_header(extract.model, module, registration, header.generic_string(), attribute_imports);
        const std::string source = emit_ifc_source(extract.model, module, registration, header_text,
                                                   registration_headers, registration_imports);
        // Do not publish partial output if IFC extraction or code generation failed.
        write_changed(header, header_text);
        if (bridge)
        {
            write_changed(output / (stem + "_prsm.h"), bridge->header);
            write_changed(output / (stem + "_prsm.cpp"), bridge->source);
            write_changed(output / (stem + "_prsm.abi.tsv"), bridge->abi_table);
            write_changed(output / (stem + "_prsm.bindings.json"), bridge->catalog);
        }
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


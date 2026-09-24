// reflgen — [[reflgen::…]] attribute 를 읽어 reflection 코드를 만드는 생성기.
//
//   reflgen --module NAME --output DIR [--attribute-scope NS]... [--clang-args-file FILE]
//           [--depfile FILE] [--dependency-list FILE] [--discover] HEADER... [-- CLANG_ARG...]
//   reflgen @FILE          FILE 의 한 줄을 인자 하나로 펼친다(명령줄 길이 제한을 피하려는 것)
//
// 빌드 시스템(cmake/reflgen.cmake 의 reflgen_generate, msbuild/reflgen.targets)이 부르는 것이
// 기본이다. 출력은 모두 빌드 중간 산출물이다 — 사용자 header 는 아무것도 include 하지 않고, 빌드 시스템이
// reflgen_<module>.h 를 모든 번역 단위에 강제 include 한다. 출력 디렉터리에는 편집기용 attribute 카탈로그
// (reflgen_<module>.attributes.tsv)도 생긴다.
//
// --discover: 받은 header 는 후보다(프로젝트의 header 전부). [[reflgen::reflect]] 가 있는 것만 파싱하고,
//             없는 파일은 건너뛰며, 더는 반영하지 않는 header 의 옛 생성 파일을 지운다.
#include "attribute_scan.h"
#include "catalog.h"
#include "diagnostics.h"
#include "emit.h"
#include "extract.h"
#include "clang_api.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace reflgen::generator;

    constexpr std::string_view usage =
        "usage: reflgen --module NAME --output DIR [--attribute-scope NS]... "
        "[--clang-args-file FILE] [--depfile FILE] [--dependency-list FILE] [--discover] HEADER... "
        "[-- CLANG_ARG...]\n";

    struct cli_options
    {
        extract_options extraction;
        std::string depfile;         // Makefile 문법(Ninja·Make). 비면 쓰지 않는다
        std::string dependency_list; // 한 줄에 경로 하나(MSBuild). 비면 쓰지 않는다
        bool discover = false;       // header 는 후보다 — 반영을 선언한 것만 남긴다
    };

    // 한 줄에 인자 하나. 빈 줄은 건너뛴다(빌드 시스템이 만든 파일의 끝 줄바꿈 등). 손으로 쓰거나 다른 도구가 만든
    // 파일도 받도록 UTF-8 BOM 과 줄 전체를 감싼 따옴표를 벗긴다(메모장·PowerShell 은 BOM 을 붙인다).
    std::vector<std::string> read_argument_lines(const std::string& path)
    {
        std::ifstream stream{std::filesystem::path(path)};
        std::vector<std::string> lines;
        std::string line;
        for (bool first_line = true; std::getline(stream, line); first_line = false)
        {
            if (first_line && line.starts_with("\xEF\xBB\xBF"))
            {
                line.erase(0, 3);
            }
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            {
                line.pop_back();
            }
            if (line.size() >= 2 && line.front() == '"' && line.back() == '"')
            {
                line = line.substr(1, line.size() - 2);
            }
            if (!line.empty())
            {
                lines.push_back(line);
            }
        }
        return lines;
    }

    // @FILE 을 펼친다. 응답 파일 안의 @ 는 다시 펼치지 않는다.
    std::vector<std::string> expand_response_files(int argc, char** argv, diagnostics& report)
    {
        std::vector<std::string> arguments;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument = argv[i];
            if (!argument.starts_with('@'))
            {
                arguments.emplace_back(argument);
                continue;
            }
            const std::string path(argument.substr(1));
            if (!std::filesystem::exists(path))
            {
                report.error("RG0001", {}, "response file '" + path + "' does not exist");
                continue;
            }
            for (std::string& line : read_argument_lines(path))
            {
                arguments.push_back(std::move(line));
            }
        }
        return arguments;
    }

    std::optional<cli_options> parse_options(const std::vector<std::string>& arguments, diagnostics& report)
    {
        cli_options cli;
        extract_options& options = cli.extraction;
        bool clang_part = false;
        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            const std::string_view argument = arguments[i];
            if (clang_part)
            {
                options.clang_arguments.emplace_back(argument);
                continue;
            }
            const auto value = [&](std::string_view name) -> std::optional<std::string> {
                if (i + 1 >= arguments.size())
                {
                    report.error("RG0001", {}, std::string(name) + " needs a value");
                    return std::nullopt;
                }
                return arguments[++i];
            };

            if (argument == "--")
            {
                clang_part = true;
            }
            else if (argument == "--discover")
            {
                cli.discover = true;
            }
            else if (argument == "--module")
            {
                if (const auto name = value(argument))
                {
                    options.module_name = *name;
                }
            }
            else if (argument == "--output")
            {
                if (const auto directory = value(argument))
                {
                    options.output_directory = normalize_path(*directory);
                }
            }
            else if (argument == "--attribute-scope")
            {
                if (const auto scope = value(argument))
                {
                    options.attribute_scopes.push_back(*scope);
                }
            }
            else if (argument == "--depfile")
            {
                if (const auto path = value(argument))
                {
                    cli.depfile = *path;
                }
            }
            else if (argument == "--dependency-list")
            {
                if (const auto path = value(argument))
                {
                    cli.dependency_list = *path;
                }
            }
            else if (argument == "--clang-args-file")
            {
                if (const auto path = value(argument))
                {
                    if (!std::filesystem::exists(*path))
                    {
                        report.error("RG0001", {}, "clang arguments file '" + *path + "' does not exist");
                    }
                    for (std::string& line : read_argument_lines(*path))
                    {
                        options.clang_arguments.push_back(std::move(line));
                    }
                }
            }
            else if (argument.starts_with("--"))
            {
                report.error("RG0001", {}, "unknown option '" + std::string(argument) + "'");
            }
            else
            {
                options.headers.push_back(normalize_path(std::string(argument)));
            }
        }

        if (!is_identifier(options.module_name))
        {
            report.error("RG0001", {}, "--module must be a C++ identifier (got '" + options.module_name + "')");
        }
        if (options.output_directory.empty())
        {
            report.error("RG0001", {}, "--output is required");
        }
        if (options.headers.empty())
        {
            report.error("RG0001", {}, "no input headers");
        }
        // 후보 목록(프로젝트의 header 전부)에는 지워진 파일이 남아 있을 수 있다 — 그것은 discover 가 걸러 낸다.
        for (const std::string& header : options.headers)
        {
            if (!cli.discover && !std::filesystem::exists(header))
            {
                report.error("RG0001", {header, 1, 1}, "input header does not exist");
            }
        }
        if (report.has_errors())
        {
            return std::nullopt;
        }
        return cli;
    }

    // 내용이 같으면 쓰지 않는다 — 시각이 바뀌지 않아야 이 파일에 기대는 것들이 다시 빌드되지 않는다.
    // C++ 생성 파일은 UTF-8 BOM 으로 시작한다(emit.cpp 머리말 참고). 도구가 읽는 파일은 BOM 없이 쓴다.
    enum class byte_order_mark
    {
        with,
        without,
    };

    bool write_if_changed(const std::string& path, const std::string& text, diagnostics& report,
                          byte_order_mark mark = byte_order_mark::with)
    {
        const std::string content = (mark == byte_order_mark::with ? "\xEF\xBB\xBF" : "") + text;
        {
            std::ifstream existing(std::filesystem::path(path), std::ios::binary);
            if (existing)
            {
                std::ostringstream buffer;
                buffer << existing.rdbuf();
                if (buffer.str() == content)
                {
                    return true;
                }
            }
        }
        std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        stream << content;
        if (!stream)
        {
            report.error("RG0001", {}, "could not write '" + path + "'");
            return false;
        }
        return true;
    }

    std::string ascii_lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::string read_file(const std::string& path)
    {
        std::ifstream stream(std::filesystem::path(path), std::ios::binary);
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    // --discover: 반영을 선언한 header 만 남긴다.
    std::vector<std::string> reflected_headers(const std::vector<std::string>& candidates)
    {
        std::vector<std::string> headers;
        for (const std::string& header : candidates)
        {
            if (std::filesystem::is_regular_file(header) && declares_reflection(read_file(header)))
            {
                headers.push_back(header);
            }
        }
        return headers;
    }

    // --discover: 더는 반영하지 않는 header 의 옛 생성 파일을 지운다. 주입 header 가 include 하지 않으니 남아도
    // 해는 없지만, 출력 디렉터리가 지금의 생성 결과만 담게 한다. 지난 실행의 주입 header 가 include 하던 것만
    // 지운다 — 출력 디렉터리를 다른 module 과 나눠 써도 그쪽 생성 파일은 건드리지 않는다. 경로는 대소문자를
    // 가리지 않고 견준다(Windows 에서는 같은 파일이다 — 지금 쓴 파일을 지우면 안 된다).
    void remove_stale_outputs(const std::string& previous_injection, const std::vector<std::string>& generated_paths)
    {
        constexpr std::string_view include_prefix = "#include \"";
        constexpr std::string_view generated_suffix = ".reflgen.h\"";
        std::vector<std::string> current;
        for (const std::string& path : generated_paths)
        {
            current.push_back(ascii_lowercase(path));
        }
        std::istringstream lines(previous_injection);
        for (std::string line; std::getline(lines, line);)
        {
            if (!line.starts_with(include_prefix) || !line.ends_with(generated_suffix))
            {
                continue;
            }
            const std::string path = line.substr(include_prefix.size(), line.size() - include_prefix.size() - 1);
            if (std::find(current.begin(), current.end(), ascii_lowercase(path)) == current.end())
            {
                std::error_code ignored; // 다른 프로세스가 쥐고 있으면 다음 실행에서 지운다
                std::filesystem::remove(std::filesystem::path(path), ignored);
            }
        }
    }

    // Makefile 문법의 depfile — header 가 include 하는 파일이 바뀔 때도 빌드 시스템이 다시 생성하게 한다.
    std::string escape_for_depfile(const std::string& path)
    {
        std::string escaped;
        for (const char c : path)
        {
            if (c == ' ' || c == '#')
            {
                escaped += '\\';
            }
            else if (c == '$')
            {
                escaped += '$';
            }
            escaped += c;
        }
        return escaped;
    }

    void write_depfile(const std::string& path, const std::string& target, const std::vector<std::string>& dependencies,
                       diagnostics& report)
    {
        std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        stream << escape_for_depfile(target) << ":";
        for (const std::string& dependency : dependencies)
        {
            stream << " \\\n  " << escape_for_depfile(dependency);
        }
        stream << "\n";
        if (!stream)
        {
            report.error("RG0001", {}, "could not write '" + path + "'");
        }
    }

    void write_dependency_list(const std::string& path, const std::vector<std::string>& dependencies,
                               diagnostics& report)
    {
        std::string text;
        for (const std::string& dependency : dependencies)
        {
            text += dependency + '\n';
        }
        write_if_changed(path, text, report, byte_order_mark::without);
    }

    int run(int argc, char** argv)
    {
        diagnostics report;
        const std::vector<std::string> arguments = expand_response_files(argc, argv, report);
        std::optional<cli_options> cli = report.has_errors() ? std::nullopt : parse_options(arguments, report);
        if (!cli)
        {
            report.print(stderr);
            std::fputs(usage.data(), stderr);
            return 1;
        }
        if (cli->discover)
        {
            cli->extraction.headers = reflected_headers(cli->extraction.headers);
        }
        const extract_options& options = cli->extraction;

        std::map<std::string, std::string> owners;
        std::vector<std::string> generated_names;
        for (const std::string& header : options.headers)
        {
            // 대소문자만 다른 이름도 Windows 에서는 같은 파일이다.
            const std::string name = generated_header_name(header);
            const auto [found, inserted] = owners.emplace(ascii_lowercase(name), header);
            if (!inserted)
            {
                report.error("RG0007", {header, 1, 1},
                             "generated file name '" + name + "' collides with the one for '" + found->second +
                                 "'; rename one header or generate them in separate modules");
            }
            generated_names.push_back(name);
        }
        std::filesystem::create_directories(options.output_directory);

        // 반영할 header 가 없어도 주입 header 와 등록 함수는 만든다 — 빌드 시스템이 늘 강제 include 한다.
        const extract_result extracted =
            report.has_errors() || options.headers.empty() ? extract_result{} : extract(options, report);
        const std::vector<header_model>& headers = extracted.headers;
        if (!report.has_errors())
        {
            std::vector<std::string> generated_paths;
            for (std::size_t i = 0; i < headers.size(); ++i)
            {
                generated_paths.push_back(options.output_directory + "/" + generated_names[i]);
                write_if_changed(generated_paths.back(), emit_header(headers[i], generated_paths.back()), report);
            }
            const std::string base = options.output_directory + "/reflgen_" + options.module_name;
            const std::string previous_injection = cli->discover ? read_file(base + ".h") : std::string();
            write_if_changed(base + ".h", emit_module_header(options.module_name, generated_paths), report);
            write_if_changed(base + ".cpp", emit_module_source(options.module_name, headers), report);
            write_if_changed(base + ".attributes.tsv", format_attribute_catalog(extracted.attributes), report,
                             byte_order_mark::without);
            if (!cli->dependency_list.empty())
            {
                write_dependency_list(cli->dependency_list, extracted.dependencies, report);
            }
            if (!cli->depfile.empty())
            {
                // 대상은 빌드 시스템이 아는 첫 출력이다.
                write_depfile(cli->depfile, generated_paths.empty() ? base + ".h" : generated_paths.front(),
                              extracted.dependencies, report);
            }
            if (cli->discover)
            {
                remove_stale_outputs(previous_injection, generated_paths);
            }
        }
        report.print(stderr);
        return report.has_errors() ? 1 : 0;
    }
} // namespace

int main(int argc, char** argv)
{
    // 예외가 새어 나가면 빌드 로그에 위치 없는 비정상 종료만 남는다 — 진단 한 줄로 바꾼다.
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "reflgen : error RG0001: %s\n", error.what());
    }
    return 1;
}

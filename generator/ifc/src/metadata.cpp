#include "metadata.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace reflgen::generator
{
    namespace
    {
        std::string normalized(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            path = std::filesystem::absolute(path).lexically_normal().generic_string();
            std::ranges::transform(path, path.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return path;
        }
        bool word(unsigned char c)
        {
            return std::isalnum(c) || c == '_' || c >= 128;
        }
    } // namespace

    module_metadata::module_metadata(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot open preprocessed metadata: " + path.string());
        const std::string text(std::istreambuf_iterator<char>(input), {});
        std::string file;
        unsigned line = 1;
        bool line_start = true;
        auto advance = [&](std::size_t begin, std::size_t end) {
            for (auto i = begin; i < end; ++i)
                if (text[i] == '\n')
                {
                    ++line;
                    line_start = true;
                }
        };
        for (std::size_t i = 0; i < text.size();)
        {
            if (std::isspace(static_cast<unsigned char>(text[i])))
            {
                advance(i, i + 1);
                ++i;
                continue;
            }
            if (i == 0 && text.compare(i, 3, "\xef\xbb\xbf") == 0)
            {
                i += 3;
                continue;
            }
            if (line_start && text[i] == '#')
            {
                const auto end = text.find('\n', i);
                const auto stop = end == std::string::npos ? text.size() : end;
                std::istringstream directive(text.substr(i + 1, stop - i - 1));
                std::string key;
                directive >> key;
                if (key == "line")
                {
                    unsigned number;
                    directive >> number >> std::ws;
                    if (!directive || directive.get() != '"')
                        throw std::runtime_error("invalid metadata #line");
                    std::string name;
                    std::getline(directive, name, '"');
                    file = normalized(name);
                    line = number;
                }
                else if (key != "pragma")
                    throw std::runtime_error("metadata must be MSVC /P output with #line directives");
                else
                    ++line;
                i = stop < text.size() ? stop + 1 : stop;
                line_start = true;
                continue;
            }
            line_start = false;
            const auto begin = i;
            token_kind kind = token_kind::punctuation;
            if (text.compare(i, 2, "//") == 0)
            {
                i = text.find('\n', i);
                if (i == std::string::npos)
                    i = text.size();
                advance(begin, i);
                continue;
            }
            if (text.compare(i, 2, "/*") == 0)
            {
                const auto end = text.find("*/", i + 2);
                if (end == std::string::npos)
                    throw std::runtime_error("unterminated metadata comment");
                i = end + 2;
                advance(begin, i);
                continue;
            }
            // Consume prefixed, raw and ordinary literals as single tokens so attribute-like text inside
            // a string can never become metadata. /P has already expanded macros and removed inactive code.
            auto quote = i;
            if (text.compare(quote, 2, "u8") == 0)
                quote += 2;
            else if (text[quote] == 'u' || text[quote] == 'U' || text[quote] == 'L')
                ++quote;
            const bool raw = quote + 1 < text.size() && text[quote] == 'R' && text[quote + 1] == '"';
            if (raw)
            {
                const auto open = text.find('(', quote + 2);
                if (open == std::string::npos || open - quote > 18)
                    throw std::runtime_error("invalid raw literal");
                const auto close = ")" + text.substr(quote + 2, open - quote - 2) + "\"";
                const auto end = text.find(close, open + 1);
                if (end == std::string::npos)
                    throw std::runtime_error("unterminated raw literal");
                i = end + close.size();
                kind = token_kind::literal;
            }
            else if (quote < text.size() && (text[quote] == '"' || text[quote] == '\''))
            {
                const auto delimiter = text[quote];
                i = quote + 1;
                bool closed = false;
                while (i < text.size())
                {
                    if (text[i] == '\\')
                    {
                        i = std::min(i + 2, text.size());
                        continue;
                    }
                    if (text[i++] == delimiter)
                    {
                        closed = true;
                        break;
                    }
                }
                if (!closed)
                    throw std::runtime_error("unterminated metadata literal");
                kind = token_kind::literal;
            }
            else if (word(static_cast<unsigned char>(text[i])))
            {
                kind = std::isdigit(static_cast<unsigned char>(text[i])) ? token_kind::literal : token_kind::identifier;
                while (i < text.size() &&
                       (word(static_cast<unsigned char>(text[i])) ||
                        (kind == token_kind::literal &&
                         (text[i] == '.' || text[i] == '\'' ||
                          ((text[i] == '+' || text[i] == '-') && i > begin &&
                           (text[i - 1] == 'e' || text[i - 1] == 'E' || text[i - 1] == 'p' || text[i - 1] == 'P'))))))
                    ++i;
            }
            else
            {
                ++i;
                for (const auto op : {"::", "->", "&&", "||", "==", "!=", "<=", ">=", "<<", ">>", "++", "--"})
                    if (text.compare(begin, 2, op) == 0)
                    {
                        i = begin + 2;
                        break;
                    }
            }
            if (text.substr(begin, i - begin) == "using")
                kind = token_kind::keyword;
            tokens_.push_back({kind, text.substr(begin, i - begin), begin, i});
            locations_.push_back({file, line});
            lines_[{file, line}].push_back(tokens_.size() - 1);
            advance(begin, i);
        }
        if (file.empty())
            throw std::runtime_error("metadata has no #line information; use MSVC /P (without /EP)");
        groups_ = scan_attribute_groups(tokens_);
        for (std::size_t i = 0; i + 1 < tokens_.size(); ++i)
            if (tokens_[i].spelling == "__preprocessed_module")
            {
                for (++i; i < tokens_.size() && tokens_[i].spelling != ";"; ++i)
                    module_ += tokens_[i].spelling;
                break;
            }
        if (module_.empty())
            throw std::runtime_error("metadata has no primary module declaration");
    }

    std::vector<attribute_group> module_metadata::declaration(const source_position& position, std::string_view name,
                                                              bool type) const
    {
        const auto found = lines_.find({normalized(position.file), position.line});
        if (found == lines_.end())
            throw std::runtime_error("metadata does not match IFC source line; rebuild both inputs");
        std::vector<std::size_t> matches;
        for (auto index : found->second)
            if (tokens_[index].kind == token_kind::identifier && tokens_[index].spelling == name)
            {
                const auto offset = tokens_[index].begin;
                if (std::ranges::none_of(groups_,
                                         [offset](const auto& g) { return g.begin <= offset && offset < g.end; }))
                    matches.push_back(index);
            }
        if (matches.size() > 1 && type)
        {
            // A class name can appear again in its own CRTP base list on the same line.
            // Select the declaration introducer, never a template argument or attribute operand.
            std::vector<std::size_t> declarations;
            for (const auto candidate : matches)
            {
                auto before = candidate;
                while (before > 0)
                {
                    const auto group = std::ranges::find_if(groups_, [&](const auto& item) {
                        return item.end == tokens_[before - 1].end;
                    });
                    if (group == groups_.end()) break;
                    while (before > 0 && tokens_[before - 1].begin >= group->begin) --before;
                }
                if (before > 0)
                {
                    const auto& introducer = tokens_[before - 1].spelling;
                    if (introducer == "class" || introducer == "struct" ||
                        introducer == "enum" || introducer == "union")
                        declarations.push_back(candidate);
                }
            }
            if (declarations.size() == 1) matches = std::move(declarations);
        }
        if (matches.size() != 1)
            throw std::runtime_error("ambiguous metadata declaration '" + std::string(name) +
                                     "'; put declarations on separate lines");
        const auto at = matches.front();
        auto start = at;
        // Skip entire attribute groups and balanced initializer/parameter expressions when walking to
        // the declaration boundary. A comma is not a boundary: leading attributes apply to all declarators.
        int parens = 0, brackets = 0;
        while (start > 0)
        {
            const auto& t = tokens_[start - 1].spelling;
            if (t == ")")
                ++parens;
            if (t == "]")
                ++brackets;
            if (parens == 0 && brackets == 0 && (t == ";" || t == "{" || t == "}" || t == ":"))
                break;
            if (t == "(")
                --parens;
            if (t == "[")
                --brackets;
            --start;
        }
        std::vector<attribute_group> result;
        if (type)
            result = groups_between(groups_, tokens_[start].begin, tokens_[at].begin);
        else
            result = groups_run_at(tokens_, groups_, tokens_[start].begin);
        if (at + 1 < tokens_.size())
        {
            auto after = groups_run_at(tokens_, groups_, tokens_[at + 1].begin);
            result.insert(result.end(), after.begin(), after.end());
        }
        return result;
    }

    const scanned_attribute* module_metadata::directive(const std::vector<attribute_group>& groups,
                                                        std::string_view name) const
    {
        for (const auto& group : groups)
            for (const auto& attribute : group.attributes)
                if (attribute.scope == "reflgen" && attribute.name == name)
                    return &attribute;
        return nullptr;
    }

    std::string module_metadata::arguments(const scanned_attribute& attribute, const qualifier_lookup& lookup) const
    {
        return argument_text(
            tokens_, attribute.arguments_begin, attribute.arguments_end,
            lookup ? lookup : [](std::string_view) { return std::string(); });
    }

    std::vector<attribute_use> module_metadata::uses(const std::vector<attribute_group>& groups,
                                                     const std::set<std::string>& scopes,
                                                     const qualifier_lookup& lookup, bool interop) const
    {
        std::vector<attribute_use> result;
        for (const auto& group : groups)
            for (const auto& a : group.attributes)
            {
                const bool binding_directive =
                    a.scope == "reflgen" && (a.name == "interop" || a.name == "lifetime" || a.name == "property_get" ||
                                             a.name == "property_set" || a.name == "effect" || a.name == "owner" ||
                                             a.name == "receiver");
                if ((!interop && binding_directive) || !scopes.contains(a.scope) ||
                    (a.scope == "reflgen" && (a.name == "reflect" || a.name == "ignore" || a.name == "attribute" ||
                                             a.name == "public_methods" || a.name == "public_fields" || a.name == "runtime")))
                    continue;
                const auto index = std::lower_bound(tokens_.begin(), tokens_.end(), a.begin,
                                                    [](const auto& t, auto offset) { return t.begin < offset; });
                const auto& loc = locations_[static_cast<std::size_t>(index - tokens_.begin())];
                result.push_back(
                    {"::" + a.scope + "::" + a.name + (a.has_arguments ? "(" + arguments(a, lookup) + ")" : "{}"),
                     {loc.file, loc.line, 1}});
                auto& use = result.back();
                if (interop && binding_directive)
                    consumed_bindings_.insert(a.begin);
                use.scope = a.scope;
                use.name = a.name;
                use.has_arguments = a.has_arguments;
                if (a.has_arguments)
                    for (const auto& t : tokens_)
                        if (t.begin >= a.arguments_begin && t.end <= a.arguments_end && t.kind != token_kind::comment)
                            use.argument_tokens.push_back(t.spelling);
            }
        return result;
    }

    void module_metadata::validate_prsm_selection() const
    {
        for (const auto& group : groups_)
            for (const auto& a : group.attributes)
                if (a.scope == "reflgen" &&
                    ((a.name == "interop" && a.has_arguments && arguments(a) == "\"prsm\"") ||
                     a.name == "property_get" || a.name == "property_set" || a.name == "effect" || a.name == "owner" ||
                     a.name == "receiver") &&
                    !consumed_bindings_.contains(a.begin))
                    throw std::runtime_error("selected PrSM annotation has no supported exported IFC declaration");
    }

    std::string module_metadata::catalog(const std::set<std::string>& scopes) const
    {
        std::string result = "reflgen-attributes\t1\n";
        std::set<std::string> seen;
        for (const auto& group : groups_)
            for (const auto& a : group.attributes)
                if (a.scope != "reflgen" && scopes.contains(a.scope) && seen.insert(a.scope + "::" + a.name).second)
                    result += "attribute\t" + a.scope + "\t" + a.name + "\t" + a.name +
                              (a.has_arguments ? "(...)" : "") + "\tImported module attribute\n";
        return result;
    }
} // namespace reflgen::generator


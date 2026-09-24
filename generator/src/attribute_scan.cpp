#include "attribute_scan.h"
#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace reflgen::generator
{
namespace
{
bool is(const token& item, std::string_view spelling)
{
    return item.kind == token_kind::punctuation && item.spelling == spelling;
}

bool is_name(const token& item)
{
    // attribute 이름은 키워드여도 된다([[reflgen::transient]] 의 이름은 식별자지만,
    // 표준 attribute 이름과 사용자 이름이 키워드와 겹칠 수 있다).
    return item.kind == token_kind::identifier || item.kind == token_kind::keyword;
}

// position 은 '(' 를 가리킨다. 짝이 맞는 ')' 의 위치를 돌려준다.
std::optional<std::size_t> matching_parenthesis(std::span<const token> tokens, std::size_t position)
{
    std::vector<char> expected;
    for (std::size_t i = position; i < tokens.size(); ++i)
    {
        const token& item = tokens[i];
        if (item.kind != token_kind::punctuation)
        {
            continue;
        }
        if (item.spelling == "(" || item.spelling == "[" || item.spelling == "{")
        {
            expected.push_back(item.spelling == "(" ? ')' : item.spelling == "[" ? ']' : '}');
        }
        else if (item.spelling == ")" || item.spelling == "]" || item.spelling == "}")
        {
            if (expected.empty() || expected.back() != item.spelling.front())
            {
                return std::nullopt;
            }
            expected.pop_back();
            if (expected.empty())
            {
                return i;
            }
        }
    }
    return std::nullopt;
}

// position 은 '[' '[' 의 첫 '[' 다. 성공하면 그룹과 다음 token 위치를 돌려준다.
std::optional<std::pair<attribute_group, std::size_t>> parse_group(std::span<const token> tokens, std::size_t position)
{
    attribute_group group;
    group.begin = tokens[position].begin;
    std::size_t i = position + 2;

    std::string default_scope;
    if (i + 2 < tokens.size() && tokens[i].kind == token_kind::keyword && tokens[i].spelling == "using" &&
        is_name(tokens[i + 1]) && is(tokens[i + 2], ":"))
    {
        default_scope = tokens[i + 1].spelling;
        i += 3;
    }

    while (i < tokens.size())
    {
        if (i + 1 < tokens.size() && is(tokens[i], "]") && is(tokens[i + 1], "]"))
        {
            group.end = tokens[i + 1].end;
            return std::pair{std::move(group), i + 2};
        }
        if (is(tokens[i], ","))
        {
            ++i;
            continue;
        }
        if (!is_name(tokens[i]))
        {
            return std::nullopt;
        }

        scanned_attribute attribute;
        attribute.begin = tokens[i].begin;
        if (i + 2 < tokens.size() && is(tokens[i + 1], "::") && is_name(tokens[i + 2]))
        {
            attribute.scope = tokens[i].spelling;
            attribute.name = tokens[i + 2].spelling;
            i += 3;
        }
        else
        {
            attribute.scope = default_scope;
            attribute.name = tokens[i].spelling;
            i += 1;
        }

        if (i < tokens.size() && is(tokens[i], "("))
        {
            const std::optional<std::size_t> close = matching_parenthesis(tokens, i);
            if (!close)
            {
                return std::nullopt;
            }
            attribute.has_arguments = true;
            attribute.arguments_begin = tokens[i].end;
            attribute.arguments_end = tokens[*close].begin;
            i = *close + 1;
        }
        group.attributes.push_back(std::move(attribute));
    }
    return std::nullopt;
}

// offset 이후(포함) 첫 token.
std::span<const token>::iterator first_token_from(std::span<const token> tokens, std::size_t offset)
{
    return std::lower_bound(tokens.begin(), tokens.end(), offset,
                            [](const token& item, std::size_t value) { return item.begin < value; });
}

bool follows_member_access(const token* previous)
{
    if (previous == nullptr || previous->kind != token_kind::punctuation)
    {
        return false;
    }
    const std::string_view spelling = previous->spelling;
    return spelling == "::" || spelling == "." || spelling == "->" || spelling == ".*" || spelling == "->*";
}
} // namespace

std::vector<attribute_group> scan_attribute_groups(std::span<const token> tokens)
{
    std::vector<attribute_group> groups;
    std::size_t i = 0;
    while (i + 1 < tokens.size())
    {
        if (is(tokens[i], "[") && is(tokens[i + 1], "["))
        {
            if (auto parsed = parse_group(tokens, i))
            {
                groups.push_back(std::move(parsed->first));
                i = parsed->second;
                continue;
            }
        }
        ++i;
    }
    return groups;
}

bool is_identifier(std::string_view text) noexcept
{
    if (text.empty() || (text.front() >= '0' && text.front() <= '9'))
    {
        return false;
    }
    for (const char c : text)
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!valid)
        {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> next_token_offset(std::span<const token> tokens, std::size_t offset)
{
    const auto found = first_token_from(tokens, offset);
    if (found == tokens.end() || found->begin != offset || found + 1 == tokens.end())
    {
        return std::nullopt;
    }
    return (found + 1)->begin;
}

std::vector<attribute_group> groups_run_at(std::span<const token> tokens, std::span<const attribute_group> groups,
                                           std::size_t offset)
{
    std::vector<attribute_group> run;
    while (true)
    {
        const auto group =
            std::lower_bound(groups.begin(), groups.end(), offset,
                             [](const attribute_group& item, std::size_t value) { return item.begin < value; });
        if (group == groups.end() || group->begin != offset)
        {
            return run;
        }
        run.push_back(*group);
        const auto next = first_token_from(tokens, group->end);
        if (next == tokens.end())
        {
            return run;
        }
        offset = next->begin;
    }
}

std::vector<attribute_group> groups_between(std::span<const attribute_group> groups, std::size_t begin, std::size_t end)
{
    std::vector<attribute_group> result;
    for (const attribute_group& group : groups)
    {
        if (group.begin >= begin && group.end <= end)
        {
            result.push_back(group);
        }
    }
    return result;
}

std::string argument_text(std::span<const token> tokens, std::size_t begin, std::size_t end,
                          const qualifier_lookup& qualifier_of)
{
    std::string text;
    const token* previous = nullptr;
    for (auto item = first_token_from(tokens, begin); item != tokens.end() && item->end <= end; ++item)
    {
        if (item->kind == token_kind::comment)
        {
            continue;
        }
        if (previous != nullptr && previous->end < item->begin)
        {
            text += ' ';
        }
        if (item->kind == token_kind::identifier && !follows_member_access(previous))
        {
            text += qualifier_of(item->spelling);
        }
        text += item->spelling;
        previous = &*item;
    }
    return text;
}
} // namespace reflgen::generator

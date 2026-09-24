#include "attribute_scan.h"
#include "harness.h"
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using reflgen::generator::argument_text;
    using reflgen::generator::attribute_group;
    using reflgen::generator::declares_reflection;
    using reflgen::generator::groups_between;
    using reflgen::generator::groups_run_at;
    using reflgen::generator::next_token_offset;
    using reflgen::generator::scan_attribute_groups;
    using reflgen::generator::token;
    using reflgen::generator::token_kind;
    using reflgen_test::check;
    using reflgen_test::check_equal;
    using reflgen_test::require;
    using reflgen_test::test;

    // 시험용 간이 lexer — 식별자·숫자·문자열 리터럴·"::"·"->"·한 글자 구두점만 가르고 // 주석은 버린다.
    std::vector<token> lex(std::string_view text)
    {
        std::vector<token> tokens;
        std::size_t i = 0;
        while (i < text.size())
        {
            const char c = text[i];
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') // clang_tokenize 처럼 주석은 버린다
            {
                while (i < text.size() && text[i] != '\n')
                {
                    ++i;
                }
                continue;
            }
            token item;
            item.begin = i;
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
                {
                    ++i;
                }
                const std::string word(text.substr(item.begin, i - item.begin));
                item.kind =
                    word == "using" || word == "int" || word == "class" ? token_kind::keyword : token_kind::identifier;
            }
            else if (std::isdigit(static_cast<unsigned char>(c)))
            {
                while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '.'))
                {
                    ++i;
                }
                item.kind = token_kind::literal;
            }
            else if (c == '"')
            {
                ++i;
                while (i < text.size() && text[i] != '"')
                {
                    i += text[i] == '\\' ? 2 : 1;
                }
                ++i;
                item.kind = token_kind::literal;
            }
            else if (i + 1 < text.size() && ((c == ':' && text[i + 1] == ':') || (c == '-' && text[i + 1] == '>')))
            {
                i += 2;
                item.kind = token_kind::punctuation;
            }
            else
            {
                ++i;
                item.kind = token_kind::punctuation;
            }
            item.end = i;
            item.spelling = std::string(text.substr(item.begin, item.end - item.begin));
            tokens.push_back(std::move(item));
        }
        return tokens;
    }

    std::string arguments_of(std::string_view text, const reflgen::generator::scanned_attribute& attribute)
    {
        return std::string(text.substr(attribute.arguments_begin, attribute.arguments_end - attribute.arguments_begin));
    }

    const test scoped_attributes("scan: scoped attributes with and without arguments", [] {
        const std::string_view text = R"([[reflgen::range(0, max_hp), reflgen::transient]] int hp;)";
        const std::vector<attribute_group> groups = scan_attribute_groups(lex(text));
        require(groups.size() == 1);
        require(groups[0].attributes.size() == 2);
        check_equal(groups[0].attributes[0].scope, std::string("reflgen"));
        check_equal(groups[0].attributes[0].name, std::string("range"));
        check(groups[0].attributes[0].has_arguments);
        check_equal(arguments_of(text, groups[0].attributes[0]), std::string("0, max_hp"));
        check_equal(groups[0].attributes[1].name, std::string("transient"));
        check(!groups[0].attributes[1].has_arguments);
        check_equal(groups[0].begin, std::size_t{0});
        check_equal(text.substr(groups[0].end), std::string_view(" int hp;"));
    });

    const test using_prefix("scan: [[using ns: ...]] applies the scope", [] {
        const std::string_view text = R"([[using reflgen: display_name("HP"), hidden]])";
        const std::vector<attribute_group> groups = scan_attribute_groups(lex(text));
        require(groups.size() == 1 && groups[0].attributes.size() == 2);
        check_equal(groups[0].attributes[0].scope, std::string("reflgen"));
        check_equal(arguments_of(text, groups[0].attributes[0]), std::string("\"HP\""));
        check_equal(groups[0].attributes[1].scope, std::string("reflgen"));
    });

    const test nested_brackets("scan: arguments keep nested brackets and strings verbatim", [] {
        const std::string_view text = R"([[game::table({1, 2}, values[0], "a]]b", f(g(x)))]] int x;)";
        const std::vector<attribute_group> groups = scan_attribute_groups(lex(text));
        require(groups.size() == 1 && groups[0].attributes.size() == 1);
        check_equal(arguments_of(text, groups[0].attributes[0]), std::string(R"({1, 2}, values[0], "a]]b", f(g(x)))"));
    });

    const test several_groups("scan: several groups and unscoped attributes", [] {
        const std::string_view text = R"(class [[reflgen::reflect("game.player")]] [[nodiscard]] player)";
        const std::vector<attribute_group> groups = scan_attribute_groups(lex(text));
        require(groups.size() == 2);
        check_equal(arguments_of(text, groups[0].attributes[0]), std::string("\"game.player\""));
        check(groups[1].attributes[0].scope.empty());
        check_equal(groups[1].attributes[0].name, std::string("nodiscard"));
    });

    const test not_attributes("scan: bracket runs that are not attributes are skipped", [] {
        check(scan_attribute_groups(lex("a[[b]]")).size() == 1); // 모양은 attribute — 해석은 호출자 몫
        check(scan_attribute_groups(lex("a[[0]] + 1")).empty());
        check(scan_attribute_groups(lex("[[reflgen::range(0, 1]] int x;")).empty());
        check(scan_attribute_groups(lex("[[reflgen::")).empty());
        check(scan_attribute_groups(lex("")).empty());
    });

    // 이름의 첫 글자 offset.
    std::size_t offset_of(std::string_view text, std::string_view needle)
    {
        return text.find(needle);
    }

    const test declarator_runs("select: leading groups belong to every declarator, trailing ones to one", [] {
        const std::string_view text = "[[a]] [[b]] int x [[c]], y;";
        const std::vector<token> tokens = lex(text);
        const std::vector<attribute_group> groups = scan_attribute_groups(tokens);
        require(groups.size() == 3);

        const std::vector<attribute_group> leading = groups_run_at(tokens, groups, 0);
        require(leading.size() == 2);
        check_equal(leading[1].attributes[0].name, std::string("b"));

        const auto after_x = next_token_offset(tokens, offset_of(text, "x ["));
        require(after_x.has_value());
        const std::vector<attribute_group> x_groups = groups_run_at(tokens, groups, *after_x);
        require(x_groups.size() == 1);
        check_equal(x_groups[0].attributes[0].name, std::string("c"));

        const auto after_y = next_token_offset(tokens, offset_of(text, "y;"));
        require(after_y.has_value());
        check(groups_run_at(tokens, groups, *after_y).empty());
        check(!next_token_offset(tokens, text.size() - 1).has_value());         // 마지막 token 뒤는 없다
        check(!next_token_offset(tokens, offset_of(text, "nt x")).has_value()); // token 중간
    });

    const test between("select: groups fully inside a range", [] {
        const std::string_view text = "class [[a]] name { [[b]] int x; };";
        const std::vector<attribute_group> groups = scan_attribute_groups(lex(text));
        const std::vector<attribute_group> before_name = groups_between(groups, 0, offset_of(text, "name"));
        require(before_name.size() == 1);
        check_equal(before_name[0].attributes[0].name, std::string("a"));
    });

    const test argument_rebuild("arguments: rebuilt from tokens, class members qualified", [] {
        const std::string_view text = "[[r(0, max_hp // upper bound\n, limits::cap, cfg.value, other->x, f( 1 ,2 ))]]";
        const std::vector<token> tokens = lex(text);
        const std::vector<attribute_group> groups = scan_attribute_groups(tokens);
        require(groups.size() == 1);
        const auto& attribute = groups[0].attributes[0];
        const std::string rebuilt =
            argument_text(tokens, attribute.arguments_begin, attribute.arguments_end, [](std::string_view name) {
                const bool member = name == "max_hp" || name == "cfg" || name == "cap" || name == "x" || name == "f";
                return member ? std::string("::game::t::") : std::string();
            });
        // 주석이 빠지고, '::' '.' '->' 뒤의 이름은 그대로다.
        check_equal(rebuilt, std::string("0, ::game::t::max_hp , limits::cap, ::game::t::cfg.value, other->x, "
                                         "::game::t::f( 1 ,2 )"));
    });

    const test discovery("discover: headers that declare reflection", [] {
        check(declares_reflection("struct [[reflgen::reflect]] a {};"));
        check(declares_reflection("struct [[ reflgen :: reflect ( \"game.a\" ) ]] a {};"));
        check(declares_reflection("enum class [[nodiscard, reflgen::reflect]] e { x };"));
        check(declares_reflection("struct [[using reflgen: range(0, 1), reflect]] a {};"));
        check(declares_reflection("struct a {};\nclass\n[[reflgen::reflect]]\nb {};"));
    });

    const test discovery_rejects("discover: headers that do not declare reflection", [] {
        check(!declares_reflection("#include \"reflgen/reflgen.h\"\nstruct a { static consteval auto reflect(); };"));
        check(!declares_reflection("struct [[reflgen::reflected]] a {};"));
        check(!declares_reflection("struct [[reflgen::transient]] a {};"));
        check(!declares_reflection("struct [[other::reflect]] a {};"));
        check(!declares_reflection("struct [[using other: reflect]] a {};"));
        check(!declares_reflection("struct [[reflgen::reflect a {};")); // 닫히지 않은 그룹
    });
} // namespace

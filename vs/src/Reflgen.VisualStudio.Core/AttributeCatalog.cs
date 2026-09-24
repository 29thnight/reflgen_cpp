using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Reflgen.VisualStudio
{
    public enum AttributeKind
    {
        Directive, // 생성기가 해석하는 표지(reflect, ignore)
        Attribute, // 스키마로 옮겨지는 attribute 타입
    }

    // Signature 는 생성자마다 한 줄이다.
    public sealed record AttributeEntry(AttributeKind Kind, string Scope, string Name, string Signature, string Summary)
    {
        // 인자를 받는가 — 완성할 때 괄호를 붙일지 가른다.
        public bool TakesArguments => Signature.IndexOf('(') >= 0;
    }

    // [[scope:: 뒤에서 제안할 것들. 생성기가 빌드마다 reflgen_<module>.attributes.tsv 로 내놓는다.
    public sealed class AttributeCatalog
    {
        public const string LibraryScope = "reflgen";
        private const string Header = "reflgen-attributes\t1";

        private readonly List<AttributeEntry> _entries;

        public AttributeCatalog(IEnumerable<AttributeEntry> entries)
        {
            _entries = entries.ToList();
        }

        public IReadOnlyList<AttributeEntry> Entries => _entries;

        public static AttributeCatalog Empty { get; } = new AttributeCatalog(Array.Empty<AttributeEntry>());

        // 한 번도 생성하지 않은 프로젝트에서도 기본 attribute 는 완성되게 한다. 생성기의 카탈로그가 있으면
        // 그것이 이것을 대신한다(include/reflgen/core/attributes.h 에서 뽑은 최신판).
        public static AttributeCatalog BuiltIn { get; } = new AttributeCatalog(new[]
        {
            new AttributeEntry(AttributeKind.Directive, LibraryScope, "reflect", "reflect\nreflect(\"schema.name\")",
                               "클래스·열거형을 반영 대상으로, 메서드를 스키마에 넣을 것으로 표시한다. " +
                               "클래스에 준 인자는 등록 키·다형 태그가 된다."),
            new AttributeEntry(AttributeKind.Directive, LibraryScope, "ignore", "ignore",
                               "이 멤버를 반영에서 뺀다."),
            new AttributeEntry(AttributeKind.Directive, LibraryScope, "attribute", "attribute",
                               "이 타입을 편집기 자동완성의 attribute 로 내보낸다. " +
                               "이름공간에 하나라도 있으면 표시한 타입만 내보낸다."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "description",
                               "description(std::string_view text)", "설명문 (툴팁 등)."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "display_name",
                               "display_name(std::string_view text)", "사람이 읽는 이름 (인스펙터 라벨 등)."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "hidden", "hidden",
                               "편집기 표시 힌트. 직렬화와 무관하다."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "range", "range(T min_value, T max_value)",
                               "값의 허용 구간. 직렬화는 검사하지 않는다 — 편집기·검증기가 소비하는 표기다."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "readonly", "readonly",
                               "편집기에서 읽기 전용으로 보인다. 직렬화와 무관하다."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "required", "required",
                               "역직렬화 입력에 반드시 있어야 한다."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "serialized_name",
                               "serialized_name(std::string_view text)",
                               "직렬화 키 이름을 멤버 이름과 다르게 둔다."),
            new AttributeEntry(AttributeKind.Attribute, LibraryScope, "transient", "transient",
                               "직렬화에서 제외한다. 캐시·런타임 핸들처럼 저장할 이유가 없는 필드에 붙인다."),
        });

        public IEnumerable<string> Scopes =>
            _entries.Select(entry => entry.Scope).Distinct(StringComparer.Ordinal).OrderBy(scope => scope,
                                                                                            StringComparer.Ordinal);

        public IEnumerable<AttributeEntry> InScope(string scope) =>
            _entries.Where(entry => string.Equals(entry.Scope, scope, StringComparison.Ordinal));

        // 여러 프로젝트의 카탈로그를 합친다. (scope, name) 이 겹치면 먼저 온 것을 쓴다.
        public static AttributeCatalog Merge(IEnumerable<AttributeCatalog> catalogs)
        {
            var seen = new HashSet<string>(StringComparer.Ordinal);
            var merged = new List<AttributeEntry>();
            foreach (AttributeEntry entry in catalogs.SelectMany(catalog => catalog.Entries))
            {
                if (seen.Add(entry.Scope + "::" + entry.Name))
                {
                    merged.Add(entry);
                }
            }
            return new AttributeCatalog(merged);
        }

        // 형식이 다르거나 망가진 줄은 건너뛴다 — 편집기 기능이 파일 하나 때문에 멈추면 안 된다.
        public static AttributeCatalog Parse(string text)
        {
            string[] lines = text.Replace("\r\n", "\n").Split('\n');
            if (lines.Length == 0 || lines[0] != Header)
            {
                return Empty;
            }
            var entries = new List<AttributeEntry>();
            foreach (string line in lines.Skip(1))
            {
                string[] fields = line.Split('\t');
                if (fields.Length != 5)
                {
                    continue;
                }
                AttributeKind kind = fields[0] == "directive" ? AttributeKind.Directive : AttributeKind.Attribute;
                entries.Add(new AttributeEntry(kind, Unescape(fields[1]), Unescape(fields[2]), Unescape(fields[3]),
                                               Unescape(fields[4])));
            }
            return new AttributeCatalog(entries);
        }

        private static string Unescape(string value)
        {
            if (value.IndexOf('\\') < 0)
            {
                return value;
            }
            var builder = new StringBuilder(value.Length);
            for (int i = 0; i < value.Length; ++i)
            {
                char c = value[i];
                if (c == '\\' && i + 1 < value.Length)
                {
                    char next = value[++i];
                    builder.Append(next == 't' ? '\t' : next == 'n' ? '\n' : next);
                }
                else
                {
                    builder.Append(c);
                }
            }
            return builder.ToString();
        }
    }
}

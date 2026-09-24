namespace Reflgen.VisualStudio
{
    public enum AttributePosition
    {
        None,   // attribute 목록 밖(또는 주석·문자열·인자 괄호 안)
        Name,   // "[[" 나 "," 바로 뒤 — attribute 이름공간(과 using 기본 이름공간의 이름)을 제안한다
        Member, // "scope::" 바로 뒤 — 그 이름공간의 attribute 를 제안한다
    }

    // Scope — Member: "::" 앞의 이름공간, Name: [[using ns: …]] 의 ns(없으면 null).
    // PrefixLength — 커서 앞에 이미 친 식별자 글자 수. 완성이 바꿀 구간이다.
    public sealed record AttributeCompletionContext(AttributePosition Position, string? Scope, int PrefixLength)
    {
        public static readonly AttributeCompletionContext None = new(AttributePosition.None, null, 0);
    }

    // 커서 앞 텍스트만 보고 [[…]] 안의 어디인지 가린다. C++ 파서가 아니라 편집 중의 불완전한 코드에서도
    // 돌아야 하므로 주석·리터럴은 CodeText 로 지우고 괄호만 따라간다.
    public static class AttributeContext
    {
        public static AttributeCompletionContext Analyze(string textBeforeCaret)
        {
            // 주석과 리터럴 내용을 지운 텍스트만 본다 — 인자 문자열 안의 쉼표·괄호에 속지 않는다.
            ScannedCode scanned = CodeText.Scan(textBeforeCaret, blankLiterals: true);
            int attributeStart = scanned.EndsInCode ? FindOpenAttribute(scanned.Text) : -1;
            if (attributeStart < 0)
            {
                return AttributeCompletionContext.None;
            }
            string segment = scanned.Text.Substring(attributeStart);

            int prefixStart = segment.Length;
            while (prefixStart > 0 && IsIdentifierChar(segment[prefixStart - 1]))
            {
                --prefixStart;
            }
            int prefixLength = segment.Length - prefixStart;
            if (prefixLength > 0 && char.IsDigit(segment[prefixStart]))
            {
                return AttributeCompletionContext.None;
            }
            string before = segment.Substring(0, prefixStart).TrimEnd();

            if (before.EndsWith("::", System.StringComparison.Ordinal))
            {
                string scope = TrailingIdentifier(before.Substring(0, before.Length - 2).TrimEnd());
                return scope.Length == 0 ? AttributeCompletionContext.None
                                         : new AttributeCompletionContext(AttributePosition.Member, scope, prefixLength);
            }

            string? usingScope = UsingScope(segment);
            bool afterUsingPrefix = usingScope != null && before.EndsWith(":", System.StringComparison.Ordinal) &&
                                    before.IndexOf(',') < 0;
            if (before.Length == 0 || before.EndsWith(",", System.StringComparison.Ordinal) || afterUsingPrefix)
            {
                return new AttributeCompletionContext(AttributePosition.Name, usingScope, prefixLength);
            }
            return AttributeCompletionContext.None;
        }

        // 닫히지 않은 "[[" 바로 뒤의 위치. 없거나 커서가 인자 괄호 안이면 -1. text 는 주석·리터럴을 지운 것이다.
        private static int FindOpenAttribute(string text)
        {
            int attributeStart = -1;
            int depth = 0; // attribute 안의 (), [], {} 깊이
            for (int i = 0; i < text.Length; ++i)
            {
                char c = text[i];
                char next = i + 1 < text.Length ? text[i + 1] : '\0';
                if (attributeStart < 0 && c == '[' && next == '[')
                {
                    attributeStart = i + 2;
                    depth = 0;
                    ++i;
                }
                else if (attributeStart >= 0 && depth == 0 && c == ']' && next == ']')
                {
                    attributeStart = -1;
                    ++i;
                }
                else if (attributeStart >= 0 && (c == '(' || c == '[' || c == '{'))
                {
                    ++depth;
                }
                else if (attributeStart >= 0 && depth > 0 && (c == ')' || c == ']' || c == '}'))
                {
                    --depth;
                }
            }
            return depth == 0 ? attributeStart : -1;
        }

        // [[using ns: …]] 의 ns.
        private static string? UsingScope(string segment)
        {
            string text = segment.TrimStart();
            if (!text.StartsWith("using", System.StringComparison.Ordinal) || text.Length == 5 ||
                IsIdentifierChar(text[5]))
            {
                return null;
            }
            text = text.Substring(5).TrimStart();
            int end = 0;
            while (end < text.Length && IsIdentifierChar(text[end]))
            {
                ++end;
            }
            string scope = text.Substring(0, end);
            return scope.Length > 0 && text.Substring(end).TrimStart().StartsWith(":", System.StringComparison.Ordinal)
                       ? scope
                       : null;
        }

        private static string TrailingIdentifier(string text)
        {
            int start = text.Length;
            while (start > 0 && IsIdentifierChar(text[start - 1]))
            {
                --start;
            }
            return text.Substring(start);
        }

        private static bool IsIdentifierChar(char c) => char.IsLetterOrDigit(c) || c == '_';
    }
}

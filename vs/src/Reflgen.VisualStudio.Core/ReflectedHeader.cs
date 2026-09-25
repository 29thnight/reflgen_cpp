using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Reflgen.VisualStudio
{
    // reflgen 이 다루는 header 인지를 텍스트로 가린다. header 는 생성 파일을 include 하지 않는다 — 빌드가
    // 생성물을 묶은 주입 header 를 모든 번역 단위에 강제 include 한다.
    public static class ReflectedHeader
    {
        private static readonly string[] HeaderExtensions = { ".h", ".hh", ".hpp", ".hxx", ".h++" };

        // "[[" 부터 처음 나오는 "]]" 까지가 attribute 목록 하나다 — 닫히지 않은 목록은 세지 않는다.
        private static readonly Regex AttributeList =
            new Regex(@"\[\[(.*?)\]\]", RegexOptions.Singleline | RegexOptions.CultureInvariant);
        private static readonly Regex Whitespace = new Regex(@"\s+", RegexOptions.CultureInvariant);

        public static bool IsHeaderPath(string path) =>
            HeaderExtensions.Contains(Path.GetExtension(path), StringComparer.OrdinalIgnoreCase);

        // 생성기와 같은 규칙: player.h → player.reflgen.h (빌드 중간 산출물 디렉터리에 생긴다)
        public static string GeneratedHeaderName(string headerPath) =>
            Path.GetFileNameWithoutExtension(headerPath) + ".reflgen.h";

        // 생성기가 --discover 로 header 를 고르는 규칙(attribute_scan.cpp 의 declares_reflection)과 같다. 다른 점은
        // 하나 — 여기서는 주석·문자열 안의 표기를 세지 않는다(생성기는 그런 header 도 파싱해 보고 아무것도 만들지
        // 않는다). 메서드에만 reflect 를 단 header 도 참이 되지만, 메서드 reflect 는 reflect 클래스 안에서만 뜻이
        // 있으니 실제로는 같은 뜻이다.
        public static bool DeclaresReflection(string text) =>
            HasDirective(CodeText.Blank(text, blankLiterals: true), "reflect");

        // code(주석·문자열을 비운 텍스트)의 [[…]] 목록 가운데 reflgen 지시어 directive 가 있는가:
        // [[reflgen::ignore]], [[nodiscard, reflgen::reflect("x")]], [[using reflgen: reflect]]
        public static bool HasDirective(string code, string directive)
        {
            const string UsingPrefix = "usingreflgen:";
            string name = Regex.Escape(directive);
            string scoped = $"(?<![A-Za-z0-9_])reflgen::{name}(?![A-Za-z0-9_])";
            string unscoped = $"(?<![A-Za-z0-9_]){name}(?![A-Za-z0-9_])";
            foreach (Match match in AttributeList.Matches(code))
            {
                // 공백을 뺀 목록 — "reflgen :: reflect" 도 "reflgen::reflect" 가 된다.
                string list = Whitespace.Replace(match.Groups[1].Value, string.Empty);
                bool found = list.StartsWith(UsingPrefix, StringComparison.Ordinal)
                                 ? Regex.IsMatch(list.Substring(UsingPrefix.Length), unscoped)
                                 : Regex.IsMatch(list, scoped);
                if (found)
                {
                    return true;
                }
            }
            return false;
        }
    }
}

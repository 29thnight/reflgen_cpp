using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace Reflgen.VisualStudio
{
    // 텍스트 버퍼의 Position 에 Text 를 넣는다.
    public sealed record TextInsertion(int Position, string Text);

    // reflgen 이 다루는 header 인지, 생성 파일을 include 하는지를 텍스트로 가린다.
    public static class ReflectedHeader
    {
        private static readonly string[] HeaderExtensions = { ".h", ".hh", ".hpp", ".hxx", ".h++" };

        // [[reflgen::reflect]], [[nodiscard, reflgen::reflect("x")]], [[using reflgen: reflect]]
        private static readonly Regex ReflectAttribute = new Regex(
            @"\[\[(?:\s*using\s+reflgen\s*:[^\]]*?\breflect\b|[^\]]*?\breflgen\s*::\s*reflect\b)",
            RegexOptions.CultureInvariant);

        public static bool IsHeaderPath(string path) =>
            HeaderExtensions.Contains(Path.GetExtension(path), StringComparer.OrdinalIgnoreCase);

        // 생성기와 같은 규칙: player.h → player.reflgen.h
        public static string GeneratedHeaderName(string headerPath) =>
            Path.GetFileNameWithoutExtension(headerPath) + ".reflgen.h";

        // 주석·문자열 안의 표기는 세지 않는다. 메서드에만 reflect 를 단 header 도 참이 되지만, 메서드
        // reflect 는 reflect 클래스 안에서만 뜻이 있으니 실제로는 같은 뜻이다.
        public static bool DeclaresReflection(string text) =>
            ReflectAttribute.IsMatch(CodeText.Blank(text, blankLiterals: true));

        // 대소문자는 가리지 않는다(Windows 에서는 같은 파일이다). 주석 처리된 include 는 세지 않는다.
        public static bool IncludesGenerated(string text, string generatedName)
        {
            var include = new Regex(@"^[ \t]*#[ \t]*include[ \t]*[""<](?:[^"">\r\n]*[/\\])?" +
                                        Regex.Escape(generatedName) + @"["">]",
                                    RegexOptions.Multiline | RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
            return include.IsMatch(CodeText.Blank(text, blankLiterals: false));
        }

        // 반영을 선언했는데 생성 파일을 include 하지 않으면 파일 끝에 넣을 것. 할 일이 없으면 null.
        // 개행 문자는 파일이 쓰는 것을 따르고, 앞 코드와는 빈 줄 하나로 띄운다.
        public static TextInsertion? IncludeInsertion(string text, string headerPath)
        {
            string name = GeneratedHeaderName(headerPath);
            if (!DeclaresReflection(text) || IncludesGenerated(text, name))
            {
                return null;
            }
            string newline = text.Contains("\r\n") ? "\r\n" : "\n";
            string separator = text.EndsWith(newline + newline, StringComparison.Ordinal) ? string.Empty
                               : text.EndsWith(newline, StringComparison.Ordinal)            ? newline
                                                                                             : newline + newline;
            return new TextInsertion(text.Length, separator + "#include \"" + name + "\"" + newline);
        }
    }
}

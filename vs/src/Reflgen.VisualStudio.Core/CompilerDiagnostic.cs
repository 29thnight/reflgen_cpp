using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace Reflgen.VisualStudio
{
    public enum DiagnosticSeverity
    {
        Warning,
        Error,
    }

    // 생성기·MSBuild 가 내는 진단 한 줄. Line·Column 은 1부터(없으면 0)이고, File 이 없으면(옵션 오류
    // 등) 위치 없는 진단이다.
    public sealed record CompilerDiagnostic(string? File, int Line, int Column, DiagnosticSeverity Severity, string Code,
                                            string Message)
    {
        public override string ToString()
        {
            string where = File is null ? "reflgen" : Line > 0 ? $"{File}({Line},{Column})" : File;
            return $"{where}: {(Severity == DiagnosticSeverity.Error ? "error" : "warning")} {Code}: {Message}";
        }
    }

    // MSVC 형식("file(line,col): error RG0002: message")을 읽는다. 생성기·cl·MSBuild 모두 이 형식이고,
    // "error"/"warning" 은 지역화되지 않는다.
    public static class DiagnosticParser
    {
        private static readonly Regex Pattern = new Regex(
            @"^\s*(?<origin>.*?)(\((?<line>\d+)(,(?<column>\d+))?\))?\s*:\s*(?<severity>fatal error|error|warning)\s+" +
                @"(?<code>[A-Za-z]+\d+)\s*:\s*(?<message>.*?)\s*$",
            RegexOptions.CultureInvariant);

        // MSBuild 가 줄 끝에 붙이는 " [C:\…\project.vcxproj]".
        private static readonly Regex ProjectSuffix = new Regex(@"\s+\[[^\]]+\.[A-Za-z]*proj\]\s*$",
                                                                RegexOptions.CultureInvariant);

        // Exec 의 "명령이 코드 1 로 끝났다" — 원인은 이미 앞 줄들에 있다.
        private static readonly HashSet<string> Redundant = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "MSB3073",
        };

        public static CompilerDiagnostic? ParseLine(string text)
        {
            string line = ProjectSuffix.Replace(text, string.Empty);
            Match match = Pattern.Match(line);
            if (!match.Success)
            {
                return null;
            }
            string code = match.Groups["code"].Value;
            if (Redundant.Contains(code))
            {
                return null;
            }
            string origin = match.Groups["origin"].Value.Trim();
            // "reflgen : error RG0001" 처럼 도구 이름만 있으면 위치가 없다.
            bool hasFile = match.Groups["line"].Success || LooksLikePath(origin);
            return new CompilerDiagnostic(
                hasFile ? origin : null,
                match.Groups["line"].Success ? int.Parse(match.Groups["line"].Value) : 0,
                match.Groups["column"].Success ? int.Parse(match.Groups["column"].Value) : 0,
                match.Groups["severity"].Value == "warning" ? DiagnosticSeverity.Warning : DiagnosticSeverity.Error,
                code,
                match.Groups["message"].Value);
        }

        // 같은 진단이 MSBuild 요약에서 다시 나오므로 겹치는 것은 한 번만 돌려준다.
        public static IReadOnlyList<CompilerDiagnostic> Parse(IEnumerable<string> lines)
        {
            var result = new List<CompilerDiagnostic>();
            var seen = new HashSet<CompilerDiagnostic>();
            foreach (string line in lines)
            {
                CompilerDiagnostic? diagnostic = ParseLine(line);
                if (diagnostic != null && seen.Add(diagnostic))
                {
                    result.Add(diagnostic);
                }
            }
            return result;
        }

        private static bool LooksLikePath(string origin)
        {
            return origin.IndexOf('/') >= 0 || origin.IndexOf('\\') >= 0 || origin.IndexOf('.') >= 0;
        }
    }
}

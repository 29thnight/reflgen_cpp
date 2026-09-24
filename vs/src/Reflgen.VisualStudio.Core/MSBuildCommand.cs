using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Reflgen.VisualStudio
{
    // 저장할 때 프로젝트 하나의 ReflgenGenerate target 만 부르는 MSBuild 명령줄.
    public static class MSBuildCommand
    {
        public const string TargetName = "ReflgenGenerate";

        // 콘솔 출력은 코드 페이지를 타므로(한국어 Windows 는 949) 진단은 UTF-8 파일 로그로 받는다.
        // solutionFile 을 주면 VS 가 빌드할 때처럼 $(SolutionDir) 등을 넘긴다 — 프로젝트 경로 설정이 흔히
        // 그것에 기댄다.
        public static string Arguments(string projectFile, string configuration, string platform, string? solutionFile,
                                       string logFile)
        {
            var arguments = new List<string>
            {
                projectFile,
                "/t:" + TargetName,
                "/nologo",
                "/noconsolelogger",
                "/nodeReuse:false",
                "/p:Configuration=" + configuration,
                "/p:Platform=" + platform,
                "/flp:LogFile=" + logFile + ";Encoding=UTF-8;Verbosity=minimal",
            };
            if (!string.IsNullOrEmpty(solutionFile))
            {
                string directory = Path.GetDirectoryName(solutionFile) ?? string.Empty;
                arguments.Add("/p:SolutionDir=" + directory.TrimEnd('\\') + "\\");
                arguments.Add("/p:SolutionPath=" + solutionFile);
                arguments.Add("/p:SolutionFileName=" + Path.GetFileName(solutionFile));
                arguments.Add("/p:SolutionName=" + Path.GetFileNameWithoutExtension(solutionFile));
                arguments.Add("/p:SolutionExt=" + Path.GetExtension(solutionFile));
            }
            return string.Join(" ", arguments.Select(CommandLine.Quote));
        }
    }

    public static class CommandLine
    {
        // Windows CRT 의 명령줄 규칙대로 인자 하나를 감싼다. 닫는 따옴표 앞의 역슬래시는 두 배로 적어야
        // 한다 — "C:\dir\" 는 따옴표가 먹혀 뒤 인자와 합쳐진다.
        public static string Quote(string argument)
        {
            if (argument.Length > 0 && argument.IndexOfAny(new[] { ' ', '\t', '"' }) < 0)
            {
                return argument;
            }
            var quoted = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char c in argument)
            {
                if (c == '\\')
                {
                    ++backslashes;
                    continue;
                }
                quoted.Append('\\', c == '"' ? backslashes * 2 + 1 : backslashes);
                quoted.Append(c);
                backslashes = 0;
            }
            quoted.Append('\\', backslashes * 2);
            quoted.Append('"');
            return quoted.ToString();
        }
    }
}

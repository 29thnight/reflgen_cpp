using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class DiagnosticParserTests
    {
        [Fact]
        public void ParseLine_ReadsGeneratorDiagnosticWithColumn()
        {
            CompilerDiagnostic? diagnostic = DiagnosticParser.ParseLine(
                "C:/game/player.h(9,28): error RG0002: 'game::player' reflects the non-public member 'secret_'");

            Assert.NotNull(diagnostic);
            Assert.Equal("C:/game/player.h", diagnostic!.File);
            Assert.Equal(9, diagnostic.Line);
            Assert.Equal(28, diagnostic.Column);
            Assert.Equal(DiagnosticSeverity.Error, diagnostic.Severity);
            Assert.Equal("RG0002", diagnostic.Code);
            Assert.Equal("'game::player' reflects the non-public member 'secret_'", diagnostic.Message);
        }

        [Fact]
        public void ParseLine_ReadsWarningWithoutColumn()
        {
            CompilerDiagnostic? diagnostic =
                DiagnosticParser.ParseLine(@"  C:\game\item.h(16): warning RG0004: bit-field 'bits' cannot be reflected");

            Assert.NotNull(diagnostic);
            Assert.Equal(@"C:\game\item.h", diagnostic!.File);
            Assert.Equal(16, diagnostic.Line);
            Assert.Equal(0, diagnostic.Column);
            Assert.Equal(DiagnosticSeverity.Warning, diagnostic.Severity);
        }

        [Fact]
        public void ParseLine_TreatsToolNameAsNoLocation()
        {
            CompilerDiagnostic? diagnostic = DiagnosticParser.ParseLine("reflgen : error RG0001: --output is required");

            Assert.NotNull(diagnostic);
            Assert.Null(diagnostic!.File);
            Assert.Equal("RG0001", diagnostic.Code);
        }

        [Fact]
        public void ParseLine_DropsMSBuildProjectSuffix()
        {
            CompilerDiagnostic? diagnostic = DiagnosticParser.ParseLine(
                @"C:\game\player.h(3,1): error RG0100: clang: 'missing.h' file not found [C:\game\game.vcxproj]");

            Assert.Equal("clang: 'missing.h' file not found", diagnostic!.Message);
        }

        [Fact]
        public void ParseLine_SkipsExecExitCodeAndPlainText()
        {
            Assert.Null(DiagnosticParser.ParseLine(
                @"C:\reflgen\reflgen.targets(134,5): error MSB3073: ""reflgen.exe"" exited with code 1. [C:\g.vcxproj]"));
            Assert.Null(DiagnosticParser.ParseLine("  reflgen_game.vcxproj -> C:\\out\\game.exe"));
            Assert.Null(DiagnosticParser.ParseLine(string.Empty));
        }

        // MSBuild 파일 로그에서 그대로 가져온 줄(한글·공백 경로, 지역화된 MSB3073).
        [Fact]
        public void Parse_ReadsARealMSBuildLog()
        {
            string[] log =
            {
                "C:/work/한글 폴더/bad_types.h(9,28): error RG0002: 'bad::hidden_without_friend' reflects the " +
                    "non-public member 'secret_'; add `friend struct reflgen::access;` to the class [C:\\work\\e2e.vcxproj]",
                "C:/work/한글 폴더/bad_types.h(1,1): warning RG0003: this header does not include \"bad_types.reflgen.h\"" +
                    " [C:\\work\\e2e.vcxproj]",
                "C:\\reflgen\\msbuild\\reflgen.targets(141,5): error MSB3073: \"\"C:\\bin\\reflgen.exe\" " +
                    "@\"C:\\work\\x64\\Debug\\reflgen\\reflgen_e2e.rsp\"\" 명령이 종료되었습니다(코드: 1). [C:\\work\\e2e.vcxproj]",
            };

            var diagnostics = DiagnosticParser.Parse(log);

            Assert.Equal(2, diagnostics.Count);
            Assert.Equal("C:/work/한글 폴더/bad_types.h", diagnostics[0].File);
            Assert.EndsWith("to the class", diagnostics[0].Message);
            Assert.Equal(DiagnosticSeverity.Warning, diagnostics[1].Severity);
            Assert.Equal("RG0003", diagnostics[1].Code);
        }

        [Fact]
        public void Parse_ReturnsEachDiagnosticOnce()
        {
            string line = "C:/game/player.h(9,28): error RG0002: message";

            var diagnostics = DiagnosticParser.Parse(new[] { line, "Build started.", line + " [C:\\g.vcxproj]" });

            Assert.Single(diagnostics);
        }
    }
}

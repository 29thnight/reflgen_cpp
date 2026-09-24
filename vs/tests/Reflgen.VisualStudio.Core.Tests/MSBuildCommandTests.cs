using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class MSBuildCommandTests
    {
        [Theory]
        [InlineData("plain", "plain")]
        [InlineData(@"C:\dir\", @"C:\dir\")]                            // 따옴표가 없으면 역슬래시는 그대로
        [InlineData(@"C:\my dir\", @"""C:\my dir\\""")]                  // 닫는 따옴표 앞 역슬래시는 두 배
        [InlineData(@"say ""hi""", @"""say \""hi\""""")]
        [InlineData(@"a\""b", @"""a\\\""b""")]
        [InlineData("", @"""""")]
        public void Quote_FollowsTheWindowsRules(string argument, string expected)
        {
            Assert.Equal(expected, CommandLine.Quote(argument));
        }

        [Fact]
        public void Arguments_RunOnlyTheGenerateTargetWithAUtf8Log()
        {
            string arguments = MSBuildCommand.Arguments(@"C:\game\game.vcxproj", "Debug", "x64", null, @"C:\temp\log.txt");

            Assert.Equal(@"C:\game\game.vcxproj /t:ReflgenGenerate /nologo /noconsolelogger /nodeReuse:false " +
                             @"/p:Configuration=Debug /p:Platform=x64 " +
                             @"/flp:LogFile=C:\temp\log.txt;Encoding=UTF-8;Verbosity=minimal",
                         arguments);
        }

        [Fact]
        public void Arguments_PassSolutionPropertiesLikeVisualStudio()
        {
            string arguments = MSBuildCommand.Arguments(@"C:\My Games\game\game.vcxproj", "Release", "Win32",
                                                        @"C:\My Games\Game.sln", @"C:\temp\log.txt");

            Assert.Contains(@"""C:\My Games\game\game.vcxproj""", arguments);
            Assert.Contains(@"""/p:SolutionDir=C:\My Games\\""", arguments);
            Assert.Contains(@"""/p:SolutionPath=C:\My Games\Game.sln""", arguments);
            Assert.Contains("/p:SolutionName=Game ", arguments);
            Assert.Contains("/p:SolutionExt=.sln", arguments);
            Assert.Contains("/p:Platform=Win32", arguments);
        }
    }
}

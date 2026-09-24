using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class ReflectedHeaderTests
    {
        private const string Player =
            "#pragma once\n#include \"reflgen/reflgen.h\"\n\nstruct [[reflgen::reflect]] player\n{\n    int hp = 0;\n};\n";

        [Theory]
        [InlineData("struct [[reflgen::reflect]] player {};")]
        [InlineData("class [[nodiscard, reflgen::reflect(\"game.player\")]] player {};")]
        [InlineData("enum class [[ reflgen :: reflect ]] element { fire };")]
        [InlineData("struct [[using reflgen: reflect, hidden]] player {};")]
        [InlineData("constexpr auto kText = R\"(say \"hi\")\"; struct [[reflgen::reflect]] player {};")]
        [InlineData("constexpr auto kText = R\"x(a )\" b)x\"; struct [[reflgen::reflect]] player {};")]
        [InlineData("auto s = u8R\"(\n\"\n)\";\nstruct [[reflgen::reflect]] player {};")]
        [InlineData("char c = L'\"'; struct [[reflgen::reflect]] player {};")]
        [InlineData("int n = 1'000'000; struct [[reflgen::reflect]] player {};")]
        public void DeclaresReflection_FindsTheDirective(string text)
        {
            Assert.True(ReflectedHeader.DeclaresReflection(text));
        }

        [Theory]
        [InlineData("// struct [[reflgen::reflect]] player {};")]
        [InlineData("/* [[reflgen::reflect]] */ struct player {};")]
        [InlineData("const char* text = \"[[reflgen::reflect]]\";")]
        [InlineData("struct [[reflgen::reflected]] player {};")]
        [InlineData("struct [[mygame::reflect]] player {};")]
        [InlineData("auto text = R\"([[reflgen::reflect]])\";")]
        [InlineData("auto text = R\"x(\n)\" [[reflgen::reflect]]\n)x\";")]
        public void DeclaresReflection_IgnoresCommentsLiteralsAndOtherNames(string text)
        {
            Assert.False(ReflectedHeader.DeclaresReflection(text));
        }

        [Theory]
        [InlineData("#include \"player.reflgen.h\"")]
        [InlineData("  #  include <generated/Player.Reflgen.h>")]
        public void IncludesGenerated_MatchesRegardlessOfCaseAndDirectory(string text)
        {
            Assert.True(ReflectedHeader.IncludesGenerated(text, "player.reflgen.h"));
        }

        [Theory]
        [InlineData("// #include \"player.reflgen.h\"")]
        [InlineData("#include \"other_player.reflgen.h\"")]
        [InlineData("#include \"player.reflgen.hpp\"")]
        public void IncludesGenerated_RejectsCommentsAndOtherFiles(string text)
        {
            Assert.False(ReflectedHeader.IncludesGenerated(text, "player.reflgen.h"));
        }

        [Fact]
        public void IncludeInsertion_AppendsAfterABlankLine()
        {
            TextInsertion? insertion = ReflectedHeader.IncludeInsertion(Player, @"C:\game\player.h");

            Assert.NotNull(insertion);
            Assert.Equal(Player.Length, insertion!.Position);
            Assert.Equal("\n#include \"player.reflgen.h\"\n", insertion.Text);
        }

        [Fact]
        public void IncludeInsertion_KeepsWindowsLineEndingsAndMissingFinalNewline()
        {
            string text = Player.Replace("\n", "\r\n").TrimEnd();

            TextInsertion? insertion = ReflectedHeader.IncludeInsertion(text, "player.hpp");

            Assert.Equal("\r\n\r\n#include \"player.reflgen.h\"\r\n", insertion!.Text);
        }

        [Fact]
        public void IncludeInsertion_DoesNotAddASecondBlankLine()
        {
            TextInsertion? insertion = ReflectedHeader.IncludeInsertion(Player + "\n", "player.h");

            Assert.Equal("#include \"player.reflgen.h\"\n", insertion!.Text);
        }

        [Fact]
        public void IncludeInsertion_DoesNothingWhenIncludedOrNotReflected()
        {
            Assert.Null(ReflectedHeader.IncludeInsertion(Player + "\n#include \"player.reflgen.h\"\n", "player.h"));
            Assert.Null(ReflectedHeader.IncludeInsertion("struct plain {};\n", "plain.h"));
            Assert.Null(ReflectedHeader.IncludeInsertion(string.Empty, "empty.h"));
        }

        [Theory]
        [InlineData("player.h", true)]
        [InlineData("PLAYER.HPP", true)]
        [InlineData("player.hxx", true)]
        [InlineData("player.cpp", false)]
        [InlineData("player.reflgen", false)]
        public void IsHeaderPath_KnowsHeaderExtensions(string path, bool expected)
        {
            Assert.Equal(expected, ReflectedHeader.IsHeaderPath(path));
        }

        [Fact]
        public void GeneratedHeaderName_FollowsTheGeneratorRule()
        {
            Assert.Equal("player.reflgen.h", ReflectedHeader.GeneratedHeaderName(@"C:\game\player.hpp"));
        }

        [Fact]
        public void CodeText_BlanksWithoutMovingPositions()
        {
            const string text = "a /* b */ \"c\" // d\ne";

            string blanked = CodeText.Blank(text, blankLiterals: true);

            Assert.Equal(text.Length, blanked.Length);
            Assert.Equal("a         \" \"     \ne", blanked);
        }
    }
}

using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class ReflectedHeaderTests
    {
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
        [InlineData("struct [[reflgen::reflect a {};")] // 닫히지 않은 목록 — 생성기도 세지 않는다
        [InlineData("struct [[using other: reflect]] player {};")]
        public void DeclaresReflection_IgnoresCommentsLiteralsAndOtherNames(string text)
        {
            Assert.False(ReflectedHeader.DeclaresReflection(text));
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

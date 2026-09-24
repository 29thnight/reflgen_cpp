using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class AttributeContextTests
    {
        [Theory]
        [InlineData("struct [[", 0)]
        [InlineData("struct [[refl", 4)]
        [InlineData("[[reflgen::reflect, ", 0)]
        [InlineData("int x;\n    [[nodiscard, gam", 3)]
        public void Analyze_SuggestsScopesAfterOpeningOrComma(string text, int prefixLength)
        {
            AttributeCompletionContext context = AttributeContext.Analyze(text);

            Assert.Equal(AttributePosition.Name, context.Position);
            Assert.Null(context.Scope);
            Assert.Equal(prefixLength, context.PrefixLength);
        }

        [Theory]
        [InlineData("[[reflgen::", "reflgen", 0)]
        [InlineData("[[reflgen::ra", "reflgen", 2)]
        [InlineData("[[reflgen :: ra", "reflgen", 2)]
        [InlineData("[[reflgen::range(0, max_hp), game::tool", "game", 4)]
        [InlineData("[[reflgen::display_name(\"a]]b\"), reflgen::", "reflgen", 0)]
        [InlineData("[[reflgen::range(values[0], 1), reflgen::", "reflgen", 0)]
        public void Analyze_SuggestsMembersAfterScope(string text, string scope, int prefixLength)
        {
            AttributeCompletionContext context = AttributeContext.Analyze(text);

            Assert.Equal(AttributePosition.Member, context.Position);
            Assert.Equal(scope, context.Scope);
            Assert.Equal(prefixLength, context.PrefixLength);
        }

        [Theory]
        [InlineData("[[using reflgen: ", 0)]
        [InlineData("[[using reflgen: display_name(\"x\"), hid", 3)]
        public void Analyze_UsesTheUsingPrefixAsDefaultScope(string text, int prefixLength)
        {
            AttributeCompletionContext context = AttributeContext.Analyze(text);

            Assert.Equal(AttributePosition.Name, context.Position);
            Assert.Equal("reflgen", context.Scope);
            Assert.Equal(prefixLength, context.PrefixLength);
        }

        [Theory]
        [InlineData("")]
        [InlineData("int x = values[index]")]
        [InlineData("[[reflgen::reflect]] struct player { int ")]
        [InlineData("[[reflgen::range(0, ma")]                  // 인자 괄호 안
        [InlineData("// [[reflgen::")]                          // 주석
        [InlineData("/* [[reflgen:: */ int ")]                  // 닫힌 블록 주석 뒤
        [InlineData("auto text = \"[[reflgen::")]               // 문자열
        [InlineData("[[reflgen:")]                              // 콜론 하나
        [InlineData("[[reflgen::reflect ")]                     // 이름 뒤, 쉼표 없음
        [InlineData("[[reflgen::1")]                            // 숫자로 시작
        public void Analyze_StaysOutOfEverythingElse(string text)
        {
            Assert.Equal(AttributePosition.None, AttributeContext.Analyze(text).Position);
        }

        [Theory]
        [InlineData("int n = 1'000; [[reflgen::")]                       // 자릿수 구분자
        [InlineData("char c = u8'['; [[reflgen::")]                      // 접두 문자 리터럴
        [InlineData("auto t = R\"(say \"hi\")\"; [[reflgen::")]          // raw 문자열 안의 따옴표
        [InlineData("[[reflgen::display_name(R\"(a]]b)\"), reflgen::")] // 인자 안의 raw 문자열
        [InlineData("[[reflgen::display_name(\"a, b\"), reflgen::")]
        public void Analyze_SeesThroughLiterals(string text)
        {
            AttributeCompletionContext context = AttributeContext.Analyze(text);

            Assert.Equal(AttributePosition.Member, context.Position);
            Assert.Equal("reflgen", context.Scope);
        }

        [Theory]
        [InlineData("auto t = R\"([[reflgen::")]        // 닫히지 않은 raw 문자열 안
        [InlineData("auto t = R\"x(\n)\"\n[[reflgen::")] // 여러 줄 raw 문자열 안
        public void Analyze_StaysOutOfRawStrings(string text)
        {
            Assert.Equal(AttributePosition.None, AttributeContext.Analyze(text).Position);
        }
    }
}

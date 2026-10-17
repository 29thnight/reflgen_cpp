using System.Linq;
using Xunit;
namespace Reflgen.VisualStudio.Tests
{
    public class CppCompletionPolicyTests
    {
        [Theory]
        [InlineData("stdcpp20", 20)]
        [InlineData("stdcpp23preview", 23)]
        [InlineData("stdcpp26", 26)]
        [InlineData("stdcpplatest", 26)]
        public void ReadsProjectStandard(string setting, int expected) => Assert.Equal(expected, CppCompletionPolicy.Version(setting));
        [Fact]
        public void StandardItemsFollowVersion()
        {
            Assert.DoesNotContain(CppCompletionPolicy.StandardAttributes(20), e => e.Name == "assume");
            Assert.Contains(CppCompletionPolicy.StandardAttributes(23), e => e.Name == "assume");
            Assert.DoesNotContain(CppCompletionPolicy.StandardAttributes(26), e => e.Name == "carries_dependency");
            Assert.Contains(CppCompletionPolicy.StandardAttributes(26), e => e.Name == "nodiscard");
            Assert.DoesNotContain(CppCompletionPolicy.StandardAttributes(23), e => e.Name == "indeterminate");
            Assert.Contains(CppCompletionPolicy.StandardAttributes(26), e => e.Name == "indeterminate");
        }
        [Theory]
        [InlineData("[[=", AttributePosition.Name, null)]
        [InlineData("[[=refl", AttributePosition.Name, null)]
        [InlineData("[[=reflgen::", AttributePosition.Member, "reflgen")]
        [InlineData("[[=reflgen::ignore{}, =editor::tip", AttributePosition.Member, "editor")]
        [InlineData("[[=reflgen::range(0, ", AttributePosition.None, null)]
        public void AnnotationContext(string text, AttributePosition position, string? scope)
        {
            var context = AttributeContext.Analyze(text);
            Assert.Equal(position, context.Position);
            Assert.Equal(scope, context.Scope);
            if (position != AttributePosition.None) Assert.True(context.IsAnnotation);
        }
        [Fact]
        public void AnnotationCompletionInsertsValues()
        {
            var ignore = AttributeCatalog.BuiltIn.InScope("reflgen").Single(e => e.Name == "ignore");
            var range = AttributeCatalog.BuiltIn.InScope("reflgen").Single(e => e.Name == "range");
            Assert.Equal("ignore{}", CppCompletionPolicy.InsertText(ignore, true));
            Assert.Equal("range(", CppCompletionPolicy.InsertText(range, true));
            Assert.Equal("ignore", CppCompletionPolicy.InsertText(ignore, false));
        }
    }
}

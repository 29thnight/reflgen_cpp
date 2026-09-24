using System.Linq;
using Xunit;

namespace Reflgen.VisualStudio.Tests
{
    public sealed class AttributeCatalogTests
    {
        // 생성기가 쓰는 모양 그대로(탭 구분, 역슬래시 escape).
        private const string GeneratorOutput =
            "reflgen-attributes\t1\n" +
            "directive\treflgen\treflect\treflect\\nreflect(\"schema.name\")\tMarks a type.\n" +
            "attribute\treflgen\trange\trange(T min_value, T max_value)\tAllowed interval.\n" +
            "attribute\treflgen\ttransient\ttransient\tNot saved.\n" +
            "attribute\tgame\ttooltip\ttooltip(std::string_view text)\tTab\\there, slash \\\\ end.\n";

        [Fact]
        public void Parse_ReadsGeneratorOutput()
        {
            AttributeCatalog catalog = AttributeCatalog.Parse(GeneratorOutput);

            Assert.Equal(4, catalog.Entries.Count);
            AttributeEntry reflect = catalog.Entries[0];
            Assert.Equal(AttributeKind.Directive, reflect.Kind);
            Assert.Equal("reflect\nreflect(\"schema.name\")", reflect.Signature);
            Assert.Equal("Tab\there, slash \\ end.", catalog.Entries[3].Summary);
            Assert.Equal(new[] { "game", "reflgen" }, catalog.Scopes.ToArray());
            Assert.Equal(new[] { "reflect", "range", "transient" },
                         catalog.InScope("reflgen").Select(entry => entry.Name).ToArray());
        }

        [Fact]
        public void Parse_AcceptsWindowsLineEndings()
        {
            AttributeCatalog catalog = AttributeCatalog.Parse(GeneratorOutput.Replace("\n", "\r\n"));

            Assert.Equal("Allowed interval.", catalog.Entries[1].Summary);
        }

        [Fact]
        public void Parse_RejectsUnknownFormatAndSkipsBrokenLines()
        {
            Assert.Empty(AttributeCatalog.Parse("something else\n").Entries);
            Assert.Empty(AttributeCatalog.Parse(string.Empty).Entries);

            AttributeCatalog catalog = AttributeCatalog.Parse("reflgen-attributes\t1\nbroken line\n\n" +
                                                              "attribute\treflgen\trange\tsig\tsummary\textra\n");
            Assert.Empty(catalog.Entries); // 필드가 모자라도, 남아도 버린다
        }

        [Fact]
        public void TakesArguments_FollowsTheSignature()
        {
            AttributeCatalog catalog = AttributeCatalog.Parse(GeneratorOutput);

            Assert.True(catalog.Entries[1].TakesArguments);  // range(...)
            Assert.False(catalog.Entries[2].TakesArguments); // transient
        }

        [Fact]
        public void Merge_KeepsTheFirstEntryForEachName()
        {
            AttributeCatalog project = AttributeCatalog.Parse(GeneratorOutput);

            AttributeCatalog merged = AttributeCatalog.Merge(new[] { project, AttributeCatalog.BuiltIn });

            Assert.Equal("Allowed interval.", merged.InScope("reflgen").Single(entry => entry.Name == "range").Summary);
            Assert.Contains(merged.InScope("reflgen"), entry => entry.Name == "display_name"); // 내장에서 채워진다
            Assert.Single(merged.InScope("game"));
        }

        [Fact]
        public void BuiltIn_HasTheDirectivesAndLibraryAttributes()
        {
            string[] names = AttributeCatalog.BuiltIn.InScope("reflgen").Select(entry => entry.Name).ToArray();

            Assert.Contains("reflect", names);
            Assert.Contains("ignore", names);
            Assert.Contains("range", names);
            Assert.Contains("serialized_name", names);
        }
    }
}

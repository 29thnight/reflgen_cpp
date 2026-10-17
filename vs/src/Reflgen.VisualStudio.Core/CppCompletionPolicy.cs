using System;
using System.Collections.Generic;
using System.Linq;

namespace Reflgen.VisualStudio
{
    public static class CppCompletionPolicy
    {
        public static int Version(string? setting)
        {
            string text = (setting ?? "").ToLowerInvariant();
            if (text.Contains("latest") || text.Contains("26") || text.Contains("2c")) return 26;
            if (text.Contains("23") || text.Contains("2b")) return 23;
            if (text.Contains("20") || text.Contains("2a")) return 20;
            if (text.Contains("17")) return 17;
            if (text.Contains("14")) return 14;
            return 20; // conservative default for an unbound editor; reflgen requires C++20
        }

        public static IEnumerable<AttributeEntry> StandardAttributes(int version) =>
            AttributeCatalog.Standard.Entries.Where(entry => MinimumVersion(entry.Name) <= version &&
                !(entry.Name == "carries_dependency" && version >= 26));

        private static int MinimumVersion(string name) => name switch
        {
            "deprecated" => 14,
            "nodiscard" or "maybe_unused" or "fallthrough" => 17,
            "no_unique_address" or "likely" or "unlikely" => 20,
            "assume" => 23,
            "indeterminate" => 26,
            _ => 11,
        };

        // In annotations an entry must be a value expression, not an attribute token.
        public static string InsertText(AttributeEntry entry, bool annotation) =>
            !annotation ? entry.Name : entry.Name + (entry.TakesArguments ? "(" : "{}");
    }
}

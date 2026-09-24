using System.ComponentModel;
using Microsoft.VisualStudio.Shell;

namespace Reflgen.VisualStudio
{
    // Tools > Options > reflgen > General
    public sealed class ReflgenOptionsPage : DialogPage
    {
        [Category("On save")]
        [DisplayName("Add the generated include")]
        [Description("When a header with [[reflgen::reflect]] is saved, append #include \"<name>.reflgen.h\" if it is missing.")]
        public bool AddIncludeOnSave { get; set; } = true;

        [Category("On save")]
        [DisplayName("Register the header in the project")]
        [Description("When a header with [[reflgen::reflect]] is saved, mark its ClInclude item with ReflgenGenerate=true.")]
        public bool RegisterOnSave { get; set; } = true;

        [Category("On save")]
        [DisplayName("Generate reflection")]
        [Description("Run the project's ReflgenGenerate target after a registered header is saved and show its diagnostics in the Error List.")]
        public bool GenerateOnSave { get; set; } = true;

        [Category("MSBuild")]
        [DisplayName("MSBuild.exe path")]
        [Description("Leave empty to use the MSBuild that ships with this Visual Studio.")]
        public string MSBuildPath { get; set; } = string.Empty;
    }
}

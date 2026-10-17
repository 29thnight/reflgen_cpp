using System.ComponentModel;
using Microsoft.VisualStudio.Shell;

namespace Reflgen.VisualStudio
{
    // Tools > Options > reflgen > General
    public sealed class ReflgenOptionsPage : DialogPage
    {
        [Category("On save")]
        [DisplayName("Add friend struct reflgen::access")]
        [Description("When a [[reflgen::reflect]] class reflects non-public members and lacks `friend struct reflgen::access;`, insert it at the top of the class body. The generated code lives outside the class and needs it; C++20/23 on MSVC offers no other way.")]
        public bool AddFriendOnSave { get; set; } = true;

        [Category("On save")]
        [DisplayName("Generate reflection")]
        [Description("Run ReflgenGenerate after a reflected header is saved, or route a module interface save to its configured IFC reflection project. IFC generation first builds the module provider. Also generate when a never-generated project opens, and show diagnostics in the Error List.")]
        public bool GenerateOnSave { get; set; } = true;

        [Category("On save")]
        [DisplayName("Refresh IntelliSense")]
        [Description("When generation changed the generated code, run Project > Rescan Solution so the editor sees it. IntelliSense does not notice changes to force-included files outside the project on its own.")]
        public bool RefreshIntelliSense { get; set; } = true;

        [Category("MSBuild")]
        [DisplayName("MSBuild.exe path")]
        [Description("Leave empty to use the MSBuild that ships with this Visual Studio.")]
        public string MSBuildPath { get; set; } = string.Empty;
    }
}

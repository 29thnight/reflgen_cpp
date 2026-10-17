# Consume installed tools/targets/module sources, without pointing at source-tree tools or headers.
param(
    [Parameter(Mandatory)][string]$Prefix,
    [Parameter(Mandatory)][string]$Work
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Prefix = [IO.Path]::GetFullPath($Prefix)
$Work = [IO.Path]::GetFullPath($Work)
if (Test-Path -LiteralPath $Work) { throw 'Use a fresh Work directory for the installed module consumer' }
New-Item -ItemType Directory -Path $Work | Out-Null
Copy-Item (Join-Path $root 'tests/ifc/mixed/*') $Work
Copy-Item (Join-Path $root 'tests/ifc/msbuild/common.props') $Work
$utf8 = New-Object System.Text.UTF8Encoding($false)
$main = Join-Path $Work 'main.cpp'
$text = [IO.File]::ReadAllText($main).Replace('#include "reflgen/json.h"', '#include "reflgen/generated.h"' + "`n" + 'import reflgen.json;')
[IO.File]::WriteAllText($main, $text, $utf8)
$project = Join-Path $Work 'mixed.vcxproj'
$text = [IO.File]::ReadAllText($project).Replace('<ClInclude Include="settings.h" />', '<ClInclude Include="settings.h" /><ProjectReference Include="library.vcxproj"><Project>{87FB4E12-BE4B-4B2D-9413-50F00F534EAB}</Project></ProjectReference>')
[IO.File]::WriteAllText($project, $text, $utf8)
$include = [Security.SecurityElement]::Escape((Join-Path $Prefix 'include'))
$sources = Get-ChildItem (Join-Path $Prefix 'share/reflgen/modules') -Filter '*.ixx' | ForEach-Object {
    '<ClCompile Include="' + [Security.SecurityElement]::Escape($_.FullName) + '"><CompileAs>CompileAsCppModule</CompileAs></ClCompile>'
}
$library = @'
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><ProjectGuid>{87FB4E12-BE4B-4B2D-9413-50F00F534EAB}</ProjectGuid><IfcTestKind>StaticLibrary</IfcTestKind></PropertyGroup>
<Import Project="common.props"/>
<ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>INCLUDE_PATH;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>
<ItemGroup>MODULE_SOURCES</ItemGroup>
<Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets"/>
</Project>
'@
$library = $library.Replace('INCLUDE_PATH', $include).Replace('MODULE_SOURCES', ($sources -join "`n"))
[IO.File]::WriteAllText((Join-Path $Work 'library.vcxproj'), $library, $utf8)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/amd64/MSBuild.exe' | Select-Object -First 1
& $msbuild $project /t:Build /nologo /v:minimal /nodeReuse:false /p:Configuration=Release /p:Platform=x64 `
    "/p:IfcTestWork=$Work/out/" "/p:ReflgenTargetsFile=$Prefix/share/reflgen/msbuild/reflgen.targets"
if ($LASTEXITCODE -ne 0) { throw 'Installed mixed/module consumer build failed' }
& (Join-Path $Work 'out/Release/bin/mixed.exe')
if ($LASTEXITCODE -ne 0) { throw 'Installed mixed/module consumer execution failed' }

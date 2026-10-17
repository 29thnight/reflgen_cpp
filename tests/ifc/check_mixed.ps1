param(
    [Parameter(Mandatory)][string]$Generator,
    [Parameter(Mandatory)][string]$HeaderGenerator,
    [Parameter(Mandatory)][string]$MSBuild,
    [Parameter(Mandatory)][string]$Work,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$Work = [IO.Path]::GetFullPath($Work).TrimEnd('\', '/') + '\'
$fixture = Join-Path $Work 'fixture'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'mixed/*') $fixture -Force
Copy-Item (Join-Path $PSScriptRoot 'msbuild/common.props') $fixture -Force
foreach ($name in @('settings.h', 'types.ixx')) {
    (Get-Item (Join-Path $fixture $name)).LastWriteTimeUtc = [DateTime]::UtcNow
}
$arguments = @((Join-Path $fixture 'mixed.vcxproj'), '/nologo', '/v:minimal', '/nodeReuse:false',
    "/p:Configuration=$Configuration", '/p:Platform=x64', "/p:IfcTestWork=$Work",
    "/p:ReflgenExecutable=$HeaderGenerator", "/p:ReflgenIfcExecutable=$Generator",
    "/p:ReflgenTargetsFile=$([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../msbuild/reflgen.targets')))")
function Invoke-Build([string]$Target, [bool]$Success = $true) {
    $log = & $MSBuild @arguments "/t:$Target" 2>&1
    $exitCode = $LASTEXITCODE
    $log | Write-Output
    if (($exitCode -eq 0) -ne $Success) { throw "Unexpected build result $exitCode`n$($log -join "`n")" }
}
Invoke-Build 'Build'
& (Join-Path $Work "$Configuration/bin/mixed.exe")
if ($LASTEXITCODE -ne 0) { throw 'Mixed registration/serialization failed' }
Invoke-Build 'ReflgenGenerate'
$output = Join-Path $Work "$Configuration/obj/mixed/reflgen"
$headerStamp = Join-Path $output 'reflgen_mixed.stamp'
$moduleStamp = Join-Path $output 'reflgen_mixed_types.stamp'
$moduleHeader = Join-Path $output 'reflgen_mixed_types.h'
$headerTime = (Get-Item $headerStamp).LastWriteTimeUtc
$moduleTime = (Get-Item $moduleStamp).LastWriteTimeUtc
Invoke-Build 'ReflgenGenerate'
if ((Get-Item $headerStamp).LastWriteTimeUtc -ne $headerTime -or
    (Get-Item $moduleStamp).LastWriteTimeUtc -ne $moduleTime) { throw 'No-op generation rewrote a stamp' }
$header = Join-Path $fixture 'settings.h'
[IO.File]::WriteAllText($header, (Get-Content $header -Raw).Replace('int version = 3;', 'int version = 3; int header_saved = 9;'))
Invoke-Build 'ReflgenGenerate'
if ((Get-Content (Join-Path $output 'settings.reflgen.h') -Raw) -notmatch 'header_saved') { throw 'Header save did not regenerate' }
if ((Get-Item $moduleStamp).LastWriteTimeUtc -ne $moduleTime) { throw 'Header save reran the IFC generator' }
$headerTime = (Get-Item $headerStamp).LastWriteTimeUtc
$module = Join-Path $fixture 'types.ixx'
[IO.File]::WriteAllText($module, (Get-Content $module -Raw).Replace('int value = 7;', 'int value = 7; int module_saved = 11;'))
Invoke-Build 'ReflgenGenerate'
if ((Get-Content $moduleHeader -Raw) -notmatch 'T::module_saved') { throw 'Module save did not regenerate' }
if ((Get-Item $headerStamp).LastWriteTimeUtc -ne $headerTime) { throw 'Module save reran the header generator' }
$validHash = (Get-FileHash $moduleHeader).Hash
[IO.File]::WriteAllText($module, (Get-Content $module -Raw).Replace('int value = 7;', 'int value = 7; unsigned bad_bits : 3;'))
Invoke-Build 'ReflgenGenerate' $false
if ((Get-FileHash $moduleHeader).Hash -ne $validHash) { throw 'Failed IFC generation damaged the previous schema' }
[IO.File]::WriteAllText($module, (Get-Content $module -Raw).Replace(' unsigned bad_bits : 3;', ''))
Invoke-Build 'Build'
& (Join-Path $Work "$Configuration/bin/mixed.exe")
if ($LASTEXITCODE -ne 0) { throw 'Mixed rebuild failed' }
Write-Output 'MIXED_ROUTES_OK header -> libclang; module -> IFC; no-op stable; failed schema preserved'

param(
    [Parameter(Mandatory)][string]$Generator,
    [Parameter(Mandatory)][string]$MSBuild,
    [Parameter(Mandatory)][string]$Work,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Work = [IO.Path]::GetFullPath($Work).TrimEnd('\', '/') + '\'
$fixture = Join-Path $Work 'fixture'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'engine/*') $fixture -Recurse -Force
Copy-Item (Join-Path $PSScriptRoot 'msbuild/common.props') (Join-Path $fixture 'msbuild') -Force
foreach ($name in @('types.ixx', 'serialization.ixx', 'serializers.h')) {
    (Get-Item (Join-Path $fixture $name)).LastWriteTimeUtc = [DateTime]::UtcNow
}
$arguments = @((Join-Path $fixture 'msbuild/reflection.vcxproj'), '/nologo', '/v:minimal', '/nodeReuse:false',
    "/p:Configuration=$Configuration", '/p:Platform=x64', "/p:IfcTestWork=$Work",
    "/p:IfcTestInclude=$root/include", "/p:ReflgenIfcExecutable=$Generator",
    "/p:IfcMetadataTargets=$root/msbuild/reflgen.ifc.metadata.targets", "/p:IfcTargets=$root/msbuild/reflgen.ifc.targets")
function Invoke-Build {
    $log = & $MSBuild @arguments /t:Build 2>&1
    $result = $LASTEXITCODE
    $log | Write-Output
    if ($result -ne 0) { throw "Engine IFC project failed: $result" }
    & (Join-Path $Work "$Configuration/bin/reflection.exe")
    if ($LASTEXITCODE -ne 0) { throw 'Engine IFC registration or runtime round-trip failed' }
}
Invoke-Build
$stamp = Join-Path $Work "$Configuration/obj/reflection/reflgen-ifc/reflgen_engine.stamp"
$before = (Get-Item $stamp).LastWriteTimeUtc
Invoke-Build
if ((Get-Item $stamp).LastWriteTimeUtc -ne $before) { throw 'No-op engine reflection rewrote its stamp' }
# A serializer-only change must be a generator input and a generated-source compiler dependency.
Add-Content -LiteralPath (Join-Path $fixture 'serializers.h') -Value '// serializer input changed'
Invoke-Build
if ((Get-Item $stamp).LastWriteTimeUtc -le $before) { throw 'Registration header change was not an IFC generation input' }
Write-Output "IFC_ENGINE_MSBUILD_OK $Configuration : runtime, no-op, registration header invalidation"

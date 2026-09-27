# NuGet 패키지 소비 시험 — build\package 의 nupkg 를 로컬 원본으로 두고, reflgen 설정이 하나도 없는 프로젝트
# (tests\package\consumer)가 packages.config 복원만으로 생성·빌드·실행되는지 본다.
#
#   ./tests/package/check_nuget_consumer.ps1 [-Package <폴더>]   # 기본: build\package
param([string]$Package)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Package) { $Package = Join-Path $root 'build\package' }
$nupkg = Get-ChildItem $Package -Filter 'reflgen.*.nupkg' | Select-Object -First 1
if (-not $nupkg) { throw "no reflgen nupkg in $Package — run scripts\package.ps1 first" }
$version = $nupkg.BaseName.Substring('reflgen.'.Length)

$work = Join-Path $Package 'consumer'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
Copy-Item (Join-Path $PSScriptRoot 'consumer') $work -Recurse
# 같은 판을 다시 묶어도 전역 캐시(%USERPROFILE%\.nuget\packages)의 옛 패키지를 쓰지 않게 캐시를 격리한다.
$env:NUGET_PACKAGES = Join-Path $work 'nuget-cache'
Set-Content (Join-Path $work 'packages.config') -Encoding utf8 -Value @"
<?xml version="1.0" encoding="utf-8"?>
<packages>
  <package id="reflgen" version="$version" targetFramework="native" />
</packages>
"@
Set-Content (Join-Path $work 'nuget.config') -Encoding utf8 -Value @"
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="local" value="$($nupkg.DirectoryName)" />
  </packageSources>
</configuration>
"@

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
$project = Join-Path $work 'Consumer.vcxproj'
$common = @("/p:SolutionDir=$work\", "/p:ReflgenPackageVersion=$version", '/p:Configuration=Release', '/p:Platform=x64', '/nologo', '/v:minimal')

& $msbuild $project -t:restore -p:RestorePackagesConfig=true @common
if ($LASTEXITCODE -ne 0) { throw 'restore failed' }
& $msbuild $project -t:build @common
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
& (Join-Path $work 'bin\Consumer.exe')
if ($LASTEXITCODE -ne 0) { throw 'the consumer failed' }

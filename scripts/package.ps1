# 배포물을 만든다 — Release 생성기와 헤더를 한 번 cmake 설치 배치로 모으고, 그것에서 zip 과 NuGet 패키지를 낸다.
# (vcpkg port 는 ports\reflgen 이 소스에서 빌드한다 — 같은 설치 배치를 vcpkg 식으로 옮겨 담는다.)
#
#   ./scripts/package.ps1                    # build\package\ 에 reflgen-<판>-windows-x64.zip, reflgen.<판>.nupkg
#   ./scripts/package.ps1 -Output <폴더>
#
# VS 개발자 환경(cl, Ninja, CMake)이 필요하다. 없으면 vswhere 로 찾아 들어간다. dotnet SDK 가 NuGet 패키지를 묶는다.
param(
    [string]$Output
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Output) { $Output = Join-Path $root 'build\package' }

if (-not (Get-Command cl -ErrorAction SilentlyContinue) -or -not $env:INCLUDE) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

Push-Location $root
try {
    cmake --preset release | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
    cmake --build --preset release
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }

    $stage = Join-Path $Output 'stage'
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    cmake --install (Join-Path $root 'build\release') --prefix $stage | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'install failed' }
    Copy-Item (Join-Path $root 'LICENSE') $stage

    # 배포물이 기대는 배치 — 하나라도 빠지면 소비자 쪽에서 조용히 깨진다.
    $required = @(
        'bin\reflgen.exe', 'bin\libclang.dll', 'include\reflgen\reflgen.h', 'include\reflgen\core\version.h',
        'share\reflgen\msbuild\reflgen.targets', 'share\reflgen\licenses\libclang-LICENSE.TXT',
        'lib\cmake\reflgen\reflgen-config.cmake', 'LICENSE')
    foreach ($path in $required) {
        if (-not (Test-Path (Join-Path $stage $path))) { throw "the install layout lacks $path" }
    }

    $banner = & (Join-Path $stage 'bin\reflgen.exe') --version
    if ($banner -notmatch '^reflgen (\d+\.\d+\.\d+) ') { throw "unexpected reflgen --version output: $banner" }
    $version = $Matches[1]

    $zip = Join-Path $Output "reflgen-$version-windows-x64.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

    # 복원·포장 부산물은 소스 폴더가 아니라 출력 폴더 아래에 둔다.
    $work = Join-Path $Output 'nuget-work'
    $pack = dotnet pack (Join-Path $root 'msbuild\nuget\pack.csproj') --nologo -o $Output `
        "-p:ReflgenVersion=$version" "-p:ReflgenStage=$stage" `
        "-p:BaseIntermediateOutputPath=$work\obj\" "-p:BaseOutputPath=$work\bin\" 2>&1
    if ($LASTEXITCODE -ne 0) { $pack | Write-Host; throw 'dotnet pack failed' }

    "reflgen $version"
    Get-ChildItem $Output -File | Where-Object { $_.Name -like "reflgen*$version*" } |
        ForEach-Object { '  {0}  ({1:0.0} MB)' -f $_.Name, ($_.Length / 1MB) }
}
finally {
    Pop-Location
}

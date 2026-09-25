# 컴파일러 × 표준 행렬로 구성·빌드·시험한다.
#
#   ./scripts/test.ps1                      # 네 조합 전부
#   ./scripts/test.ps1 -Presets msvc-cpp20  # 하나만
#
# VS 개발자 환경(cl, clang-cl, Ninja, CMake)이 필요하다. 없으면 vswhere 로 찾아 들어간다.
param(
    [string[]]$Presets = @('msvc-cpp20', 'msvc-cpp23', 'clang-cl-cpp20', 'clang-cl-cpp23')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command cl -ErrorAction SilentlyContinue) -or -not $env:INCLUDE) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# 어느 컴파일러로 시험했는지 남긴다 — 로컬과 CI 의 결과를 견줄 때 필요하다.
$cl = (& cl 2>&1 | Select-String -Pattern '\d+\.\d+\.\d+' | Select-Object -First 1)
$clang = (& clang-cl --version 2>&1 | Select-Object -First 1)
Write-Host "cl: $cl"
Write-Host "clang-cl: $clang ($((Get-Command clang-cl -ErrorAction SilentlyContinue).Source))"

$failed = @()
foreach ($preset in $Presets) {
    Write-Host "=== $preset" -ForegroundColor Cyan
    Push-Location $root
    try {
        cmake --preset $preset | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "configure failed" }
        cmake --build --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "build failed" }
        ctest --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "tests failed" }
    }
    catch {
        Write-Host "$preset : $_" -ForegroundColor Red
        $failed += $preset
    }
    finally {
        Pop-Location
    }
}

if ($failed.Count -gt 0) {
    Write-Host "FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "all presets passed" -ForegroundColor Green

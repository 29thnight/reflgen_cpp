# vcpkg overlay port 를 만든다 — 소비 프로젝트의 overlay-ports 폴더에 reflgen\ 을 쓴다.
#
#   ./scripts/make-overlay-port.ps1 -Destination <overlay-ports 폴더>                # 릴리스: GitHub 의 v<판> 태그
#   ./scripts/make-overlay-port.ps1 -Destination <overlay-ports 폴더> -Ref <커밋>    # GitHub 에 push 한 커밋
#   ./scripts/make-overlay-port.ps1 -Destination <overlay-ports 폴더> -Local         # 개발: 이 저장소의 HEAD 커밋
#
# GitHub 에서 받는 port(기본, -Ref)는 원본 묶음(archive/<ref>.tar.gz)을 받아 SHA512 를 적는다 — 어느 기계에서나 같은
# 원본을 받는다. 태그나 커밋이 GitHub 에 없으면 실패한다.
# -Local 은 push 없이 로컬 커밋을 소비 프로젝트에서 시험하려는 것이다. 이 기계에서만 된다. REF 가 커밋이라 커밋이
# 바뀌면 vcpkg 가 다시 빌드한다(REF 가 port 의 ABI 해시에 들어간다). 커밋하지 않은 변경은 들어가지 않는다.
param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$Ref,
    [switch]$Local
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'ports\reflgen'
$target = Join-Path $Destination 'reflgen'
$repository = '29thnight/reflgen_cpp'

if ($Local -and $Ref) {
    throw '-Local and -Ref cannot be combined'
}

function Set-SourceBlock([string]$portfile, [string]$block) {
    [regex]::Replace($portfile, '(?s)# REFLGEN_SOURCE_BEGIN.*?# REFLGEN_SOURCE_END', $block.Replace("`r`n", "`n"))
}

$portfile = [IO.File]::ReadAllText((Join-Path $source 'portfile.cmake'))

if ($Local) {
    $commit = (& git -C $root rev-parse HEAD).Trim()
    if (& git -C $root status --porcelain) {
        Write-Warning "uncommitted changes in $root are not part of commit $commit"
    }
    $url = $root.Replace('\', '/')
    $portfile = Set-SourceBlock $portfile @"
# REFLGEN_SOURCE_BEGIN (make-overlay-port.ps1 -Local)
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "$url"
    REF $commit
)
# REFLGEN_SOURCE_END
"@
    "overlay port: local commit $commit"
}
else {
    if (-not $Ref) {
        $version = (Get-Content -Raw (Join-Path $source 'vcpkg.json') | ConvertFrom-Json).version
        $Ref = "v$version"
    }
    # vcpkg_from_github 가 받는 것과 같은 묶음이다.
    $archive = Join-Path ([IO.Path]::GetTempPath()) "reflgen-$([guid]::NewGuid()).tar.gz"
    try {
        Invoke-WebRequest -Uri "https://github.com/$repository/archive/$Ref.tar.gz" -OutFile $archive -UseBasicParsing
        $sha512 = (Get-FileHash -Algorithm SHA512 $archive).Hash.ToLowerInvariant()
    }
    finally {
        Remove-Item -Force $archive -ErrorAction SilentlyContinue
    }
    $portfile = Set-SourceBlock $portfile @"
# REFLGEN_SOURCE_BEGIN (make-overlay-port.ps1 -Ref $Ref)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO $repository
    REF $Ref
    SHA512 $sha512
    HEAD_REF main
)
# REFLGEN_SOURCE_END
"@
    "overlay port: GitHub $Ref"
}

New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item (Join-Path $source 'vcpkg.json') $target -Force
[IO.File]::WriteAllText((Join-Path $target 'portfile.cmake'), $portfile, (New-Object Text.UTF8Encoding($false)))
"written to $target"

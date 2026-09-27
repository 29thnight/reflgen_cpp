# vcpkg overlay port 를 만든다 — 소비 프로젝트의 overlay-ports 폴더에 reflgen\ 을 쓴다.
#
#   ./scripts/make-overlay-port.ps1 -Destination <overlay-ports 폴더>          # 릴리스: GitHub 의 v<판> 태그
#   ./scripts/make-overlay-port.ps1 -Destination <overlay-ports 폴더> -Local   # 개발: 이 저장소의 HEAD 커밋
#
# -Local 은 push 없이 로컬 커밋을 소비 프로젝트에서 시험하려는 것이다. REF 가 커밋이라 커밋이 바뀌면 vcpkg 가
# 다시 빌드한다(REF 가 port 의 ABI 해시에 들어간다). 커밋하지 않은 변경은 들어가지 않는다.
param(
    [Parameter(Mandatory)][string]$Destination,
    [switch]$Local
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'ports\reflgen'
$target = Join-Path $Destination 'reflgen'

New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item (Join-Path $source 'vcpkg.json') $target -Force
$portfile = [IO.File]::ReadAllText((Join-Path $source 'portfile.cmake'))

if ($Local) {
    $commit = (& git -C $root rev-parse HEAD).Trim()
    if (& git -C $root status --porcelain) {
        Write-Warning "uncommitted changes in $root are not part of commit $commit"
    }
    $url = $root.Replace('\', '/')
    $replacement = @"
# REFLGEN_SOURCE_BEGIN (make-overlay-port.ps1 -Local)
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "$url"
    REF $commit
)
# REFLGEN_SOURCE_END
"@
    $portfile = [regex]::Replace($portfile, '(?s)# REFLGEN_SOURCE_BEGIN.*?# REFLGEN_SOURCE_END', $replacement.Replace("`r`n", "`n"))
    "overlay port: local commit $commit"
}
else {
    "overlay port: GitHub tag"
}
[IO.File]::WriteAllText((Join-Path $target 'portfile.cmake'), $portfile, (New-Object Text.UTF8Encoding($false)))
"written to $target"

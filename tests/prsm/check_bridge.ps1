param(
    [Parameter(Mandatory)][string]$HeaderGenerator,
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$Work,
    [string]$IfcGenerator,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Work = [System.IO.Path]::GetFullPath($Work)
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$include = Join-Path $root 'include'
$fixture = $PSScriptRoot
$adapter = (Join-Path $fixture 'adapter.h').Replace('\', '/')
$runtime = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
$common = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/W4', '/WX', '/permissive-', '/wd5030', $runtime, "/I$include", "/I$fixture")
if ([System.IO.Path]::GetFileName($Compiler) -match '^clang-cl') { $common += '/clang:-Wno-unknown-attributes' }
function Run([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed: $LASTEXITCODE" }
}
function Check-Surface([string]$directory, [string]$provider, [string[]]$extra) {
    $table = Get-Content (Join-Path $directory 'reflgen_demo_prsm.abi.tsv') | Where-Object { -not $_.StartsWith('#') }
    if (@($table).Count -ne 6 -or ($table -join "`n").Contains('Hidden')) { throw 'wrong selected binding surface' }
    $runner = [System.IO.File]::ReadAllText((Join-Path $fixture 'runner.cpp.in')).Replace('@PROVIDER@', $provider)
    foreach ($name in @('Get', 'Set', 'Add', 'Enabled', 'Scale')) {
        $hex = [System.Convert]::ToHexString([System.Text.Encoding]::UTF8.GetBytes($name)).ToLowerInvariant()
        $row = @($table | ForEach-Object { ,$_.Split("`t") } | Where-Object { $_[0].EndsWith('_' + $hex) })
        if ($row.Count -ne 1) { throw "missing/duplicate method $name" }
        $runner = $runner.Replace('@' + $name.ToUpperInvariant() + '@', $row[0][0])
    }
    $runnerPath = Join-Path $directory 'runner.cpp'
    [System.IO.File]::WriteAllText($runnerPath, $runner, [System.Text.UTF8Encoding]::new($false))
    Run $Compiler ($common + @("/I$directory", '/c', (Join-Path $directory 'reflgen_demo_prsm.cpp'), "/Fo$directory/bridge.obj") + $extra)
    Run $Compiler ($common + @("/I$directory", '/c', $runnerPath, "/Fo$directory/runner.obj") + $extra)
    $objects = @((Join-Path $directory 'bridge.obj'), (Join-Path $directory 'runner.obj'))
    if ($extra.Count -gt 0) { $objects += Join-Path $Work 'counter.obj' }
    Run $Compiler (@('/nologo') + $objects + @("/Fe$directory/runner.exe", '/link', "/IMPLIB:$directory/runner.lib"))
    Run (Join-Path $directory 'runner.exe') @()
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if ($dumpbin) {
        $exports = (& $dumpbin.Source /nologo /exports (Join-Path $directory 'runner.exe')) -join "`n"
        if ($LASTEXITCODE -ne 0) { throw 'cannot inspect generated native exports' }
        foreach ($line in $table) {
            if (-not $exports.Contains($line.Split("`t")[0])) { throw 'generated C ABI symbol is not exported' }
        }
    }
    # Generated declarations must compile as plain C as well.
    $cPath = Join-Path $directory 'consumer.c'
    [System.IO.File]::WriteAllText($cPath, "#include `"reflgen_demo_prsm.h`"`n", [System.Text.UTF8Encoding]::new($false))
    Run $Compiler @('/nologo', '/W4', '/WX', "/I$include", "/I$directory", '/c', $cPath, "/Fo$directory/consumer.obj")
}
$headerDir = Join-Path $Work 'header'
Run $HeaderGenerator @('--module', 'demo', '--output', $headerDir, '--prsm', '--prsm-adapter-header', $adapter,
                       (Join-Path $fixture 'counter.h'), '--', '-std=c++20', '--target=x86_64-pc-windows-msvc')
Check-Surface $headerDir '#include "counter.h"' @()
# Entity/component receiver kinds: one generic adapter per kind, entity handles only.
$sceneDir = Join-Path $Work 'scene'
$sceneAdapter = (Join-Path $fixture 'scene_adapter.h').Replace([string][char]92, '/')
Run $HeaderGenerator @('--module', 'scene', '--output', $sceneDir, '--prsm', '--prsm-adapter-header', $sceneAdapter,
                       (Join-Path $fixture 'scene.h'), '--', '-std=c++20', '--target=x86_64-pc-windows-msvc')
$sceneTable = @(Get-Content (Join-Path $sceneDir 'reflgen_scene_prsm.abi.tsv') | Where-Object { -not $_.StartsWith('#') })
if ($sceneTable.Count -ne 6 -or ($sceneTable -join "`n").Contains('Hidden')) { throw 'wrong entity/component binding surface' }
if (-not ($sceneTable -join "`n").Contains('34:entity_generation_component_lookup;') -or
    -not ($sceneTable -join "`n").Contains('17:entity_generation;')) { throw 'receiver kind missing from ABI descriptors' }
$sceneCatalog = Get-Content -Raw (Join-Path $sceneDir 'reflgen_scene_prsm.bindings.json') | ConvertFrom-Json
$kinds = @($sceneCatalog.types | ForEach-Object { $_.name + '=' + $_.receiver }) -join ','
if ($kinds -ne 'scene::Entity=entity,scene::LightComponent=component') { throw "unexpected catalog receiver kinds: $kinds" }
$sceneBridge = [System.IO.File]::ReadAllText((Join-Path $sceneDir 'reflgen_scene_prsm.cpp'))
if (-not $sceneBridge.Contains('component_access<::scene::LightComponent>') -or $sceneBridge.Contains('binding_access<')) {
    throw 'component receivers must use the generic component adapter'
}
function SceneSymbol([string]$type, [string]$member) {
    $hex = { param($t) [System.Convert]::ToHexString([System.Text.Encoding]::UTF8.GetBytes($t)).ToLowerInvariant() }
    'ce_prsm_bind_' + (& $hex 'scene') + '_' + (& $hex $type) + '_' + (& $hex $member)
}
$sceneRunner = [System.IO.File]::ReadAllText((Join-Path $fixture 'scene_runner.cpp.in'))
$sceneSymbols = [ordered]@{
    '@LIGHT_OWNER@' = (SceneSymbol 'scene::LightComponent' 'x') -replace '_78$', '_owner'
    '@ENTITY_OWNER@' = (SceneSymbol 'scene::Entity' 'x') -replace '_78$', '_owner'
    '@LIGHT_GET@' = SceneSymbol 'scene::LightComponent' 'GetIntensity'
    '@LIGHT_SET@' = SceneSymbol 'scene::LightComponent' 'SetIntensity'
    '@ENTITY_GET@' = SceneSymbol 'scene::Entity' 'IsEnabled'
    '@ENTITY_SET@' = SceneSymbol 'scene::Entity' 'SetEnabled'
}
foreach ($key in $sceneSymbols.Keys) { $sceneRunner = $sceneRunner.Replace($key, $sceneSymbols[$key]) }
[System.IO.File]::WriteAllText((Join-Path $sceneDir 'scene_runner.cpp'), $sceneRunner, [System.Text.UTF8Encoding]::new($false))
Run $Compiler ($common + @("/I$sceneDir", '/c', (Join-Path $sceneDir 'reflgen_scene_prsm.cpp'), "/Fo$sceneDir/bridge.obj"))
Run $Compiler ($common + @("/I$sceneDir", '/c', (Join-Path $sceneDir 'scene_runner.cpp'), "/Fo$sceneDir/runner.obj"))
Run $Compiler @('/nologo', "$sceneDir/bridge.obj", "$sceneDir/runner.obj", "/Fe$sceneDir/runner.exe", '/link', "/IMPLIB:$sceneDir/runner.lib")
Run (Join-Path $sceneDir 'runner.exe') @()
$unknownKind = Join-Path $sceneDir 'unknown_kind.h'
[System.IO.File]::WriteAllText($unknownKind, "class [[reflgen::interop(`"prsm`"), reflgen::lifetime(`"borrowed`"), reflgen::receiver(`"actor`")]] Bad { public: [[reflgen::interop(`"prsm`")]] int Get() const; };`n", [System.Text.UTF8Encoding]::new($false))
$rejected = (& $HeaderGenerator --module bad --output (Join-Path $sceneDir 'bad') --prsm --prsm-adapter-header $sceneAdapter $unknownKind -- -std=c++20 --target=x86_64-pc-windows-msvc 2>&1) -join "`n"
if ($LASTEXITCODE -eq 0 -or $rejected -notmatch 'unknown PrSM receiver kind' -or (Test-Path (Join-Path $sceneDir 'bad/reflgen_bad_prsm.cpp'))) {
    throw 'unknown receiver kind was not rejected before output'
}
# Identical generation preserves both bytes and timestamps.
$stable = Join-Path $headerDir 'reflgen_demo_prsm.cpp'
$timestamp = [System.IO.File]::GetLastWriteTimeUtc($stable)
Run $HeaderGenerator @('--module', 'demo', '--output', $headerDir, '--prsm', '--prsm-adapter-header', $adapter,
                       (Join-Path $fixture 'counter.h'), '--', '-std=c++20', '--target=x86_64-pc-windows-msvc')
if ([System.IO.File]::GetLastWriteTimeUtc($stable) -ne $timestamp) { throw 'unchanged bridge was rewritten' }
$protectedHeader = Join-Path $headerDir 'reflgen_demo_prsm.h'
$protectedDigest = (Get-FileHash $protectedHeader).Hash
$collision = (& $HeaderGenerator --module demo --output $headerDir --prsm --prsm-adapter-header $protectedHeader (Join-Path $fixture 'counter.h') -- -std=c++20 --target=x86_64-pc-windows-msvc 2>&1) -join "`n"
if ($LASTEXITCODE -eq 0 -or $collision -notmatch 'collides' -or (Get-FileHash $protectedHeader).Hash -ne $protectedDigest) {
    throw 'header generator did not preserve a colliding adapter input'
}
if ($IfcGenerator) {
    $ifc = Join-Path $Work 'counter.ifc'
    $metadata = Join-Path $Work 'counter.i'
    Run $Compiler ($common + @('/interface', '/c', (Join-Path $fixture 'counter.ixx'), '/ifcOutput', $ifc, "/Fo$Work/counter.obj"))
    Run $Compiler ($common + @('/P', (Join-Path $fixture 'counter.ixx'), "/Fi$metadata"))
    $moduleDir = Join-Path $Work 'module'
    $relativeAdapter = [System.IO.Path]::GetRelativePath((Get-Location).Path, $adapter)
    Run $IfcGenerator @('--ifc', $ifc, '--import', 'demo.counter', '--module', 'demo', '--output', $moduleDir,
                         '--metadata', $metadata, '--prsm', '--prsm-adapter-header', $relativeAdapter)
    Check-Surface $moduleDir 'import demo.counter;' @('/reference', "demo.counter=$ifc")
    $headerAbi = [System.IO.File]::ReadAllText((Join-Path $headerDir 'reflgen_demo_prsm.abi.tsv'))
    $moduleAbi = [System.IO.File]::ReadAllText((Join-Path $moduleDir 'reflgen_demo_prsm.abi.tsv'))
    if ($headerAbi -cne $moduleAbi) { throw 'actual header/IFC Binding IR signatures differ' }
    $headerCatalog = [System.IO.File]::ReadAllText((Join-Path $headerDir 'reflgen_demo_prsm.bindings.json'))
    $moduleCatalog = [System.IO.File]::ReadAllText((Join-Path $moduleDir 'reflgen_demo_prsm.bindings.json'))
    if ($headerCatalog -cne $moduleCatalog) { throw 'actual header/IFC production catalogs differ' }
    $protectedHeader = Join-Path $moduleDir 'reflgen_demo_prsm.h'
    $protectedDigest = (Get-FileHash $protectedHeader).Hash
    $collision = (& $IfcGenerator --ifc $ifc --import demo.counter --module demo --output $moduleDir --metadata $metadata --prsm --prsm-adapter-header $protectedHeader 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0 -or $collision -notmatch 'collides' -or (Get-FileHash $protectedHeader).Hash -ne $protectedDigest) {
        throw 'IFC generator did not preserve a colliding adapter input'
    }
}

# A stale wrapper must not compile against a changed native declaration.
$driftDir = Join-Path $Work 'signature-drift'
New-Item -ItemType Directory -Force -Path $driftDir | Out-Null
$driftHeader = Join-Path $driftDir 'counter.h'
$originalHeader = [System.IO.File]::ReadAllText((Join-Path $fixture 'counter.h'))
[System.IO.File]::WriteAllText($driftHeader, $originalHeader, [System.Text.UTF8Encoding]::new($false))
Run $HeaderGenerator @('--module', 'demo', '--output', $driftDir, '--prsm', '--prsm-adapter-header', $adapter,
                      $driftHeader, '--', '-std=c++20', '--target=x86_64-pc-windows-msvc')
[System.IO.File]::WriteAllText($driftHeader, $originalHeader.Replace('using count_type = int;', 'using count_type = long long;'), [System.Text.UTF8Encoding]::new($false))
$nativeMismatch = (& $Compiler @common "/I$driftDir" /c (Join-Path $driftDir 'reflgen_demo_prsm.cpp') "/Fo$driftDir/drift.obj" 2>&1) -join "`n"
if ($LASTEXITCODE -eq 0 -or $nativeMismatch -notmatch 'C2338|C2607|static assertion') {
    throw "native signature drift did not fail its compiled assertions: $nativeMismatch"
}

$cases = @(
    @{Name='pointer'; Body='[[reflgen::interop("prsm")]] int* Get() { return nullptr; }'},
    @{Name='private'; Body='private: [[reflgen::interop("prsm")]] int Get() { return 1; }'},
    @{Name='static'; Body='[[reflgen::interop("prsm")]] static int Get() { return 1; }'},
    @{Name='volatile'; Body='[[reflgen::interop("prsm")]] int Get() volatile { return 1; }'},
    @{Name='refqualified'; Body='[[reflgen::interop("prsm")]] int Get() & { return 1; }'},
    @{Name='consteval'; Body='[[reflgen::interop("prsm")]] consteval int Get() { return 1; }'},
    @{Name='overload'; Body="[[reflgen::interop(`"prsm`")]] int Get() { return 1; }`nint Get(int x) { return x; }"},
    @{Name='field'; Body='[[reflgen::interop("prsm")]] int value = 0;'},
    @{Name='badproperty'; Body='[[reflgen::interop("prsm"), reflgen::property_get("Value")]] void Get() {}'},
    @{Name='unselected'; Type=''; Body='[[reflgen::interop("prsm")]] int Get() { return 1; }'},
    @{Name='lifetime'; Type='[[reflgen::interop("prsm")]]'; Body='[[reflgen::interop("prsm")]] int Get() { return 1; }'},
    @{Name='effect'; Body='[[reflgen::interop("prsm"), reflgen::effect("pure")]] int Get() { return 1; }'},
    @{Name='gettereffect'; Body='[[reflgen::interop("prsm"), reflgen::property_get("Value"), reflgen::effect("write")]] int Get() { return 1; }'},
    @{Name='template'; Body='template<class T> [[reflgen::interop("prsm")]] int Get(T) { return 1; }'}
)
foreach ($case in $cases) {
    $caseDir = Join-Path $Work ('reject-' + $case.Name)
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    $attrs = if ($case.ContainsKey('Type')) { $case.Type } else { '[[reflgen::interop("prsm"), reflgen::lifetime("borrowed")]]' }
    $body = "namespace rejected {`nclass $attrs Counter {`npublic:`n$($case.Body)`n};`n}`n"
    $header = Join-Path $caseDir 'bad.h'
    [System.IO.File]::WriteAllText($header, $body, [System.Text.UTF8Encoding]::new($false))
    $headerOutput = Join-Path $caseDir 'header'
    $diagnostic = (& $HeaderGenerator --module bad --output $headerOutput --prsm --prsm-adapter-header $adapter $header -- -std=c++20 --target=x86_64-pc-windows-msvc 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0 -or $diagnostic -notmatch 'RG0[23][0-9][0-9]' -or (Test-Path (Join-Path $headerOutput 'reflgen_bad_prsm.cpp'))) {
        throw "header $($case.Name) did not reject safely: $diagnostic"
    }
    if ($IfcGenerator) {
        $source = Join-Path $caseDir 'bad.ixx'
        $ifc = Join-Path $caseDir 'bad.ifc'
        $metadata = Join-Path $caseDir 'bad.i'
        [System.IO.File]::WriteAllText($source, "export module rejected;`nexport {`n$body}`n", [System.Text.UTF8Encoding]::new($false))
        Run $Compiler ($common + @('/interface', '/c', $source, '/ifcOutput', $ifc, "/Fo$caseDir/bad.obj"))
        Run $Compiler ($common + @('/P', $source, "/Fi$metadata"))
        $moduleOutput = Join-Path $caseDir 'module'
        $diagnostic = (& $IfcGenerator --ifc $ifc --import rejected --module bad --output $moduleOutput --metadata $metadata --prsm --prsm-adapter-header $adapter 2>&1) -join "`n"
        if ($LASTEXITCODE -eq 0 -or $diagnostic -notmatch 'RG0[23][0-9][0-9]' -or (Test-Path (Join-Path $moduleOutput 'reflgen_bad_prsm.cpp'))) {
            throw "IFC $($case.Name) did not reject safely: $diagnostic"
        }
    }
}
Write-Host 'PrSM generated bridge runtime and ABI equivalence passed'

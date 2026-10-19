param(
    [Parameter(Mandatory)][string]$HeaderGenerator,
    [Parameter(Mandatory)][string]$PrsmCompiler,
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$Work,
    [string]$IfcGenerator,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Run([string]$program, [string[]]$arguments) {
    & $program @arguments
    if ($LASTEXITCODE -ne 0) { throw "$program failed: $LASTEXITCODE" }
}
& "$PSScriptRoot/check_bridge.ps1" -HeaderGenerator $HeaderGenerator -Compiler $Compiler -Work $Work -IfcGenerator $IfcGenerator -Configuration $Configuration
$runtime = if ($Configuration -eq 'Debug') { '/MDd' } else { '/MD' }
$flags = @('/nologo','/std:c++20','/EHsc','/utf-8','/W4','/WX','/wd5030', $runtime, "/I$root/include", "/I$PSScriptRoot")
if ([IO.Path]::GetFileName($Compiler) -match '^clang-cl') { $flags += '/clang:-Wno-unknown-attributes' }
$providers = @('header')
if ($IfcGenerator) { $providers += 'module' }
foreach ($provider in $providers) {
    $directory = Join-Path $Work $provider
    $catalogPath = Join-Path $directory 'reflgen_demo_prsm.bindings.json'
    $catalog = Get-Content -Raw $catalogPath | ConvertFrom-Json
    if ($catalog.callables.Count -ne 6) { throw 'missing production catalog calls' }
    $extra = @()
    $providerLine = '#include "counter.h"'
    if ($provider -eq 'module') { $extra = @('/reference', "demo.counter=$Work/counter.ifc"); $providerLine = 'import demo.counter;' }
    $hostSource = [IO.File]::ReadAllText("$PSScriptRoot/compiler_host.cpp.in").Replace('@PROVIDER@', $providerLine)
    [IO.File]::WriteAllText("$directory/compiler_host.cpp", $hostSource, [Text.UTF8Encoding]::new($false))
    Run $Compiler ($flags + @("/I$directory",'/c',"$directory/compiler_host.cpp","/Fo$directory/compiler_host.obj") + $extra)
    Run $PrsmCompiler @('build',"$PSScriptRoot/calls.prsm",'--bindings',$catalogPath,'-o',"$directory/script.obj")
    Run $PrsmCompiler @('emit-ir',"$PSScriptRoot/calls.prsm",'--bindings',$catalogPath,'-o',"$directory/script.ll")
    Run $PrsmCompiler @('build-module',"$PSScriptRoot/component.prsm",'--bindings',$catalogPath,'-o',"$directory/component.obj")
    $ir = [IO.File]::ReadAllText("$directory/script.ll")
    if (-not $ir.Contains('call i32 @ce_prsm_validate_binding') -or $ir.Contains('prsm_mock_validate_binding') -or $ir.Contains('inttoptr')) { throw 'production LLVM call contract not used' }
    $objects = @("$directory/compiler_host.obj","$directory/bridge.obj")
    if ($provider -eq 'module') { $objects += "$Work/counter.obj" }
    Run $Compiler (@('/nologo') + $objects + @("$directory/script.obj","/Fe$directory/script.exe",'/link',"/IMPLIB:$directory/script.lib"))
    foreach ($mode in @('0','1','2','3','5')) { Run "$directory/script.exe" @($mode) }
    $projectDirectory = Join-Path $directory 'project'
    New-Item -ItemType Directory -Force -Path $projectDirectory | Out-Null
    [IO.File]::WriteAllText("$projectDirectory/main.prsm", "export module Production`n" + [IO.File]::ReadAllText("$PSScriptRoot/calls.prsm"), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText("$projectDirectory/project.json", '{"format":"prsm-project","version":1,"sources":["main.prsm"],"entry_module":"Production"}', [Text.UTF8Encoding]::new($false))
    foreach ($iteration in 1..2) {
        Run $PrsmCompiler @('check','--project',"$projectDirectory/project.json",'--bindings',$catalogPath,'--cache-dir',"$projectDirectory/cache",'--build-stats',"$projectDirectory/stats-$iteration.json")
    }
    $stats = Get-Content -Raw "$projectDirectory/stats-2.json" | ConvertFrom-Json
    if ($stats.reused_units -lt 1) { throw 'production project catalog did not survive validated body-cache reuse' }
    [IO.File]::WriteAllText("$directory/hidden.prsm", "fn main():int { val counter=demo::Owner()`nreturn counter.Hidden() }", [Text.UTF8Encoding]::new($false))
    $hidden = (& $PrsmCompiler check "$directory/hidden.prsm" --bindings $catalogPath 2>&1) -join "`n"
    if ($LASTEXITCODE -eq 0 -or $hidden -notmatch 'E22') { throw 'unselected native method became script-visible' }

    # Catalog cannot authorize a wider native result, even with a recomputed hash.
    $method = $catalog.callables | Where-Object cpp_name -eq 'Add'
    $method.result = 'uint64'; $method.signature = 'uint64(int32)'
    $fields = @('reflgen.prsm.call.v1','cdecl','int32','0','typed_out_pointer','no_out_pointer',
        'generational_u64','domain_generation_nominal_type','borrowed_call_guard','game_thread','caught_to_status',
        'u8_nonzero_in_normalized_out','untouched','synchronous',$catalog.module,$catalog.target.triple,'64',
        $method.symbol,$method.operation,$method.receiver,$method.cpp_name,$method.signature,$method.effect,$method.result,'1','int32')
    $utf8 = [Text.UTF8Encoding]::new($false)
    $descriptor = ($fields | ForEach-Object { [string]$utf8.GetByteCount($_) + ':' + $_ + ';' }) -join ''
    $method.abi_sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($utf8.GetBytes($descriptor))).ToLowerInvariant()
    $badPath = "$directory/widened.bindings.json"
    [IO.File]::WriteAllText($badPath, ($catalog | ConvertTo-Json -Depth 32), $utf8)
    Run $PrsmCompiler @('build',"$PSScriptRoot/widened.prsm",'--bindings',$badPath,'-o',"$directory/widened.obj")
    Run $Compiler (@('/nologo') + $objects + @("$directory/widened.obj","/Fe$directory/widened.exe",'/link',"/IMPLIB:$directory/widened.lib"))
    Run "$directory/widened.exe" @('4')

    foreach ($case in @('version','target','digest','unknown')) {
        $invalid = Get-Content -Raw $catalogPath | ConvertFrom-Json
        switch ($case) {
            version { $invalid.version = 99 }
            target { $invalid.target.pointer_width = 32 }
            digest { $invalid.callables[0].abi_sha256 = ('0' * 64) }
            unknown { $invalid | Add-Member ignored_authority 'bad' }
        }
        $path = "$directory/$case.bindings.json"
        [IO.File]::WriteAllText($path, ($invalid | ConvertTo-Json -Depth 32), $utf8)
        $diagnostic = (& $PrsmCompiler check "$PSScriptRoot/calls.prsm" --bindings $path 2>&1) -join "`n"
        if ($LASTEXITCODE -eq 0 -or $diagnostic -notmatch 'E0005') { throw "invalid production $case was accepted: $diagnostic" }
    }
}
Write-Host 'PrSM production catalog -> type checking -> LLVM -> native calls passed'

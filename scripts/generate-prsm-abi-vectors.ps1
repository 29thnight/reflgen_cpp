# Independent .NET SHA-256 oracle for the C++/Rust production contract tests.
# Default is verification; -Write deliberately updates the reviewed fixture.
param([switch]$Write)
$ErrorActionPreference = 'Stop'
$fixture = Join-Path (Split-Path -Parent $PSScriptRoot) 'generator/tests/fixtures/prsm-call-v1.tsv'
$utf8 = [System.Text.UTF8Encoding]::new($false)
$cases = @(
    @('get', 'x86_64-pc-windows-msvc', 'int32', '', 'getter', 'GetCount', 'int () const', 'read'),
    @('set', 'x86_64-pc-windows-msvc', 'void', 'int32', 'setter', 'SetCount', 'void (int)', 'write'),
    @('add', 'x86_64-pc-linux-gnu', 'int32', 'int32', 'method', 'Add', 'int (int)', 'write'),
    @('bool', 'x86_64-unknown-linux-gnu', 'bool', 'bool', 'method', 'Enabled', 'bool (bool)', 'write'),
    @('float', 'aarch64-unknown-linux-gnu', 'float32', 'float32,float64', 'method', 'Scale', 'float (float, double)', 'write'),
    @('widths', 'x86_64-pc-windows-msvc', 'uint64', 'int8,int16,int32,int64,uint8,uint16,uint32,uint64', 'method', 'Widths', 'uint64_t (int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t)', 'read'),
    @('utf8', 'x86_64-pc-windows-msvc', 'float64', '', 'getter', 'Wide', '実数 () const', 'read')
)
$lines = @('# case target module symbol operation receiver method cpp_signature effect result parameters descriptor sha256 (TAB-separated; empty parameters means zero)')
foreach ($case in $cases) {
    $caseName, $target, $result, $parameterText, $operation, $method, $signature, $effect = $case
    $parameters = @()
    if ($parameterText.Length -gt 0) { $parameters = @($parameterText.Split(',')) }
    $symbol = 'ce_prsm_bind_actor_' + $caseName
    $fields = @(
        'reflgen.prsm.call.v1', 'cdecl', 'int32', '0', 'typed_out_pointer', 'no_out_pointer',
        'generational_u64', 'domain_generation_nominal_type', 'borrowed_call_guard', 'game_thread',
        'caught_to_status', 'u8_nonzero_in_normalized_out', 'untouched', 'synchronous',
        'engine', $target, '64', $symbol, $operation, 'engine::Actor', $method, $signature,
        $effect, $result, [string]$parameters.Count
    ) + $parameters
    $descriptor = ($fields | ForEach-Object { [string]$utf8.GetByteCount($_) + ':' + $_ + ';' }) -join ''
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $digest = [System.BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($descriptor))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $lines += (@($caseName, $target, 'engine', $symbol, $operation, 'engine::Actor', $method, $signature,
                 $effect, $result, $parameterText, $descriptor, $digest) -join "`t")
}
$expected = ($lines -join "`n") + "`n"
if ($Write) {
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $fixture)) | Out-Null
    [System.IO.File]::WriteAllText($fixture, $expected, $utf8)
} elseif ([System.IO.File]::ReadAllText($fixture, $utf8).Replace("`r`n", "`n") -cne $expected) {
    throw 'PrSM production ABI vectors differ from the independent oracle'
}
Write-Host "PrSM ABI vectors verified: $($cases.Count)"

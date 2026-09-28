param([Parameter(Mandatory)][string]$TestDirectory)
$ErrorActionPreference = 'Stop'
$checker = Join-Path (Split-Path -Parent $PSScriptRoot) 'check-profile.ps1'
$fixture = Join-Path $TestDirectory 'fixture'
New-Item -ItemType Directory -Path $fixture | Out-Null
$statePath = Join-Path $fixture 'Local State'
$keyPath = Join-Path $fixture 'test.key'
$key = [byte[]](1..32)
[IO.File]::WriteAllText($keyPath, [Convert]::ToHexString($key))
$aad = [Text.Encoding]::ASCII.GetBytes("CHRMAES" + [char]1)
$nonce = [byte[]](1..12)
$plain = [byte[]](32..63)
$cipher = [byte[]]::new(32)
$tag = [byte[]]::new(16)
$aes = [Security.Cryptography.AesGcm]::new($key, 16)
$aes.Encrypt($nonce, $plain, $cipher, $tag, $aad)
$aes.Dispose()
$blob = [byte[]]([Text.Encoding]::ASCII.GetBytes('DPAPI') + $aad + $nonce + $tag + $cipher)
function Save-State([byte[]]$Value, [bool]$AppBound = $false) {
    $crypt = @{encrypted_key = [Convert]::ToBase64String($Value)}
    if ($AppBound) { $crypt.app_bound_encrypted_key = 'test-existing-app-bound-key' }
    [IO.File]::WriteAllText($statePath, (@{os_crypt=$crypt} | ConvertTo-Json -Compress))
}
function Expect-Rejection {
    $rejected = $false
    try { $null = & $checker -UserDataDirectory $fixture -PortableKeyFile $keyPath }
    catch { $rejected = $true }
    if (-not $rejected) { throw 'Checker accepted invalid/machine-bound data' }
}
Save-State $blob
$hash = (Get-FileHash -LiteralPath $statePath).Hash
$result = & $checker -UserDataDirectory $fixture -PortableKeyFile $keyPath
if (-not $result.PortableMasterKeyVerified -or $result.AppBoundKeyPresent) {
    throw 'Valid portable key not recognized'
}
if ((Get-FileHash -LiteralPath $statePath).Hash -ne $hash) { throw 'Checker modified profile' }
Save-State $blob $true
$result = & $checker -UserDataDirectory $fixture -PortableKeyFile $keyPath -WarningAction SilentlyContinue
if (-not $result.AppBoundKeyPresent) { throw 'App-Bound key not reported' }
$blob[25] = $blob[25] -bxor 1
Save-State $blob
Expect-Rejection
Save-State ([Text.Encoding]::ASCII.GetBytes('DPAPIwindows-bound-blob'))
Expect-Rejection
Write-Output 'PASS: read-only profile checker accepts authenticated portable keys, reports App-Bound state and rejects corrupt/Windows-bound keys'

[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot '.local\chromic.key'
}

if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
    $existingKey = ([IO.File]::ReadAllText(
        (Resolve-Path -LiteralPath $OutputPath).Path)).Trim()
    if ($existingKey -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'Existing portable key must contain exactly 64 hexadecimal characters.'
    }
    Write-Host "Using existing portable profile key: $OutputPath"
    return
}

$parent = Split-Path -Parent $OutputPath
if ($parent -and -not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}

$keyBytes = New-Object byte[] 32
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $rng.GetBytes($keyBytes)
} finally {
    $rng.Dispose()
}

$key = -join ($keyBytes | ForEach-Object { $_.ToString('X2') })
[IO.File]::WriteAllText($OutputPath, $key, [Text.Encoding]::ASCII)
Write-Host "Created portable profile key: $OutputPath"
Write-Host 'Keep this file private and backed up. Losing it can make encrypted profile data unreadable.'

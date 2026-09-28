#Requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$UserDataDirectory,
    [string]$PortableKeyFile = (Join-Path $PSScriptRoot '.local\chromic.key')
)
$ErrorActionPreference = 'Stop'
$statePath = Join-Path $UserDataDirectory 'Local State'
$state = [IO.File]::ReadAllText($statePath) | ConvertFrom-Json
if (-not $state.os_crypt.encrypted_key) {
    throw 'No encrypted master key in Local State. Portability is not verified.'
}
$wrapped = [Convert]::FromBase64String($state.os_crypt.encrypted_key)
if ($wrapped.Length -ne 73 -or
    [Text.Encoding]::ASCII.GetString($wrapped, 0, 12) -ne 'DPAPICHRMAES' -or
    $wrapped[12] -ne 1) {
    throw 'Master key is not in the supported Chromic portable format. Existing Windows-bound data must not be assumed portable.'
}
$keyText = [IO.File]::ReadAllText($PortableKeyFile).Trim()
if ($keyText -notmatch '^[0-9A-Fa-f]{64}$') {
    throw 'Invalid build-key file (expected 64 hex digits).'
}
$key = [Convert]::FromHexString($keyText)
$plaintext = [byte[]]::new(32)
$aes = $null
try {
    $aes = [Security.Cryptography.AesGcm]::new($key, 16)
    $aes.Decrypt([byte[]]$wrapped[13..24], [byte[]]$wrapped[41..72],
        [byte[]]$wrapped[25..40], $plaintext, [byte[]]$wrapped[5..12])
} catch {
    throw 'Portable master-key authentication failed. Wrong build key or damaged Local State; nothing was changed.'
} finally {
    if ($aes) { $aes.Dispose() }
    [Array]::Clear($key, 0, $key.Length)
    [Array]::Clear($plaintext, 0, $plaintext.Length)
    $keyText = $null
}
$hasAppBound = -not [string]::IsNullOrEmpty([string]$state.os_crypt.app_bound_encrypted_key)
if ($hasAppBound) {
    Write-Warning 'An App-Bound key is also present. This check does not migrate it or certify existing v20 data as portable.'
}
[pscustomobject]@{
    PortableMasterKeyVerified = $true
    AppBoundKeyPresent = $hasAppBound
    ProfileModified = $false
    Scope = 'Master key only. Browser sessions, extensions and another Windows account still need an end-to-end test.'
}

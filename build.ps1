[CmdletBinding()]
param(
    [string]$ToolchainDir = $env:CHROMIC_LLVM_MINGW,
    [string]$PortableKeyFile = $env:CHROMIC_PORTABLE_KEY_FILE
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$toolchainVersion = '20260922'
$toolchainArchive = "llvm-mingw-$toolchainVersion-ucrt-x86_64.zip"
$toolchainSha256 = 'E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666'
$root = $PSScriptRoot

if (-not $PortableKeyFile) {
    $PortableKeyFile = Join-Path $root '.local\chromic.key'
}
if (-not (Test-Path -LiteralPath $PortableKeyFile -PathType Leaf)) {
    & (Join-Path $root 'generate-key.ps1') -OutputPath $PortableKeyFile
}
$portableKey = ([IO.File]::ReadAllText(
    (Resolve-Path -LiteralPath $PortableKeyFile).Path)).Trim()
if ($portableKey -notmatch '^[0-9A-Fa-f]{64}$') {
    throw 'Portable key must contain exactly 64 hexadecimal characters.'
}
Write-Host "Using portable profile key: $PortableKeyFile"
$keyInitializer = (($portableKey -split '(..)' |
    Where-Object { $_ }) | ForEach-Object { "0x$_" }) -join ', '

if (-not $ToolchainDir) {
    $tools = Join-Path $root '.tools'
    $ToolchainDir = Join-Path $tools "llvm-mingw-$toolchainVersion-ucrt-x86_64"
    if (-not (Test-Path -LiteralPath $ToolchainDir)) {
        New-Item -ItemType Directory -Force -Path $tools | Out-Null
        $archive = Join-Path $tools $toolchainArchive
        if (-not (Test-Path -LiteralPath $archive)) {
            $url = "https://github.com/mstorsjo/llvm-mingw/releases/download/$toolchainVersion/$toolchainArchive"
            Write-Host "Downloading portable LLVM-MinGW (~191 MB)..."
            Invoke-WebRequest -Uri $url -OutFile $archive
        }
        $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
        if ($actual -ne $toolchainSha256) {
            throw "LLVM-MinGW SHA-256 mismatch: $actual"
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $tools -Force
    }
}

$ToolchainDir = (Resolve-Path -LiteralPath $ToolchainDir).Path
$cc = Join-Path $ToolchainDir 'bin\x86_64-w64-mingw32-clang.exe'
$cxx = Join-Path $ToolchainDir 'bin\x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $cc) -or
    -not (Test-Path -LiteralPath $cxx)) {
    throw "LLVM-MinGW x64 compiler was not found in: $ToolchainDir"
}

$build = Join-Path $root 'build'
$objects = Join-Path $build 'obj'
$out = Join-Path $root 'out'
New-Item -ItemType Directory -Force -Path $objects, $out | Out-Null
$objectFiles = @()

$keyHeader = "#pragma once`ninline constexpr unsigned char kPortableKey[32] = {$keyInitializer};`n"
[IO.File]::WriteAllText((Join-Path $build 'portable_key.h'), $keyHeader,
                        [Text.Encoding]::ASCII)

$common = @(
    '-Oz', '-flto', '-g0', '-DNDEBUG', '-DUNICODE', '-D_UNICODE',
    '-DWIN32_LEAN_AND_MEAN', '-ffunction-sections', '-fdata-sections',
    '-fno-ident'
)
$minhook = Join-Path $root 'third_party\minhook'
$cFlags = $common + @(
    "-I$(Join-Path $minhook 'include')",
    "-I$(Join-Path $minhook 'src')"
)
$cFiles = @('buffer.c', 'hook.c', 'trampoline.c', 'hde\hde64.c')
foreach ($file in $cFiles) {
    $source = Join-Path (Join-Path $minhook 'src') $file
    $name = ($file -replace '[\\.]', '_') + '.o'
    $object = Join-Path $objects $name
    & $cc @cFlags -c $source -o $object
    if ($LASTEXITCODE) { throw "C compilation failed: $file" }
    $objectFiles += $object
}

$sourceDir = Join-Path $root 'src'
$cxxFlags = $common + @(
    '-std=c++23', '-fno-exceptions', '-fno-rtti',
    "-I$(Join-Path $minhook 'include')",
    "-I$build",
    "-I$sourceDir"
)
foreach ($file in Get-ChildItem -LiteralPath $sourceDir -Filter '*.cc' -File) {
    $object = Join-Path $objects ($file.BaseName + '.o')
    & $cxx @cxxFlags -c $file.FullName -o $object
    if ($LASTEXITCODE) { throw "C++ compilation failed: $($file.Name)" }
    $objectFiles += $object
}

$outputDll = Join-Path $out 'version.dll'
$linkArgs = @(
    '-flto', '-shared', '-static',
    '-Wl,--gc-sections,--icf=all,-O2,--strip-all,--dynamicbase,--nxcompat,--no-insert-timestamp',
    '-o', $outputDll
) + $objectFiles + @(
    (Join-Path $sourceDir 'version.def'),
    '-lole32', '-loleaut32', '-luuid', '-lshlwapi', '-lshell32', '-ladvapi32',
    '-lbcrypt'
)
& $cxx @linkArgs
if ($LASTEXITCODE) { throw 'Linking version.dll failed.' }

Copy-Item -LiteralPath (Join-Path $root 'chromic.ini') -Destination $out -Force
$hash = (Get-FileHash -LiteralPath $outputDll -Algorithm SHA256).Hash
Write-Host "Built: $outputDll"
Write-Host "SHA-256: $hash"

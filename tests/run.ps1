[CmdletBinding()]
param([string]$ToolchainDir = $env:CHROMIC_LLVM_MINGW)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $ToolchainDir) {
    $ToolchainDir = Join-Path $root '.tools\llvm-mingw-20260922-ucrt-x86_64'
}
$cc = Join-Path $ToolchainDir 'bin\x86_64-w64-mingw32-clang.exe'
$cxx = Join-Path $ToolchainDir 'bin\x86_64-w64-mingw32-clang++.exe'
$testOutput = Join-Path $root ('build\tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testOutput | Out-Null
[IO.File]::WriteAllText((Join-Path $testOutput 'portable_key.h'),
    'inline constexpr unsigned char kPortableKey[32] = {1,2,3,4,5,6,7,8};')
$minhook = Join-Path $root 'third_party\minhook'
$objects = @()
foreach ($file in @('buffer.c','hook.c','trampoline.c','hde\hde64.c')) {
    $object = Join-Path $testOutput (($file -replace '[\\.]','_') + '.o')
    & $cc "-I$minhook\include" "-I$minhook\src" -c "$minhook\src\$file" -o $object
    if ($LASTEXITCODE) { throw "MinHook test compilation failed: $file" }
    $objects += $object
}
$exe = Join-Path $testOutput 'portable_test.exe'
& $cxx -std=c++23 -Wall -Wextra -Wpedantic -static -DUNICODE -D_UNICODE `
    "-I$minhook\include" "-I$testOutput" "-I$root\src" `
    "$PSScriptRoot\portable_test.cc" "$root\src\utils.cc" "$root\src\config.cc" `
    @objects -lbcrypt -lshell32 -lshlwapi -o $exe
if ($LASTEXITCODE) { throw 'Test compilation failed' }
& $exe
if ($LASTEXITCODE) { throw 'Portable tests failed' }
& (Join-Path $PSScriptRoot 'profile_check_test.ps1') -TestDirectory $testOutput
foreach ($source in Get-ChildItem -LiteralPath "$root\src" -Filter '*.cc') {
    & $cxx -std=c++23 -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE `
        "-I$minhook\include" "-I$testOutput" "-I$root\src" -fsyntax-only $source.FullName
    if ($LASTEXITCODE) { throw "Production syntax check failed: $($source.Name)" }
}

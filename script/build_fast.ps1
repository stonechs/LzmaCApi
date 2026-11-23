$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$fastBuild = Join-Path $build 'fast'
$src = Join-Path $root 'SRC'
$asm = Join-Path $src 'lzma_static_x86.asm'
New-Item -ItemType Directory -Force -Path $fastBuild | Out-Null

$clangCommand = Get-Command clang-cl -ErrorAction SilentlyContinue
$linkerCommand = Get-Command lld-link -ErrorAction SilentlyContinue
if ($clangCommand) { $clang = $clangCommand.Source }
if ($linkerCommand) { $linker = $linkerCommand.Source }
if (-not $clang) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\LLVM\bin\clang-cl.exe'
    if (Test-Path $candidate) { $clang = $candidate }
}
if (-not $linker) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\LLVM\bin\lld-link.exe'
    if (Test-Path $candidate) { $linker = $candidate }
}
if (-not $clang -or -not $linker) {
    throw 'clang-cl and lld-link are required; add LLVM\bin to PATH or install LLVM under LOCALAPPDATA\Programs\LLVM.'
}

$releaseFastObj = Join-Path $fastBuild 'sevenzip_single_fast.obj'
$releaseAsmObj = Join-Path $fastBuild 'lzma_static_x86.obj'
$releaseLib = Join-Path $build 'lzmacapi_x86_fast.lib'
$debugFastObj = Join-Path $fastBuild 'sevenzip_single_fast_debug.obj'
$debugAsmObj = Join-Path $fastBuild 'lzma_static_x86_debug.obj'
$debugLib = Join-Path $build 'lzmacapi_x86_fast_debug.lib'

nasm -f win32 -O9 $asm -o $releaseAsmObj
if ($LASTEXITCODE -ne 0) { throw 'NASM fast release assembly failed' }
nasm -f win32 -O9 -D LZMA_C_API_DEBUG=1 $asm -o $debugAsmObj
if ($LASTEXITCODE -ne 0) { throw 'NASM fast debug assembly failed' }

& $clang --target=i686-pc-windows-msvc /nologo /clang:-Oz /c /TC `
    (Join-Path $src 'sevenzip_single_fast.c') "/Fo$releaseFastObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang fast release parser compilation failed' }
& $linker /lib "/OUT:$releaseLib" $releaseFastObj $releaseAsmObj
if ($LASTEXITCODE -ne 0) { throw 'Fast release static library creation failed' }

& $clang --target=i686-pc-windows-msvc /nologo /clang:-Oz /DLZMA_C_API_DEBUG /c /TC `
    (Join-Path $src 'sevenzip_single_fast.c') "/Fo$debugFastObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang fast debug parser compilation failed' }
& $linker /lib "/OUT:$debugLib" $debugFastObj $debugAsmObj
if ($LASTEXITCODE -ne 0) { throw 'Fast debug static library creation failed' }

Get-Item $releaseLib, $debugLib |
    Select-Object Name, Length, FullName

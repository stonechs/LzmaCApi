$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$work = Join-Path $root 'work'
$src = Join-Path $root 'SRC'
$asm = Join-Path $src 'lzma_static_x86.asm'
$revision = git tag --points-at HEAD | Select-Object -First 1
if (-not $revision) { $revision = git rev-parse --short HEAD }
$buildStamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$buildId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_${buildStamp}_${buildId}"
New-Item -ItemType Directory -Force -Path $build | Out-Null
New-Item -ItemType Directory -Force -Path $work | Out-Null
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /O1'
    "Artifacts and map files: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')

$clangCommand = Get-Command clang-cl -ErrorAction SilentlyContinue
$linkerCommand = Get-Command lld-link -ErrorAction SilentlyContinue
if (-not $clangCommand) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\LLVM\bin\clang-cl.exe'
    if (Test-Path $candidate) { $clang = $candidate }
} else {
    $clang = $clangCommand.Source
}
if (-not $linkerCommand) {
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\LLVM\bin\lld-link.exe'
    if (Test-Path $candidate) { $linker = $candidate }
} else {
    $linker = $linkerCommand.Source
}
if (-not $clang -or -not $linker) {
    throw 'clang-cl and lld-link are required; add LLVM\bin to PATH or install LLVM under LOCALAPPDATA\Programs\LLVM.'
}

$asmObj = Join-Path $build 'lzma_static_x86.obj'
$debugAsmObj = Join-Path $build 'lzma_static_x86_debug.obj'
$cObj = Join-Path $build 'lzma_c_api.obj'
$debugCObj = Join-Path $build 'lzma_c_api_debug.obj'
$sevenZipCObj = Join-Path $build 'sevenzip_single.obj'
$debugSevenZipCObj = Join-Path $build 'sevenzip_single_debug.obj'
$sevenZipFastCObj = Join-Path $build 'sevenzip_single_fast.obj'
$debugSevenZipFastCObj = Join-Path $build 'sevenzip_single_fast_debug.obj'
$lib = Join-Path $build 'lzmacapi_x86.lib'
$debugLib = Join-Path $build 'lzmacapi_x86_debug.lib'
$apiTestExe = Join-Path $artifactDir 'api_test.exe'
$smokeExe = Join-Path $artifactDir 'smoke_release.exe'

nasm -f win32 -O9 $asm -o $asmObj
if ($LASTEXITCODE -ne 0) { throw 'NASM release assembly failed' }
nasm -f win32 -O9 -D LZMA_C_API_DEBUG=1 $asm -o $debugAsmObj
if ($LASTEXITCODE -ne 0) { throw 'NASM debug assembly failed' }

& $clang --target=i686-pc-windows-msvc /nologo /O1 /c /TC (Join-Path $src 'lzma_c_api.c') "/Fo$cObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang C compilation failed; use a Windows SDK and MSVC-compatible runtime installation.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /c /TC (Join-Path $src 'sevenzip_single.c') "/Fo$sevenZipCObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang 7z container compilation failed' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /c /TC (Join-Path $src 'sevenzip_single_fast.c') "/Fo$sevenZipFastCObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang fast 7z container compilation failed' }
& $linker /lib "/OUT:$lib" $cObj $sevenZipCObj $sevenZipFastCObj $asmObj
if ($LASTEXITCODE -ne 0) { throw 'Release static library creation failed' }

& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /c /TC (Join-Path $src 'lzma_c_api.c') "/Fo$debugCObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang debug C compilation failed' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /c /TC (Join-Path $src 'sevenzip_single.c') "/Fo$debugSevenZipCObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang debug 7z container compilation failed' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /c /TC (Join-Path $src 'sevenzip_single_fast.c') "/Fo$debugSevenZipFastCObj"
if ($LASTEXITCODE -ne 0) { throw 'Clang debug fast 7z container compilation failed' }
& $linker /lib "/OUT:$debugLib" $debugCObj $debugSevenZipCObj $debugSevenZipFastCObj $debugAsmObj
if ($LASTEXITCODE -ne 0) { throw 'Debug static library creation failed' }

python (Join-Path $PSScriptRoot 'make_fixture.py') $build
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC (Join-Path $src 'api_test.c') $debugLib "/Fe:$apiTestExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'api_test.map')"
if ($LASTEXITCODE -ne 0) { throw 'Clang API test build failed' }
& $apiTestExe (Join-Path $build 'minprops.raw') (Join-Path $build 'minprops.expected') 0 0 0
if ($LASTEXITCODE -ne 0) { throw 'Minimum properties API tests failed' }
& $apiTestExe (Join-Path $build 'default.raw') (Join-Path $build 'default.expected') 3 0 2
if ($LASTEXITCODE -ne 0) { throw 'Default properties API tests failed' }
& $apiTestExe (Join-Path $build 'wideprops.raw') (Join-Path $build 'wideprops.expected') 2 2 4
if ($LASTEXITCODE -ne 0) { throw 'Wide properties API tests failed' }

& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC (Join-Path $src 'smoke.c') $lib "/Fe:$smokeExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'smoke_release.map')"
if ($LASTEXITCODE -ne 0) { throw 'Clang release smoke build failed' }
& $smokeExe (Join-Path $build 'default.raw') (Join-Path $build 'default.expected')
if ($LASTEXITCODE -ne 0) { throw 'Release library smoke test failed' }

Get-Item $asmObj, $debugAsmObj, $cObj, $sevenZipCObj, $sevenZipFastCObj, $lib, $debugCObj, $debugSevenZipCObj, $debugSevenZipFastCObj, $debugLib,
    $apiTestExe, $smokeExe |
    Select-Object Name, Length
Write-Output "Executables and linker maps saved together in: $artifactDir"

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$work = Join-Path $root 'work'
$clang = (Get-Command clang-cl -ErrorAction Stop).Source
$sevenZip = @(
    (Join-Path $env:ProgramFiles '7-Zip\7z.exe'),
    (Join-Path ${env:ProgramFiles(x86)} '7-Zip\7z.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $sevenZip) { throw '7z.exe not found.' }

& (Join-Path $PSScriptRoot 'build_fast.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Fast LZMA1 library build failed.' }

$revision = git tag --points-at HEAD | Select-Object -First 1
if (-not $revision) { $revision = git rev-parse --short HEAD }
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$id = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_fast_lzma1_smoke_${stamp}_${id}"
$runDir = Join-Path $work "fast_lzma1_smoke_${stamp}"
New-Item -ItemType Directory -Force -Path $artifactDir, $runDir | Out-Null
@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /O1; NASM -O9'
    "Executables and linker maps: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')

$inputPath = Join-Path $runDir 'smoke_input.bin'
$archivePath = Join-Path $runDir 'smoke_lzma1.7z'
$expectedPath = Join-Path $runDir 'smoke_expected.bin'
$bytes = [byte[]]::new(65536)
for ($i = 0; $i -lt $bytes.Length; $i++) {
    $bytes[$i] = [byte](($i * 17 + ($i -shr 5)) -band 255)
}
[IO.File]::WriteAllBytes($inputPath, $bytes)
[IO.File]::WriteAllBytes($expectedPath, $bytes)

& $sevenZip a -t7z '-m0=LZMA:d=1m:lc=2:lp=2:pb=4' -mhc=off -mx=1 -y $archivePath $inputPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw '7-Zip failed to create the LZMA1 smoke archive.' }

$releaseExe = Join-Path $artifactDir 'sevenzip_fast_lzma1_release.exe'
$debugExe = Join-Path $artifactDir 'sevenzip_fast_lzma1_debug.exe'
$releaseMap = Join-Path $artifactDir 'sevenzip_fast_lzma1_release.map'
$debugMap = Join-Path $artifactDir 'sevenzip_fast_lzma1_debug.map'
$tester = Join-Path $root 'SRC\sevenzip_fast_test.c'
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC $tester `
    (Join-Path $build 'lzmacapi_x86_fast.lib') "/Fe:$releaseExe" `
    /link /subsystem:console "/MAP:$releaseMap"
if ($LASTEXITCODE -ne 0) { throw 'Could not link the release fast tester.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC $tester `
    (Join-Path $build 'lzmacapi_x86_fast_debug.lib') "/Fe:$debugExe" `
    /link /subsystem:console "/MAP:$debugMap"
if ($LASTEXITCODE -ne 0) { throw 'Could not link the debug fast tester.' }

& $releaseExe $archivePath $expectedPath
if ($LASTEXITCODE -ne 0) { throw 'Release LZMA1 smoke test failed.' }
& $debugExe $archivePath $expectedPath
if ($LASTEXITCODE -ne 0) { throw 'Debug LZMA1 smoke test failed.' }
Write-Output "PASS: fast LZMA1 release/debug smoke; maps and EXEs: $artifactDir"

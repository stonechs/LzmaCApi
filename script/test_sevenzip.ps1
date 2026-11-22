$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$testDir = Join-Path $build 'sevenzip_single_test'
$clang = (Get-Command clang-cl -ErrorAction Stop).Source
$revision = git describe --tags --exact-match 2>$null
if (-not $revision) { $revision = git rev-parse --short HEAD }
$buildStamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$buildId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_sevenzip_${buildStamp}_${buildId}"
$sevenZipCandidates = @(
    (Join-Path $env:ProgramFiles '7-Zip\7z.exe'),
    (Join-Path ${env:ProgramFiles(x86)} '7-Zip\7z.exe')
)
$sevenZip = $sevenZipCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $sevenZip) { throw '7z.exe not found; install 7-Zip or add 7z.exe to PATH.' }
if (-not (Test-Path (Join-Path $build 'lzmacapi_x86.lib'))) {
    throw 'Run .\script\build.ps1 before this test.'
}

New-Item -ItemType Directory -Force -Path $testDir | Out-Null
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /O1'
    "Executables and map files: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')
$inputPath = Join-Path $testDir 'single_input.bin'
$expectedPath = Join-Path $testDir 'single_expected.bin'
$testExe = Join-Path $artifactDir 'sevenzip_test.exe'
$debugTestExe = Join-Path $artifactDir 'sevenzip_test_debug.exe'
$bytes = [byte[]]::new(65536)
for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [byte](($i * 17 + ($i -shr 5)) -band 255) }
[IO.File]::WriteAllBytes($inputPath, $bytes)
[IO.File]::WriteAllBytes($expectedPath, $bytes)

& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $root 'SRC\sevenzip_test.c') `
    (Join-Path $build 'lzmacapi_x86.lib') "/Fe:$testExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_test.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not build the 7z container test program.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /TC `
    (Join-Path $root 'SRC\sevenzip_test.c') `
    (Join-Path $build 'lzmacapi_x86_debug.lib') "/Fe:$debugTestExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_test_debug.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not build the debug 7z container test program.' }
foreach ($case in @(
    @{ Name = 'single_lzma'; Method = '-m0=LZMA' },
    @{ Name = 'single_wide'; Method = '-m0=LZMA:d=1m:lc=2:lp=2:pb=4' }
)) {
    $archivePath = Join-Path $testDir ($case.Name + '.7z')
    & $sevenZip a -t7z $case.Method -mhc=off -mx=1 -y $archivePath $inputPath | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7-Zip failed to create fixture $($case.Name)." }
    & $testExe $archivePath $expectedPath
    if ($LASTEXITCODE -ne 0) { throw "7z container release test failed: $($case.Name)." }
    & $debugTestExe $archivePath $expectedPath
    if ($LASTEXITCODE -ne 0) { throw "7z container debug test failed: $($case.Name)." }
    Write-Output "7-Zip fixture: $archivePath"
}
Write-Output "Executables and linker maps saved together in: $artifactDir"

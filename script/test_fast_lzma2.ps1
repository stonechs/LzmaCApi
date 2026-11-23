$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\fast_lzma2'
$work = Join-Path $root 'work'
$src = Join-Path $root 'SRC'
$clang = (Get-Command clang-cl -ErrorAction Stop).Source
$linker = (Get-Command lld-link -ErrorAction Stop).Source
$sevenZip = @(
    (Join-Path $env:ProgramFiles '7-Zip\7z.exe'),
    (Join-Path ${env:ProgramFiles(x86)} '7-Zip\7z.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $sevenZip) { throw '7z.exe not found.' }

$revision = git tag --points-at HEAD | Select-Object -First 1
if (-not $revision) { $revision = git rev-parse --short HEAD }
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$id = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_fast_lzma2_${stamp}_${id}"
$runDir = Join-Path $work "fast_lzma2_${stamp}"
New-Item -ItemType Directory -Force -Path $build, $artifactDir, $runDir | Out-Null
@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /O1 /DLZMA_C_API_LZMA2'
    "Executables and linker maps: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')

$releaseAsm = Join-Path $build 'lzma_static_x86_lzma2.obj'
$debugAsm = Join-Path $build 'lzma_static_x86_lzma2_debug.obj'
$releaseApi = Join-Path $build 'lzma_c_api_lzma2.obj'
$debugApi = Join-Path $build 'lzma_c_api_lzma2_debug.obj'
$releaseFast = Join-Path $build 'sevenzip_single_fast_lzma2.obj'
$debugFast = Join-Path $build 'sevenzip_single_fast_lzma2_debug.obj'
$releaseLib = Join-Path $build 'lzmacapi_x86_fast_lzma2.lib'
$debugLib = Join-Path $build 'lzmacapi_x86_fast_lzma2_debug.lib'
$releaseExe = Join-Path $artifactDir 'sevenzip_fast_lzma2_release.exe'
$debugExe = Join-Path $artifactDir 'sevenzip_fast_lzma2_debug.exe'

nasm -f win32 -O9 -D LZMA_C_API_LZMA2=1 (Join-Path $src 'lzma_static_x86.asm') -o $releaseAsm
if ($LASTEXITCODE -ne 0) { throw 'NASM LZMA2 release assembly failed.' }
nasm -f win32 -O9 -D LZMA_C_API_LZMA2=1 -D LZMA_C_API_DEBUG=1 `
    (Join-Path $src 'lzma_static_x86.asm') -o $debugAsm
if ($LASTEXITCODE -ne 0) { throw 'NASM LZMA2 debug assembly failed.' }

& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_LZMA2 /c /TC `
    (Join-Path $src 'lzma_c_api.c') "/Fo$releaseApi"
if ($LASTEXITCODE -ne 0) { throw 'Clang LZMA2 release API compilation failed.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_LZMA2 /c /TC `
    (Join-Path $src 'sevenzip_single_fast.c') "/Fo$releaseFast"
if ($LASTEXITCODE -ne 0) { throw 'Clang LZMA2 release fast-path compilation failed.' }
& $linker /lib "/OUT:$releaseLib" $releaseApi $releaseFast $releaseAsm
if ($LASTEXITCODE -ne 0) { throw 'LZMA2 release static library creation failed.' }

& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_LZMA2 /DLZMA_C_API_DEBUG /c /TC `
    (Join-Path $src 'lzma_c_api.c') "/Fo$debugApi"
if ($LASTEXITCODE -ne 0) { throw 'Clang LZMA2 debug API compilation failed.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_LZMA2 /DLZMA_C_API_DEBUG /c /TC `
    (Join-Path $src 'sevenzip_single_fast.c') "/Fo$debugFast"
if ($LASTEXITCODE -ne 0) { throw 'Clang LZMA2 debug fast-path compilation failed.' }
& $linker /lib "/OUT:$debugLib" $debugApi $debugFast $debugAsm
if ($LASTEXITCODE -ne 0) { throw 'LZMA2 debug static library creation failed.' }

python (Join-Path $PSScriptRoot 'make_fixture.py') $build
if ($LASTEXITCODE -ne 0) { throw 'Raw LZMA fixture generation failed.' }
$apiTest = Join-Path $artifactDir 'api_test_lzma2_macro.exe'
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $src 'api_test.c') $debugLib "/Fe:$apiTest" `
    /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'api_test_lzma2_macro.map')"
if ($LASTEXITCODE -ne 0) { throw 'LZMA2-macro raw C API test link failed.' }
foreach ($case in @(
    @{ Name='minprops'; Lc=0; Lp=0; Pb=0 },
    @{ Name='default'; Lc=3; Lp=0; Pb=2 },
    @{ Name='wideprops'; Lc=2; Lp=2; Pb=4 }
)) {
    & $apiTest (Join-Path $build ($case.Name + '.raw')) `
        (Join-Path $build ($case.Name + '.expected')) $case.Lc $case.Lp $case.Pb
    if ($LASTEXITCODE -ne 0) { throw "LZMA2-macro raw API regression failed: $($case.Name)." }
}

& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $src 'sevenzip_fast_test.c') $releaseLib "/Fe:$releaseExe" `
    /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_fast_lzma2_release.map')"
if ($LASTEXITCODE -ne 0) { throw 'LZMA2 release test executable link failed.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $src 'sevenzip_fast_test.c') $debugLib "/Fe:$debugExe" `
    /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_fast_lzma2_debug.map')"
if ($LASTEXITCODE -ne 0) { throw 'LZMA2 debug test executable link failed.' }

$patternPath = Join-Path $runDir 'pattern_5m.bin'
$randomPath = Join-Path $runDir 'random_2m.bin'
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
$pattern = [byte[]]::new(5MB)
for ($i = 0; $i -lt $pattern.Length; $i++) {
    $pattern[$i] = [byte](($i * 17 + ($i -shr 5) + ($i -shr 13)) -band 255)
}
[IO.File]::WriteAllBytes($patternPath, $pattern)
$random = [byte[]]::new(2MB)
$rng.GetBytes($random)
[IO.File]::WriteAllBytes($randomPath, $random)
$mixedPath = Join-Path $runDir 'mixed_5m.bin'
$mixed = [byte[]]::new(5MB)
$rng.GetBytes($mixed)
$rng.Dispose()
for ($i = 3MB; $i -lt $mixed.Length; $i++) {
    $mixed[$i] = [byte](($i * 17 + ($i -shr 5) + ($i -shr 13)) -band 255)
}
[IO.File]::WriteAllBytes($mixedPath, $mixed)

$cases = @(
    @{ Name='lzma1_compat'; Input=$patternPath; Method='-m0=LZMA:d=8m:lc=3:lp=0:pb=2' },
    @{ Name='lzma2_multi_chunk'; Input=$patternPath; Method='-m0=LZMA2:d=8m:lc=3:lp=0:pb=2' },
    @{ Name='lzma2_raw_candidate'; Input=$randomPath; Method='-m0=LZMA2:d=2m:lc=3:lp=0:pb=2' },
    @{ Name='lzma2_mixed_reset'; Input=$mixedPath; Method='-m0=LZMA2:d=8m:lc=3:lp=0:pb=2' }
)
$failed = [Collections.Generic.List[string]]::new()
foreach ($case in $cases) {
    $archive = Join-Path $runDir ($case.Name + '.7z')
    $extractDir = Join-Path $runDir ($case.Name + '_extract')
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    & $sevenZip a -t7z $case.Method -mhc=off -mx=1 -y $archive $case.Input | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7-Zip fixture creation failed: $($case.Name)" }
    & $sevenZip x -y "-o$extractDir" $archive | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7-Zip fixture extraction failed: $($case.Name)" }
    $members = @(Get-ChildItem -LiteralPath $extractDir -File -Recurse)
    if ($members.Count -ne 1) { throw "Expected one extracted file for $($case.Name)." }
    foreach ($test in @(@{ Name='release'; Exe=$releaseExe }, @{ Name='debug'; Exe=$debugExe })) {
        & $test.Exe $archive $members[0].FullName
        if ($LASTEXITCODE -ne 0) { $failed.Add("$($case.Name) $($test.Name)") }
    }
}
Get-Item $releaseLib, $debugLib, $releaseExe, $debugExe |
    Select-Object Name, Length
Write-Output "Executables and linker maps saved with their maps in: $artifactDir"
Write-Output "Fixtures and extracted references: $runDir"
if ($failed.Count) { throw "LZMA2 fast tests failed: $($failed -join ', ')" }
Write-Output "PASS: $($cases.Count) archive cases passed in release and debug builds."

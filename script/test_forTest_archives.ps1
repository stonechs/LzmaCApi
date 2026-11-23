$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$forTest = Join-Path $root 'forTest'
$work = Join-Path $root 'work'
$clang = (Get-Command clang-cl -ErrorAction Stop).Source
$sevenZip = @(
    (Join-Path $env:ProgramFiles '7-Zip\7z.exe'),
    (Join-Path ${env:ProgramFiles(x86)} '7-Zip\7z.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$archives = @(Get-ChildItem -LiteralPath $forTest -Filter '*.7z' -File -Recurse)
if (-not $sevenZip) { throw '7z.exe not found.' }
if ($archives.Count -eq 0) { throw "No .7z files found in $forTest" }
& (Join-Path $PSScriptRoot 'build_fast.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Fast LZMA1 library build failed.' }

$runRoot = Join-Path $work ('forTest_decode_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$extractRoot = Join-Path $runRoot 'extracted'
$revision = git tag --points-at HEAD | Select-Object -First 1
if (-not $revision) { $revision = git rev-parse --short HEAD }
$buildStamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$buildId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_forTest_${buildStamp}_${buildId}"
New-Item -ItemType Directory -Force -Path $extractRoot, $artifactDir | Out-Null
@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /O1; NASM -O9'
    "Executables and linker maps: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')
$releaseExe = Join-Path $artifactDir 'sevenzip_fast_lzma1_forTest_release.exe'
$debugExe = Join-Path $artifactDir 'sevenzip_fast_lzma1_forTest_debug.exe'
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $root 'SRC\sevenzip_fast_test.c') `
    (Join-Path $build 'lzmacapi_x86_fast.lib') "/Fe:$releaseExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_fast_lzma1_forTest_release.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not build the release archive tester.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /TC `
    (Join-Path $root 'SRC\sevenzip_fast_test.c') `
    (Join-Path $build 'lzmacapi_x86_fast_debug.lib') "/Fe:$debugExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_fast_lzma1_forTest_debug.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not build the debug archive tester.' }

$results = [Collections.Generic.List[object]]::new()
foreach ($archive in $archives) {
    $listing = & $sevenZip l -slt $archive.FullName
    $methodLine = $listing | Where-Object { $_ -match '^Method = ' } | Select-Object -First 1
    if ($methodLine -notmatch '^Method = LZMA(?:$|:|\s)') {
        $results.Add([pscustomobject]@{
            Archive=$archive.Name; File=''; Bytes=0
            Release="SKIP ($($methodLine -replace '^Method = ', ''))"
            Debug="SKIP ($($methodLine -replace '^Method = ', ''))"
        })
        continue
    }
    $caseDir = Join-Path $extractRoot ([IO.Path]::GetFileNameWithoutExtension($archive.Name))
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null
    & $sevenZip x -y "-o$caseDir" $archive.FullName | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $results.Add([pscustomobject]@{ Archive=$archive.Name; File=''; Bytes=0; Release='7z extract failed'; Debug='7z extract failed' })
        continue
    }
    $members = @(Get-ChildItem -LiteralPath $caseDir -File -Recurse)
    if ($members.Count -ne 1) {
        $results.Add([pscustomobject]@{ Archive=$archive.Name; File=''; Bytes=0; Release="expected one file, found $($members.Count)"; Debug='not run' })
        continue
    }
    & $releaseExe $archive.FullName $members[0].FullName | Out-Null
    $releaseStatus = if ($LASTEXITCODE -eq 0) { 'PASS' } else { "FAIL ($LASTEXITCODE)" }
    & $debugExe $archive.FullName $members[0].FullName | Out-Null
    $debugStatus = if ($LASTEXITCODE -eq 0) { 'PASS' } else { "FAIL ($LASTEXITCODE)" }
    $results.Add([pscustomobject]@{
        Archive=$archive.Name; File=$members[0].Name; Bytes=$members[0].Length
        Release=$releaseStatus; Debug=$debugStatus
    })
}

$report = Join-Path $runRoot 'results.csv'
$results | Export-Csv -LiteralPath $report -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
Write-Output "Report: $report"
if (@($results | Where-Object { $_.Release -notmatch '^PASS|^SKIP' -or $_.Debug -notmatch '^PASS|^SKIP' }).Count) {
    throw 'One or more forTest archives did not pass both decoder builds.'
}
$passed = @($results | Where-Object { $_.Release -eq 'PASS' -and $_.Debug -eq 'PASS' }).Count
$skipped = @($results | Where-Object { $_.Release -like 'SKIP*' }).Count
Write-Output "PASS: $passed LZMA1 archives decoded in release/debug; $skipped unsupported archives skipped."

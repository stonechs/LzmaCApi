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
if (-not (Test-Path (Join-Path $build 'lzmacapi_x86.lib'))) {
    throw 'Run .\script\build.ps1 before this test.'
}

$runRoot = Join-Path $work ('forTest_decode_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$extractRoot = Join-Path $runRoot 'extracted'
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
$releaseExe = Join-Path $runRoot 'sevenzip_release.exe'
$debugExe = Join-Path $runRoot 'sevenzip_debug.exe'
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $root 'SRC\sevenzip_test.c') `
    (Join-Path $build 'lzmacapi_x86.lib') "/Fe:$releaseExe" /link /subsystem:console
if ($LASTEXITCODE -ne 0) { throw 'Could not build the release archive tester.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /TC `
    (Join-Path $root 'SRC\sevenzip_test.c') `
    (Join-Path $build 'lzmacapi_x86_debug.lib') "/Fe:$debugExe" /link /subsystem:console
if ($LASTEXITCODE -ne 0) { throw 'Could not build the debug archive tester.' }

$results = [Collections.Generic.List[object]]::new()
foreach ($archive in $archives) {
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
if (@($results | Where-Object { $_.Release -ne 'PASS' -or $_.Debug -ne 'PASS' }).Count) {
    throw 'One or more forTest archives did not pass both decoder builds.'
}
Write-Output "PASS: $($results.Count) archives decoded and matched 7-Zip output in release and debug builds."

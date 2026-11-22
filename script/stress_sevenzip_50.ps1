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
if (-not (Test-Path (Join-Path $build 'lzmacapi_x86.lib'))) {
    throw 'Run .\script\build.ps1 before this stress test.'
}

$runRoot = Join-Path $work ('stress_sevenzip_50_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$revision = git describe --tags --exact-match 2>$null
if (-not $revision) { $revision = git rev-parse --short HEAD }
$buildStamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$buildId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_stress50_${buildStamp}_${buildId}"
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /O1'
    "Executables and map files: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')
$releaseExe = Join-Path $artifactDir 'sevenzip_release.exe'
$debugExe = Join-Path $artifactDir 'sevenzip_debug.exe'
& $clang --target=i686-pc-windows-msvc /nologo /O1 /TC `
    (Join-Path $root 'SRC\sevenzip_test.c') `
    (Join-Path $build 'lzmacapi_x86.lib') "/Fe:$releaseExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_release.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not build the release stress tester.' }
& $clang --target=i686-pc-windows-msvc /nologo /O1 /DLZMA_C_API_DEBUG /TC `
    (Join-Path $root 'SRC\sevenzip_test.c') `
    (Join-Path $build 'lzmacapi_x86_debug.lib') "/Fe:$debugExe" /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'sevenzip_debug.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not build the debug stress tester.' }

function New-StressBytes([int]$caseIndex, [int]$length) {
    $data = [byte[]]::new($length)
    $kind = $caseIndex % 5
    if ($kind -eq 1) {
        [Random]::new(17000 + $caseIndex * 97).NextBytes($data)
    } elseif ($kind -eq 2) {
        for ($i=0; $i -lt $length; $i++) { $data[$i] = [byte]($i -band 255) }
    } elseif ($kind -eq 3) {
        $pattern = [Text.Encoding]::UTF8.GetBytes("LZMA 7z sample $caseIndex / NES ROM DATA /")
        for ($i=0; $i -lt $length; $i++) { $data[$i] = $pattern[$i % $pattern.Length] }
    } elseif ($kind -eq 4) {
        $random = [Random]::new(23000 + $caseIndex * 31)
        for ($i=0; $i -lt $length; $i++) {
            if (($i % 1024) -lt 896) { $data[$i] = [byte](($i -shr 3) -band 255) }
            else { $data[$i] = [byte]$random.Next(0, 256) }
        }
    }
    return ,$data
}

$results = [Collections.Generic.List[object]]::new()
for ($caseIndex = 0; $caseIndex -lt 50; $caseIndex++) {
    $name = 'case_{0:D2}' -f $caseIndex
    $inputPath = Join-Path $runRoot ($name + '.bin')
    $archivePath = Join-Path $runRoot ($name + '.7z')
    $length = 4096 + (($caseIndex * 7919) % 60000)
    if ($caseIndex -eq 47) { $length = 262144 }
    if ($caseIndex -eq 48) { $length = 524288 }
    if ($caseIndex -eq 49) { $length = 1048576 }
    $data = New-StressBytes $caseIndex $length
    [IO.File]::WriteAllBytes($inputPath, $data)

    # 7-Zip LZMA1 属性覆盖 lc/lp/pb 合法组合；dictionary 和压缩等级也变化。
    $lc = $caseIndex % 5
    $lp = [int]([Math]::Floor($caseIndex / 5) % (5 - $lc))
    $pb = ($caseIndex * 3) % 5
    $dict = @('64k','256k','1m','4m')[$caseIndex % 4]
    $level = 1 + ($caseIndex % 5)
    $method = "-m0=LZMA:d=$dict`:lc=$lc`:lp=$lp`:pb=$pb"
    & $sevenZip a -t7z $method "-mx=$level" -mhc=off -y $archivePath $inputPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $results.Add([pscustomobject]@{ Archive=$name; InputBytes=$length; Lc=$lc; Lp=$lp; Pb=$pb; Dictionary=$dict; Level=$level; Release='7z create failed'; Debug='not run' })
        continue
    }

    & $releaseExe $archivePath $inputPath | Out-Null
    $releaseStatus = if ($LASTEXITCODE -eq 0) { 'PASS' } else { "FAIL ($LASTEXITCODE)" }
    & $debugExe $archivePath $inputPath | Out-Null
    $debugStatus = if ($LASTEXITCODE -eq 0) { 'PASS' } else { "FAIL ($LASTEXITCODE)" }
    $results.Add([pscustomobject]@{
        Archive=($name + '.7z'); InputBytes=$length; Lc=$lc; Lp=$lp; Pb=$pb
        Dictionary=$dict; Level=$level; Release=$releaseStatus; Debug=$debugStatus
    })
    if (($caseIndex + 1) % 10 -eq 0) { Write-Output "Completed $($caseIndex + 1)/50 archives" }
}

$report = Join-Path $runRoot 'results.csv'
$results | Export-Csv -LiteralPath $report -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
Write-Output "Report: $report"
Write-Output "Executables and linker maps saved together in: $artifactDir"
$failed = @($results | Where-Object { $_.Release -ne 'PASS' -or $_.Debug -ne 'PASS' })
if ($results.Count -ne 50 -or $failed.Count) {
    throw "Stress test incomplete: $($results.Count)/50 completed; $($failed.Count) failed."
}
Write-Output 'PASS: all 50 LZMA1 7z archives decoded and matched in release and debug builds.'

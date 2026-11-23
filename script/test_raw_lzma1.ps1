$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$work = Join-Path $root 'work'
$src = Join-Path $root 'SRC'
$clang = (Get-Command clang-cl -ErrorAction Stop).Source
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw 'Python 3 with the standard lzma module is required for this raw-stream smoke test.' }

& (Join-Path $PSScriptRoot 'build_fast.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Fast LZMA1 library build failed.' }

$runRoot = Join-Path $work ('raw_lzma1_smoke_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$revision = git tag --points-at HEAD | Select-Object -First 1
if (-not $revision) { $revision = git rev-parse --short HEAD }
$buildStamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$buildId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$artifactDir = Join-Path $root "dist\builds\${revision}_raw_lzma1_${buildStamp}_${buildId}"
New-Item -ItemType Directory -Force -Path $runRoot, $artifactDir | Out-Null

$inputPath = Join-Path $runRoot 'expected.bin'
$streamPath = Join-Path $runRoot 'sample.lzma'
$bytes = [byte[]]::new(65536)
for ($i = 0; $i -lt $bytes.Length; $i++) {
    $bytes[$i] = [byte](($i * 17 + ($i -shr 5)) -band 255)
}
[IO.File]::WriteAllBytes($inputPath, $bytes)
$pythonCode = "import lzma,sys; d=open(sys.argv[1],'rb').read(); f=[{'id':lzma.FILTER_LZMA1,'dict_size':1<<20,'lc':2,'lp':2,'pb':4}]; open(sys.argv[2],'wb').write(lzma.compress(d,format=lzma.FORMAT_ALONE,filters=f))"
& $python -c $pythonCode $inputPath $streamPath
if ($LASTEXITCODE -ne 0) { throw 'Python failed to create the LZMA-Alone smoke stream.' }

@(
    "Revision: $revision"
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K')"
    'Target: i686-pc-windows-msvc'
    'Compiler flags: clang-cl /clang:-Oz; NASM -O9'
    'Coverage: lzma_static_x86_run raw LZMA1 entry, release/debug'
    "Executables and linker maps: $artifactDir"
) | Set-Content -Encoding ascii (Join-Path $artifactDir 'build_info.txt')

$tester = Join-Path $src 'lzma_raw_test.c'
$releaseExe = Join-Path $artifactDir 'lzma_raw_release.exe'
$debugExe = Join-Path $artifactDir 'lzma_raw_debug.exe'
& $clang --target=i686-pc-windows-msvc /nologo /clang:-Oz /I $src /TC $tester `
    (Join-Path $build 'lzmacapi_x86_fast.lib') "/Fe:$releaseExe" `
    /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'lzma_raw_release.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not link the release raw LZMA tester.' }
& $clang --target=i686-pc-windows-msvc /nologo /clang:-Oz /DLZMA_C_API_DEBUG /I $src /TC $tester `
    (Join-Path $build 'lzmacapi_x86_fast_debug.lib') "/Fe:$debugExe" `
    /link /subsystem:console "/MAP:$(Join-Path $artifactDir 'lzma_raw_debug.map')"
if ($LASTEXITCODE -ne 0) { throw 'Could not link the debug raw LZMA tester.' }

& $releaseExe $streamPath $inputPath
if ($LASTEXITCODE -ne 0) { throw 'Release raw LZMA1 smoke test failed.' }
& $debugExe $streamPath $inputPath
if ($LASTEXITCODE -ne 0) { throw 'Debug raw LZMA1 smoke test failed.' }

Get-Item $releaseExe, $debugExe |
    Select-Object Name, Length, FullName

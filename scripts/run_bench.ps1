# Runs the RTTI microbenchmarks for one build configuration and captures the
# raw output (including the environment header) into docs/benchmarks/.
# Usage:
#   .\scripts\run_bench.ps1 -Config Debug
#   .\scripts\run_bench.ps1 -Config Release -Iterations 50000000
# Options:
#   -Config      Debug | Release (default: Debug)
#   -Iterations  Optional iteration count passed to rtti_bench_new
#   -BuildDir    CMake build tree (default: build)
#   -OutDir      Capture directory (default: docs/benchmarks)

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [string]$Iterations = '',
    [string]$BuildDir = 'build',
    [string]$OutDir = 'docs/benchmarks'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$out = Join-Path $root $OutDir

$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) {
    $cmake = 'cmake'
}

if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    & $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
}

if (-not (Test-Path $out)) {
    New-Item -ItemType Directory -Path $out -Force | Out-Null
}

$stamp = Get-Date -Format 'yyyy-MM-dd-HHmmss'

$targets = @('rtti_bench_new', 'rtti_bench')
foreach ($t in $targets) {
    $exe = Join-Path $build "bench\$Config\$t.exe"
    if (-not (Test-Path $exe)) {
        & $cmake --build $build --config $Config --target $t
        if ($LASTEXITCODE -ne 0) { throw "Failed to build $t" }
    }
    $run_args = @()
    if ($t -eq 'rtti_bench_new' -and $Iterations -ne '') {
        $run_args += $Iterations
    }
    $log = Join-Path $out "$stamp-$Config-$t.txt"
    "run: $exe $run_args  ->  $log"
    & $exe @run_args 2>&1 | Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) { throw "$t exited with $LASTEXITCODE" }
}

"Captured logs:"
Get-ChildItem (Join-Path $out "$stamp-$Config-*.txt") | Select-Object -ExpandProperty FullName
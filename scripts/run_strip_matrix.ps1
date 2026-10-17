# Re-scan the six-gate strip matrix (2^6 = 64 combinations).
#
# For every combination the script configures a scratch build directory,
# verifies the RTTI_ENABLE_* values read back from its CMakeCache.txt, builds
# the test executables that the combination enables, runs them from the
# repository root and appends one
#   STRIP <bits> rtti=<assertions>/<failures> proto_ext=... flat=... exit=<rc>
# line to the output file. The committed result is strip_matrix_64.txt at the
# repository root.
#
# Gate bit order, left to right: JSON XML INI TOML PROTO FLAT. Combinations are
# enumerated by ascending popcount, then binary-ascending within a popcount
# group (the strip_matrix_64.txt ordering).
#
# Usage:
#   powershell -File scripts\run_strip_matrix.ps1 [-Skip N] [-Count M]
#                                                 [-Out <path>] [-Cmake <path>]
# A full refresh starts at -Skip 0 (which truncates the output file); use
# -Skip K -Count M to resume a partial sweep into the same file.
param(
    [int]$Skip = 0,
    [int]$Count = 64,
    [string]$Out = "",
    [string]$Cmake = ""
)

$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($Out)) {
    $Out = Join-Path $repo "strip_matrix_64.txt"
}

if ([string]::IsNullOrEmpty($Cmake)) {
    $Cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
    if (-not $Cmake) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vs = & $vswhere -latest -property installationPath
            if ($vs) {
                $Cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            }
        }
    }
}
if (-not $Cmake -or -not (Test-Path $Cmake)) {
    throw "cmake not found; pass -Cmake <path-to-cmake.exe>"
}

if ($Skip -eq 0) { Remove-Item -LiteralPath $Out -Force -ErrorAction SilentlyContinue }

$all = 0..63 | ForEach-Object { [Convert]::ToString($_, 2).PadLeft(6, '0') } |
    Sort-Object -Property @{Expression = { ($_ -replace '0', '').Length } }, @{Expression = { $_ } }
$batch = @($all | Select-Object -Skip $Skip -First $Count)

foreach ($c in $batch) {
    $j = $c.Substring(0, 1)  # JSON
    $x = $c.Substring(1, 1)  # XML
    $i = $c.Substring(2, 1)  # INI
    $t = $c.Substring(3, 1)  # TOML
    $p = $c.Substring(4, 1)  # PROTO
    $f = $c.Substring(5, 1)  # FLAT
    $dir = Join-Path $repo "build-strip-$c"
    $sw = [Diagnostics.Stopwatch]::StartNew()

    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    & $Cmake -S $repo -B $dir -A x64 `
        "-DRTTI_ENABLE_JSON=$j" "-DRTTI_ENABLE_XML=$x" "-DRTTI_ENABLE_INI=$i" `
        "-DRTTI_ENABLE_TOML=$t" "-DRTTI_ENABLE_PROTO=$p" "-DRTTI_ENABLE_FLAT=$f" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        "STRIP $c CONFIGURE_FAIL exit=$LASTEXITCODE" | Add-Content -LiteralPath $Out -Encoding ASCII
        Write-Output "STRIP $c CONFIGURE_FAIL"
        continue
    }

    # Cache read-back: the configured gate values must match the request.
    $cache = Get-Content -LiteralPath (Join-Path $dir 'CMakeCache.txt') -Raw
    $ok = $true
    foreach ($pair in @(@('JSON', $j), @('XML', $x), @('INI', $i), @('TOML', $t), @('PROTO', $p), @('FLAT', $f))) {
        if ($cache -notmatch "RTTI_ENABLE_$($pair[0]):BOOL=$($pair[1])") { $ok = $false }
    }
    if (-not $ok) {
        "STRIP $c CACHE_MISMATCH exit=1" | Add-Content -LiteralPath $Out -Encoding ASCII
        Write-Output "STRIP $c CACHE_MISMATCH"
        continue
    }

    $targets = @('rtti_tests')
    if ($p -eq '1') { $targets += 'rtti_proto_extension_tests' }
    if ($f -eq '1') { $targets += 'rtti_flat_schema_tests' }
    & $Cmake --build $dir --config Debug --target $targets | Out-Null
    if ($LASTEXITCODE -ne 0) {
        "STRIP $c BUILD_FAIL exit=$LASTEXITCODE" | Add-Content -LiteralPath $Out -Encoding ASCII
        Write-Output "STRIP $c BUILD_FAIL ($([math]::Round($sw.Elapsed.TotalSeconds))s)"
        continue
    }

    Push-Location $repo
    try {
        $out1 = & (Join-Path $dir 'tests\Debug\rtti_tests.exe') 2>&1 | Out-String
        $rc = $LASTEXITCODE
        $proto = 'n/a'
        if ($p -eq '1') {
            $out2 = & (Join-Path $dir 'tests\Debug\rtti_proto_extension_tests.exe') 2>&1 | Out-String
            $rc2 = $LASTEXITCODE
            $m2 = [regex]::Match($out2, '(\d+) assertions, (\d+) failures')
            $proto = if ($m2.Success) { "$($m2.Groups[1].Value)/$($m2.Groups[2].Value)" } else { 'NO_SUMMARY' }
            if ($rc2 -ne 0) { $rc = $rc2 }
        }
        $flatv = 'n/a'
        if ($f -eq '1') {
            $out3 = & (Join-Path $dir 'tests\Debug\rtti_flat_schema_tests.exe') 2>&1 | Out-String
            $rc3 = $LASTEXITCODE
            $m3 = [regex]::Match($out3, '(\d+) assertions, (\d+) failures')
            $flatv = if ($m3.Success) { "$($m3.Groups[1].Value)/$($m3.Groups[2].Value)" } else { 'NO_SUMMARY' }
            if ($rc3 -ne 0) { $rc = $rc3 }
        }
    } finally {
        Pop-Location
    }

    $m1 = [regex]::Match($out1, '(\d+) assertions, (\d+) failures')
    $rtti = if ($m1.Success) { "$($m1.Groups[1].Value)/$($m1.Groups[2].Value)" } else { 'NO_SUMMARY' }
    $line = "STRIP $c rtti=$rtti proto_ext=$proto flat=$flatv exit=$rc"
    $line | Add-Content -LiteralPath $Out -Encoding ASCII
    Write-Output "$line ($([math]::Round($sw.Elapsed.TotalSeconds))s)"
    Remove-Item -Recurse -Force $dir
}
Write-Output "BATCH DONE: Skip=$Skip Count=$Count"

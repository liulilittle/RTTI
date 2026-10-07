# Strip-matrix runner: configures a scratch build dir with one of the 64
# six-gate combinations, builds it, runs the test exe(s), prints the assertion
# count. Usage: .opencode\strip_matrix.ps1 -JSON ON -XML OFF -INI OFF -TOML OFF -PROTO OFF -FLAT OFF
# Output: one line `STRIP <code> rtti=<assertions>/<failures> proto_ext=<a>/<f> flat=<a>/<f> exit=<code>`
# Order of the six switches (J X I T P F) is fixed: JSON XML INI TOML PROTO FLAT.
param(
    [string]$JSON = "OFF",
    [string]$XML  = "OFF",
    [string]$INI  = "OFF",
    [string]$TOML = "OFF",
    [string]$PROTO = "OFF",
    [string]$FLAT = "OFF",
    [string]$Slot = "build-strip"
)

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

$code = -join @(
    $(if ($JSON  -eq "ON") { "1" } else { "0" }),
    $(if ($XML   -eq "ON") { "1" } else { "0" }),
    $(if ($INI   -eq "ON") { "1" } else { "0" }),
    $(if ($TOML  -eq "ON") { "1" } else { "0" }),
    $(if ($PROTO -eq "ON") { "1" } else { "0" }),
    $(if ($FLAT  -eq "ON") { "1" } else { "0" })
)

# Clean the scratch dir so every combination starts from a fresh configure
# (avoids stale-cache inheritance that would break gate isolation).
if (Test-Path $Slot) { Remove-Item -Recurse -Force $Slot }

& $cmake -S . -B $Slot -A x64 `
    "-DRTTI_ENABLE_JSON=$JSON" `
    "-DRTTI_ENABLE_XML=$XML" `
    "-DRTTI_ENABLE_INI=$INI" `
    "-DRTTI_ENABLE_TOML=$TOML" `
    "-DRTTI_ENABLE_PROTO=$PROTO" `
    "-DRTTI_ENABLE_FLAT=$FLAT" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Output "STRIP $code CONFIGURE_FAIL exit=$LASTEXITCODE"; exit }

# Verify the cache actually received the switches we asked for (the literal
# "$c.j" trap silently flips every gate ON). Read the configured values back
# from CMakeCache.txt; any mismatch is a hard CONFIG_MISMATCH, never a pass.
$cache = Join-Path $Slot "CMakeCache.txt"
$expected = @{
    "RTTI_ENABLE_JSON" = $JSON
    "RTTI_ENABLE_XML"  = $XML
    "RTTI_ENABLE_INI"  = $INI
    "RTTI_ENABLE_TOML" = $TOML
    "RTTI_ENABLE_PROTO"= $PROTO
    "RTTI_ENABLE_FLAT" = $FLAT
}
$mismatch = @()
foreach ($k in @("RTTI_ENABLE_JSON","RTTI_ENABLE_XML","RTTI_ENABLE_INI","RTTI_ENABLE_TOML","RTTI_ENABLE_PROTO","RTTI_ENABLE_FLAT")) {
    $line = Select-String -Path $cache -Pattern "^${k}:BOOL=(.+)$" | ForEach-Object { $_.Matches[0].Groups[1].Value }
    if ([string]::IsNullOrEmpty($line)) { $mismatch += "$k=missing"; continue }
    $want = $expected[$k]
    if ($line.ToUpper() -ne $want.ToUpper()) { $mismatch += "$k=$line(expect $want)" }
}
if ($mismatch.Count -gt 0) {
    Write-Output "STRIP $code CONFIG_MISMATCH :: $($mismatch -join ', ')"
    exit
}

& $cmake --build $Slot --config Debug | Out-Null
if ($LASTEXITCODE -ne 0) {
    # capture error lines for diagnosis
    $errs = & $cmake --build $Slot --config Debug 2>&1 | Select-String -Pattern "error|warning" | Select-Object -First 3 | Out-String
    Write-Output "STRIP $code BUILD_FAIL exit=$LASTEXITCODE :: $errs"; exit
}

function Get-Summary([string]$exe) {
    if (-not (Test-Path $exe)) { return "n/a" }
    $out = & $exe 2>&1 | Out-String
    $line = ($out -split "`r?`n" | Where-Object { $_ -match "assertions, \d+ failures" }) | Select-Object -First 1
    if ($line) {
        $m = [regex]::Match($line, "(\d+) assertions, (\d+) failures")
        if ($m.Success) { return "$($m.Groups[1].Value)/$($m.Groups[2].Value)" }
    }
    return "no-summary"
}

$rtti  = Get-Summary "$Slot\tests\Debug\rtti_tests.exe"
$proto = Get-Summary "$Slot\tests\Debug\rtti_proto_extension_tests.exe"
$flat  = Get-Summary "$Slot\tests\Debug\rtti_flat_schema_tests.exe"
Write-Output "STRIP $code rtti=$rtti proto_ext=$proto flat=$flat exit=$LASTEXITCODE"

Remove-Item -Recurse -Force $Slot
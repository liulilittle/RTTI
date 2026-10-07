# Strip-matrix runner: configures a scratch build dir with one of the 64
# six-gate combinations, builds it, runs the test exe, prints the assertion
# count. Usage: .opencode\strip_matrix.ps1 -JSON ON -XML OFF -INI ON -TOML ON -PROTO OFF -FLAT ON
# Output: one line `STRIP <JSON><XML><INI><TOML><PROTO><FLAT> <assertions> <failures> exit=<code>`
param(
    [string]$JSON = "OFF",
    [string]$XML  = "OFF",
    [string]$INI  = "OFF",
    [string]$TOML = "OFF",
    [string]$PROTO = "OFF",
    [string]$FLAT = "OFF"
)

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$slot  = "build-strip"
$tag   = "{0}{1}{2}{3}{4}{5}" -f $JSON, $XML, $INI, $TOML, $PROTO, $FLAT

# Clean the scratch dir so every combination starts from a fresh configure
# (avoids stale-cache inheritance that would break gate isolation).
if (Test-Path $slot) { Remove-Item -Recurse -Force $slot }

& $cmake -S . -B $slot -A x64 `
    "-DRTTI_ENABLE_JSON=$JSON" `
    "-DRTTI_ENABLE_XML=$XML" `
    "-DRTTI_ENABLE_INI=$INI" `
    "-DRTTI_ENABLE_TOML=$TOML" `
    "-DRTTI_ENABLE_PROTO=$PROTO" `
    "-DRTTI_ENABLE_FLAT=$FLAT" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Output "STRIP $tag CONFIGURE_FAIL exit=$LASTEXITCODE"; exit }

& $cmake --build $slot --config Debug | Out-Null
if ($LASTEXITCODE -ne 0) {
    # capture error lines for diagnosis
    $errs = & $cmake --build $slot --config Debug 2>&1 | Select-String -Pattern "error|warning" | Select-Object -First 3 | Out-String
    Write-Output "STRIP $tag BUILD_FAIL exit=$LASTEXITCODE :: $errs"; exit
}

$exe = "$slot\tests\Debug\rtti_tests.exe"
if (-not (Test-Path $exe)) { Write-Output "STRIP $tag NO_EXE"; exit }
$out = & $exe 2>&1 | Out-String
$code = $LASTEXITCODE
$line = ($out -split "`r?`n" | Where-Object { $_ -match "assertions, \d+ failures" }) | Select-Object -First 1
if ($line) { Write-Output "STRIP $tag $line exit=$code" }
else { Write-Output "STRIP $tag NO_SUMMARY exit=$code" }

Remove-Item -Recurse -Force $slot
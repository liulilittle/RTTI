# Batch strip-matrix runner: iterates over the given 6-gate combos, invoking
# strip_matrix.ps1 for each and printing the summary line.
# Usage: .opencode\batch_strip.ps1 -Combos "000000,100000,010000"
param(
    [string]$Combos
)

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
foreach ($c in $Combos.Split(',')) {
    $c = $c.Trim()
    if ($c.Length -ne 6) { Write-Output "BATCH SKIP $c (len)"; continue }
    $j = $(if ($c.Substring(0,1) -eq "1") { "ON" } else { "OFF" })
    $x = $(if ($c.Substring(1,1) -eq "1") { "ON" } else { "OFF" })
    $i = $(if ($c.Substring(2,1) -eq "1") { "ON" } else { "OFF" })
    $t = $(if ($c.Substring(3,1) -eq "1") { "ON" } else { "OFF" })
    $p = $(if ($c.Substring(4,1) -eq "1") { "ON" } else { "OFF" })
    $f = $(if ($c.Substring(5,1) -eq "1") { "ON" } else { "OFF" })
    & $cmake -E echo "BATCH START $c"
    & powershell -ExecutionPolicy Bypass -File .opencode\strip_matrix.ps1 -JSON $j -XML $x -INI $i -TOML $t -PROTO $p -FLAT $f
}
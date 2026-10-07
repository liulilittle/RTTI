# Batch strip-matrix runner: iterates over the given 6-gate combos (or the full
# 64), invoking strip_matrix.ps1 for each and printing the summary line.
# Usage: .opencode\batch_strip.ps1 -Combos "000000,100000,010000" [-Slot build-strip-N] [-Log out.log]
# Slot isolation: pass a unique -Slot per parallel lane so concurrent runs never
# share a scratch dir. Combos are 6-char bit codes in fixed order J X I T P F.
param(
    [string]$Combos,
    [string]$Slot = "build-strip",
    [string]$Log = ""
)

$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if ([string]::IsNullOrEmpty($Combos)) {
    # Full 64-combination sweep ordered by number of gates on (popcount), then
    # numeric value: 000000 (0 gates), the six single-gate combos, ..., 111111.
    # Grouping by gate count makes the per-combo assertion total monotonically
    # non-decreasing from all-off to all-on (every gate adds its own tests).
    $all = @()
    for ($n = 0; $n -lt 64; $n++) {
        $bits = [Convert]::ToString($n, 2).PadLeft(6, '0')
        $pc = 0
        for ($i = 0; $i -lt 6; $i++) { if ($bits[$i] -eq '1') { $pc++ } }
        $all += ,(("$bits") + ':' + ("$pc"))
    }
    $Combos = (($all | Sort-Object { [int](($_ -split ':')[1]) }, { [int](($_ -split ':')[0]) } | ForEach-Object { ($_ -split ':')[0] }) -join ',')
}

$lines = @()
foreach ($c in $Combos.Split(',')) {
    $c = $c.Trim()
    if ($c.Length -ne 6) { $l = "BATCH SKIP $c (len)"; Write-Output $l; $lines += $l; continue }
    $j = $(if ($c.Substring(0,1) -eq "1") { "ON" } else { "OFF" })
    $x = $(if ($c.Substring(1,1) -eq "1") { "ON" } else { "OFF" })
    $i = $(if ($c.Substring(2,1) -eq "1") { "ON" } else { "OFF" })
    $t = $(if ($c.Substring(3,1) -eq "1") { "ON" } else { "OFF" })
    $p = $(if ($c.Substring(4,1) -eq "1") { "ON" } else { "OFF" })
    $f = $(if ($c.Substring(5,1) -eq "1") { "ON" } else { "OFF" })
    Write-Output "BATCH START $c [slot=$Slot]"
    $out = & powershell -ExecutionPolicy Bypass -File .opencode\strip_matrix.ps1 -JSON $j -XML $x -INI $i -TOML $t -PROTO $p -FLAT $f -Slot $Slot
    $last = $out | Select-Object -Last 1
    Write-Output $last
    $lines += $last
    if ($Log -ne "") { $lines | Set-Content -Path $Log -Encoding UTF8 }
}
if ($Log -ne "") { $lines | Set-Content -Path $Log -Encoding UTF8 }
Write-Output "BATCH DONE $($lines.Count) combos"
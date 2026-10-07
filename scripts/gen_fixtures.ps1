<#
.SYNOPSIS
    Regenerates the official protoc/flatc fixtures under tests/fixtures.

.DESCRIPTION
    proto fixtures: for each tests/fixtures/proto/<B>.proto compile every
    matching testcase textbin (<B>.textbin, or <B>.<Message>.textbin) with
       protoc --encode=rtti.fixt.<Message> ... < in  > <B>.<Message>.wire
    then read back with --decode and assert the expected marker values.
    flat fixtures: for each tests/fixtures/flat/<F>.fbs generate <F>_generated.h
    (--cpp) and convert every *.json to <F>.bin (--binary, JSON -> binary).

    Generation requires the official tools; the produced fixtures are committed
    so CI/tests never need the tools installed.

.PARAMETER Proto
    Path to protoc.exe. Defaults to the official per-user tool install.

.PARAMETER Flatc
    Path to flatc.exe. Defaults to the official per-user tool install.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\gen_fixtures.ps1
    powershell -ExecutionPolicy Bypass -File scripts\gen_fixtures.ps1 -Proto "C:\tools\protoc.exe" -Flatc "C:\tools\flatc.exe"
#>
param(
    [string]$Proto = "$env:USERPROFILE\.rtti-tools\protoc\bin\protoc.exe",
    [string]$Flatc = "$env:USERPROFILE\.rtti-tools\flatc\flatc.exe"
)

$ErrorActionPreference = 'Stop'

$repo     = Split-Path -Parent $PSScriptRoot
$fixtures = Join-Path $repo 'tests\fixtures'
$protoDir = Join-Path $fixtures 'proto'
$flatDir  = Join-Path $fixtures 'flat'

if (-not (Test-Path -LiteralPath $Proto)) { throw "protoc not found: $Proto" }
if (-not (Test-Path -LiteralPath $Flatc)) { throw "flatc not found: $Flatc" }

function Invoke-Native {
    param([string]$Exe, [string[]]$ArgList, [string]$InFile, [string]$OutFile)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput  = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    if ($null -ne $psi.ArgumentList) {
        foreach ($a in $ArgList) { [void]$psi.ArgumentList.Add($a) }
    } else {
        $psi.Arguments = ($ArgList | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
    }
    $p = [System.Diagnostics.Process]::Start($psi)
    if ($InFile) {
        $bytes = [IO.File]::ReadAllBytes($InFile)
        if ($bytes.Length -gt 0) { $p.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length) }
    }
    $p.StandardInput.Close()
    if ($OutFile) {
        $out = [IO.File]::Create($OutFile)
        try { $p.StandardOutput.BaseStream.CopyTo($out) } finally { $out.Dispose() }
    } else {
        $p.StandardOutput.ReadToEnd() | Out-Null
    }
    $stderr = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) {
        throw "command failed (exit $($p.ExitCode)): $Exe $($ArgList -join ' ')`n$stderr"
    }
}

function Assert-Match {
    param([string]$Text, [string]$Pattern, [string]$Label)
    if ($Text -notmatch $Pattern) {
        throw "decode read-back failed for ${Label}: pattern '$Pattern' not found in output."
    }
}

Write-Host "== proto fixtures =="
Get-ChildItem -LiteralPath $protoDir -Filter *.proto | ForEach-Object {
    $protoFile = $_.FullName
    $base      = $_.BaseName
    $pkgMatch  = Select-String -LiteralPath $protoFile -Pattern '^\s*package\s+([\w.]+)' | Select-Object -First 1
    $pkgName   = if ($pkgMatch) { $pkgMatch.Matches[0].Groups[1].Value } else { 'rtti.fixt' }
    $messages  = @(Select-String -LiteralPath $protoFile -Pattern '^\s*message\s+(\w+)' |
        ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($messages.Count -eq 0) { throw "no message found in $($_.Name)" }

    Get-ChildItem -LiteralPath $protoDir -Filter "$base*.textbin" | ForEach-Object {
        $tbBase = $_.BaseName
        if ($tbBase -eq $base) {
            $msg = $messages[0]
        } elseif ($tbBase.StartsWith($base + '.')) {
            $msg = $tbBase.Substring($base.Length + 1)
        } else {
            return
        }
        if ($messages -notcontains $msg) {
            throw "textbin $($_.Name) maps to unknown message '$msg' (declared: $($messages -join ', '))"
        }

        $type     = "$pkgName.$msg"
        $wire     = Join-Path $protoDir "$base.$msg.wire"
        $protoName = "${base}.proto"
        $tmpDec   = Join-Path $env:TEMP "rtti_decode_$pid.txt"

        Write-Host "  protoc --encode=$type  < $($_.Name)  > $(Split-Path -Leaf $wire)"
        Invoke-Native $Proto @("--encode=$type", "-I$protoDir", $protoName) -InFile $_.FullName -OutFile $wire
        if (-not (Test-Path -LiteralPath $wire)) { throw "encode produced no file: $wire" }
        if ((Get-Item -LiteralPath $wire).Length -eq 0) { throw "encode produced empty file: $wire" }

        Invoke-Native $Proto @("--decode=$type", "-I$protoDir", $protoName) -InFile $wire -OutFile $tmpDec
        $decoded = [IO.File]::ReadAllText($tmpDec, [System.Text.Encoding]::UTF8)
        Remove-Item -LiteralPath $tmpDec -Force -ErrorAction SilentlyContinue
        if ([string]::IsNullOrWhiteSpace($decoded)) { throw "decode read-back produced empty output for $wire" }

        if ($base -eq 'basic') {
            if ($msg -eq 'Scalars') {
                Assert-Match $decoded 'a: -123' $wire
                Assert-Match $decoded 'z: -456' $wire
                Assert-Match $decoded 's: "hello proto"' $wire
                Assert-Match $decoded 'key: "alpha"' $wire
                Assert-Match $decoded 'value: 7' $wire
                Assert-Match $decoded 'packed: 1' $wire
                Assert-Match $decoded 'packed: 2' $wire
                Assert-Match $decoded 'packed: 3' $wire
            } elseif ($msg -eq 'Outer') {
                Assert-Match $decoded 'inner \{' $wire
                Assert-Match $decoded 'a: -123' $wire
                Assert-Match $decoded 'z: -456' $wire
                Assert-Match $decoded 's: "hello proto"' $wire
                Assert-Match $decoded 'packed: 2' $wire
            }
        }
        Write-Host "  verified  $(Split-Path -Leaf $wire)  ($((Get-Item -LiteralPath $wire).Length) bytes)"
    }
}

Write-Host "== flat fixtures =="
Get-ChildItem -LiteralPath $flatDir -Filter *.fbs | ForEach-Object {
    $fbs    = $_.FullName
    $header = Join-Path $flatDir ($_.BaseName + '_generated.h')

    Write-Host "  flatc --cpp -o $flatDir  $($_.Name)"
    Invoke-Native $Flatc @('--cpp', '-o', $flatDir, $fbs)
    if (-not (Test-Path -LiteralPath $header)) {
        throw "expected generated header not found: $header (flatc may have used a namespace sub-directory)"
    }

    Get-ChildItem -LiteralPath $flatDir -Filter *.json | ForEach-Object {
        $bin = Join-Path $flatDir ($_.BaseName + '.bin')
        Write-Host "  flatc --binary -o $flatDir  $($_.Name)"
        Invoke-Native $Flatc @('--binary', '-o', $flatDir, $fbs, $_.FullName)
        if (-not (Test-Path -LiteralPath $bin)) { throw "expected binary not found: $bin" }
        if ((Get-Item -LiteralPath $bin).Length -eq 0) { throw "binary file is empty: $bin" }
        Write-Host "  produced  $(Split-Path -Leaf $bin)  ($((Get-Item -LiteralPath $bin).Length) bytes)"
    }
}

Write-Host "fixture regeneration complete."
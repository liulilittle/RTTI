<#
.SYNOPSIS
    Regenerates the 64-ceiling macro-ladder text of include/rtti/reflection.hpp.

.DESCRIPTION
    The registration macros dispatch on RTTI_NARG, so every per-count ladder
    (RTTI_APPLY_*, RTTI_MEMBER_TYPES_*, RTTI_MEMBER_VALUES_*,
    RTTI_TEMPLATE_ARGS_*, RTTI_TAO_STRIP_*, RTTI_CTOR_TYPES_*) must define one
    macro per supported count, and RTTI_NARG_I/RTTI_NARG must carry as many
    counting slots. This script widens the ceiling from 16 to 64:

      1. RTTI_NARG_I gains _17.._64 and RTTI_NARG's default-count tail becomes
         64..0 (in place, replacing the 0..16 block);
      2. each of the six per-count ladder families gains definitions 17..64,
         appended right after its _16 definition;
      3. the RTTI_NARG comment block is updated to document the 0..64 range and
         the >64 hard-error behaviour.

    This is a development helper, not a build step: the generated text is
    committed to the repository and neither CI nor the build ever invokes this
    script. The script is idempotent -- running it on an already-widened header
    rewrites the same generated sections with byte-identical text, so
    `git diff include/rtti/reflection.hpp` stays empty across a regeneration.

    The text layout (line-width thresholds 84/88/92/96/100, continuation
    indents, one blank line between blocks) is byte-compatible with the
    reference generator the ladder text was reviewed against
    (cmake-build-audit-a/gen_ladders.py); regenerate-and-diff is the
    equivalence check.

.PARAMETER Path
    Header file to rewrite in place. Defaults to include/rtti/reflection.hpp
    of this repository.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\gen_ladders.ps1
    powershell -ExecutionPolicy Bypass -File scripts\gen_ladders.ps1 -Path tmp.hpp
#>
[CmdletBinding()]
param(
    [string]$Path
)

$ErrorActionPreference = 'Stop'

# Resolve the default target from the script location. This runs in the body
# (not in the param default) because $PSScriptRoot is not yet populated while
# a -File invocation binds its parameters.
if (-not $Path) {
    $scriptDir = $PSScriptRoot
    if (-not $scriptDir) {
        $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    }
    $Path = Join-Path (Split-Path -Parent $scriptDir) 'include\rtti\reflection.hpp'
}

# ---------------------------------------------------------------------------
# Single-line-layout primitives (mirroring the reference generator exactly).
# ---------------------------------------------------------------------------

function Wrap-Comma {
    param(
        [string]$Prefix,
        [string[]]$Items,
        [int]$Width,
        [string]$Cont,
        [string]$Closer = ')'
    )
    $lines = New-Object System.Collections.Generic.List[string]
    $cur = $Prefix + $Items[0]
    for ($i = 1; $i -lt $Items.Count; $i++) {
        $piece = $Items[$i]
        if ($cur.Trim().Length -gt 0 -and ($cur.Length + $piece.Length + 2) -gt $Width) {
            $lines.Add($cur.TrimEnd() + ',')
            $cur = $Cont + $piece
        } else {
            $cur = $cur + ', ' + $piece
        }
    }
    $lines.Add($cur.TrimEnd() + $Closer)
    return $lines.ToArray()
}

function Wrap-Space {
    param(
        [string]$Prefix,
        [string[]]$Items,
        [int]$Width,
        [string]$Cont
    )
    $lines = New-Object System.Collections.Generic.List[string]
    $cur = $Prefix
    foreach ($it in $Items) {
        if ($cur.Trim().Length -gt 0 -and ($cur.Length + $it.Length + 1) -gt $Width) {
            $lines.Add($cur.TrimEnd())
            $cur = $Cont
        }
        $cur = $cur + $it + ' '
    }
    $lines.Add($cur.TrimEnd())
    return $lines.ToArray()
}

function Join-Def {
    param([string[]]$SigLines, [string[]]$BodyLines)
    $all = New-Object System.Collections.Generic.List[string]
    foreach ($l in @($SigLines)) {
        if ($null -ne $l -and $l.Trim() -ne '') { $all.Add($l) }
    }
    foreach ($l in @($BodyLines)) {
        if ($null -ne $l -and $l.Trim() -ne '') { $all.Add($l) }
    }
    $out = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $all.Count; $i++) {
        if ($i -lt $all.Count - 1) {
            $out.Add($all[$i] + ' \')
        } else {
            $out.Add($all[$i])
        }
    }
    return (($out -join "`n") + "`n")
}

# ---------------------------------------------------------------------------
# Per-family 17..64 generators (MaxN is 64 for the shipped header).
# ---------------------------------------------------------------------------

function Gen-ApplyText {
    param([int]$MaxN = 64)
    $blocks = New-Object System.Collections.Generic.List[string]
    for ($n = 17; $n -le $MaxN; $n++) {
        $head = New-Object System.Collections.Generic.List[string]
        $head.Add('M')
        $head.Add('T')
        $body = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $n; $i++) {
            $head.Add("a$i")
            $body.Add("M(T, a$i)")
        }
        $sig = @(Wrap-Comma -Prefix "#define RTTI_APPLY_$n(" -Items $head.ToArray() -Width 84 -Cont (' ' * 22))
        $bodyLines = @(Wrap-Space -Prefix '    ' -Items $body.ToArray() -Width 88 -Cont '    ')
        $blocks.Add((Join-Def -SigLines $sig -BodyLines $bodyLines))
    }
    return ($blocks -join "`n")
}

function Gen-MemberTypesText {
    param([int]$MaxN = 64)
    $blocks = New-Object System.Collections.Generic.List[string]
    for ($n = 17; $n -le $MaxN; $n++) {
        $head = New-Object System.Collections.Generic.List[string]
        $head.Add('T')
        $body = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $n; $i++) {
            $head.Add("a$i")
            $sep = if ($i -lt $n) { ',' } else { '' }
            $body.Add("decltype(&T::a$i)$sep")
        }
        $sig = @(Wrap-Comma -Prefix "#define RTTI_MEMBER_TYPES_$n(" -Items $head.ToArray() -Width 100 -Cont '    ')
        $bodyLines = @(Wrap-Space -Prefix '    ' -Items $body.ToArray() -Width 92 -Cont '    ')
        $blocks.Add((Join-Def -SigLines $sig -BodyLines $bodyLines))
    }
    return ($blocks -join "`n")
}

function Gen-MemberValuesText {
    param([int]$MaxN = 64)
    $blocks = New-Object System.Collections.Generic.List[string]
    for ($n = 17; $n -le $MaxN; $n++) {
        $head = New-Object System.Collections.Generic.List[string]
        $head.Add('T')
        $body = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $n; $i++) {
            $head.Add("a$i")
            $sep = if ($i -lt $n) { ',' } else { '' }
            $body.Add("&T::a$i$sep")
        }
        $sig = @(Wrap-Comma -Prefix "#define RTTI_MEMBER_VALUES_$n(" -Items $head.ToArray() -Width 100 -Cont '    ')
        $bodyLines = @(Wrap-Space -Prefix '    ' -Items $body.ToArray() -Width 88 -Cont '    ')
        $blocks.Add((Join-Def -SigLines $sig -BodyLines $bodyLines))
    }
    return ($blocks -join "`n")
}

function Gen-TemplateArgsText {
    param([int]$MaxN = 64)
    $blocks = New-Object System.Collections.Generic.List[string]
    for ($n = 17; $n -le $MaxN; $n++) {
        $head = New-Object System.Collections.Generic.List[string]
        $body = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $n; $i++) {
            $head.Add("a$i")
            $sep = if ($i -lt $n) { ',' } else { '' }
            $body.Add("a$i$sep")
        }
        $sig = @(Wrap-Comma -Prefix "#define RTTI_TEMPLATE_ARGS_$n(" -Items $head.ToArray() -Width 96 -Cont '    ')
        $bodyLines = @(Wrap-Space -Prefix '    ' -Items $body.ToArray() -Width 88 -Cont '    ')
        $blocks.Add((Join-Def -SigLines $sig -BodyLines $bodyLines))
    }
    return ($blocks -join "`n")
}

function Gen-TaoStripText {
    param([int]$MaxN = 64)
    $blocks = New-Object System.Collections.Generic.List[string]
    for ($n = 17; $n -le $MaxN; $n++) {
        $head = New-Object System.Collections.Generic.List[string]
        $body = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $n; $i++) {
            $head.Add("a$i")
            $sep = if ($i -lt $n) { ',' } else { '' }
            $body.Add("RTTI_TAO_ELEM(a$i)$sep")
        }
        $sig = @(Wrap-Comma -Prefix "#define RTTI_TAO_STRIP_$n(" -Items $head.ToArray() -Width 96 -Cont '    ')
        $bodyLines = @(Wrap-Space -Prefix '    ' -Items $body.ToArray() -Width 92 -Cont '    ')
        $blocks.Add((Join-Def -SigLines $sig -BodyLines $bodyLines))
    }
    return ($blocks -join "`n")
}

function Gen-CtorTypesText {
    param([int]$MaxN = 64)
    $blocks = New-Object System.Collections.Generic.List[string]
    for ($n = 17; $n -le $MaxN; $n++) {
        $head = New-Object System.Collections.Generic.List[string]
        $head.Add('T')
        $body = New-Object System.Collections.Generic.List[string]
        for ($i = 1; $i -le $n; $i++) {
            $head.Add("a$i")
            $sep = if ($i -lt $n) { ',' } else { '' }
            $body.Add("RTTI_CTOR_TYPE(a$i)$sep")
        }
        $sig = @(Wrap-Comma -Prefix "#define RTTI_CTOR_TYPES_$n(" -Items $head.ToArray() -Width 100 -Cont '    ')
        $bodyLines = @(Wrap-Space -Prefix '    ' -Items $body.ToArray() -Width 92 -Cont '    ')
        $blocks.Add((Join-Def -SigLines $sig -BodyLines $bodyLines))
    }
    return ($blocks -join "`n")
}

function Gen-NargText {
    param([int]$MaxN = 64)
    $items = New-Object System.Collections.Generic.List[string]
    for ($i = 1; $i -le $MaxN; $i++) { $items.Add("_$i") }
    $items.Add('N')
    $items.Add('...')
    $sig = @(Wrap-Comma -Prefix '#define RTTI_NARG_I(' -Items $items.ToArray() -Width 84 -Cont (' ' * 20))
    $part1 = Join-Def -SigLines $sig -BodyLines @('    N')
    $tail = New-Object System.Collections.Generic.List[string]
    $tail.Add('__VA_ARGS__')
    for ($i = $MaxN; $i -ge 0; $i--) { $tail.Add("$i") }
    $body2 = @(Wrap-Comma -Prefix '    RTTI_NARG_I(' -Items $tail.ToArray() -Width 84 -Cont (' ' * 18) -Closer ')')
    $second = Join-Def -SigLines @('#define RTTI_NARG(...)') -BodyLines $body2
    return ($part1 + "`n" + $second + "`n")
}

# ---------------------------------------------------------------------------
# Text splicing helpers.
# ---------------------------------------------------------------------------

function ConvertTo-Crlf {
    param([string]$Text)
    return ($Text -replace "(?<!`r)`n", "`r`n")
}

# Returns @{ Start = <int>; End = <int> } covering the whole definition
# `#define <Family>_<N>(...)` including its final line terminator, or $null
# when the definition is absent.
function Get-DefSegment {
    param([string]$Text, [string]$Name)
    $pat = '(?m)^#define ' + [regex]::Escape($Name) + '\('
    $m = [regex]::Match($Text, $pat)
    if (-not $m.Success) { return $null }
    $tail = $Text.Substring($m.Index)
    $em = [regex]::Match($tail, '^(?:[^\r\n]*\\\r\n)*[^\r\n]*\r\n')
    if (-not $em.Success) {
        throw "cannot find the end of the $Name definition"
    }
    return @{ Start = $m.Index; End = $m.Index + $em.Length }
}

# ---------------------------------------------------------------------------
# Apply the widening (idempotent).
# ---------------------------------------------------------------------------

if (-not (Test-Path -LiteralPath $Path)) {
    throw "header not found: $Path"
}

$text = [IO.File]::ReadAllText($Path)
if ($text.Length -eq 0) { throw "header is empty: $Path" }

# Family text is spliced bottom-up so later anchors keep their positions; each
# family is located by name so the order only matters for readability.
$families = @(
    @{ Name = 'RTTI_CTOR_TYPES';    Text = (Gen-CtorTypesText) },
    @{ Name = 'RTTI_TAO_STRIP';     Text = (Gen-TaoStripText) },
    @{ Name = 'RTTI_TEMPLATE_ARGS'; Text = (Gen-TemplateArgsText) },
    @{ Name = 'RTTI_MEMBER_VALUES'; Text = (Gen-MemberValuesText) },
    @{ Name = 'RTTI_MEMBER_TYPES';  Text = (Gen-MemberTypesText) },
    @{ Name = 'RTTI_APPLY';         Text = (Gen-ApplyText) }
)

foreach ($fam in $families) {
    $name17 = $fam.Name + '_17'
    $segCrlf = ConvertTo-Crlf $fam.Text
    $existing17 = Get-DefSegment -Text $text -Name $name17
    if ($null -ne $existing17) {
        $existing64 = Get-DefSegment -Text $text -Name ($fam.Name + '_64')
        if ($null -eq $existing64) {
            throw "found $name17 but not $($fam.Name)_64; header state is inconsistent"
        }
        $text = $text.Substring(0, $existing17.Start) + $segCrlf +
                $text.Substring($existing64.End)
    } else {
        $seg16 = Get-DefSegment -Text $text -Name ($fam.Name + '_16')
        if ($null -eq $seg16) {
            throw "anchor $($fam.Name)_16 not found; cannot place $name17"
        }
        $text = $text.Substring(0, $seg16.End) + "`r`n" + $segCrlf +
                $text.Substring($seg16.End)
    }
}

# RTTI_NARG_I + RTTI_NARG: in-place widening of the 0..16 block.
$mI = [regex]::Match($text, '(?m)^#define RTTI_NARG_I\(')
if (-not $mI.Success) { throw 'RTTI_NARG_I definition not found' }
$tailI = $text.Substring($mI.Index)
$endI = [regex]::Match($tailI, '^(?:[^\r\n]*\\\r\n)*[^\r\n]*\r\n')
if (-not $endI.Success) { throw 'cannot find the end of RTTI_NARG_I' }
$afterI = $mI.Index + $endI.Length
$mN = [regex]::Match($text.Substring($afterI), '(?m)^#define RTTI_NARG\(')
if (-not $mN.Success) { throw 'RTTI_NARG definition not found after RTTI_NARG_I' }
$nStart = $afterI + $mN.Index
$tailN = $text.Substring($nStart)
$endN = [regex]::Match($tailN, '^(?:[^\r\n]*\\\r\n)*[^\r\n]*\r\n')
if (-not $endN.Success) { throw 'cannot find the end of RTTI_NARG' }
$nEnd = $nStart + $endN.Length
# Absorb the blank lines that follow the block, so a re-run on an
# already-widened header replaces exactly the same range (idempotence): the
# replacement text itself carries the single trailing blank line.
$blankLines = [regex]::Match($text.Substring($nEnd), '^(?:\r\n)*')
$nEnd += $blankLines.Length
$nargText = ConvertTo-Crlf (Gen-NargText)
$text = $text.Substring(0, $mI.Index) + $nargText + $text.Substring($nEnd)

# RTTI_NARG comment block: document the new ceiling and the hard-error form.
$commentOld = (@(
    '// Argument count (0..16). The single-level form (RTTI_NARG -> RTTI_NARG_I,',
    '// no intermediate variadic macro) is required for MSVC''s conformant',
    '// preprocessor (/Zc:preprocessor). Empty lists currently yield 1; the empty',
    '// RTTI_DEFINE_TYPE case (zero fields) is handled in Task 9 separately.'
) -join "`r`n") + "`r`n"
$commentNew = (@(
    '// Argument count (0..64). The single-level form (RTTI_NARG -> RTTI_NARG_I,',
    '// no intermediate variadic macro) is required for MSVC''s conformant',
    '// preprocessor (/Zc:preprocessor). Empty lists currently yield 1; the empty',
    '// RTTI_DEFINE_TYPE case (zero fields) is handled in Task 9 separately.',
    '// More than 64 arguments is a hard compile error: the 65th token is taken',
    '// as the count, so the per-count expansion macro (for example',
    '// RTTI_APPLY_f65 / RTTI_MEMBER_TYPES_f65) is undefined and the compiler',
    '// reports C2061/C3861-style diagnostics.'
) -join "`r`n") + "`r`n"

if ($text.Contains('// Argument count (0..64).')) {
    # already widened -- nothing to do
} elseif ($text.Contains($commentOld)) {
    $text = $text.Replace($commentOld, $commentNew)
} else {
    throw 'RTTI_NARG comment anchor not found; refusing to rewrite the comment'
}

[IO.File]::WriteAllText($Path, $text, (New-Object System.Text.UTF8Encoding($true)))
Write-Host "gen_ladders: widened $Path (families 17..64 appended, RTTI_NARG widened to 0..64)"

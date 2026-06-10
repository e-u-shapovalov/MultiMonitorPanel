#requires -Version 5
<#
.SYNOPSIS
  Bump the MultiMonitorPanel version everywhere it is hard-coded, in one command.

.DESCRIPTION
  Single source of truth for the version is kVersion in src/panel.cpp. Run this
  before a release and it moves the version in every spot that must match it:

    - src/panel.cpp      kVersion    -> X.Y.Z
                         kReleaseDate -> today (or -Date YYYY-MM-DD)
    - res/app.manifest   <assemblyIdentity version="X.Y.Z.0" name="MultiMonitorPanel.App">

  User-facing docs (README*, DOWNLOAD.md, docs/MANUAL.*) deliberately carry NO
  version number and link to /releases/latest, so they never need a bump. After
  editing, the script reports any other place the OLD version still appears in
  tracked files (release history / RELEASE.md), so leftovers can be eyeballed.

  Files are rewritten preserving their original UTF-8 BOM state (panel.cpp builds
  with /utf-8, so no BOM is introduced).

.EXAMPLE
  pwsh scripts/bump-version.ps1 1.2.0
  pwsh scripts/bump-version.ps1 1.2.0 -Date 2026-07-01
  pwsh scripts/bump-version.ps1 1.2.0 -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$Version,
    [string]$Date = (Get-Date -Format 'yyyy-MM-dd'),
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
function Fail([string]$m) { Write-Error "bump-version: $m"; exit 1 }

if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "`"$Version`" is not an X.Y.Z version" }

$root = Split-Path -Parent $PSScriptRoot
$cppPath = Join-Path $root 'src\panel.cpp'
$manPath = Join-Path $root 'res\app.manifest'
foreach ($f in @($cppPath, $manPath)) { if (-not (Test-Path -LiteralPath $f)) { Fail "missing file: $f" } }

# Read/Write helpers that keep the file's original UTF-8 BOM state (don't flip it).
function Read-Text([string]$p) { [System.IO.File]::ReadAllText($p) }
function Save-Text([string]$p, [string]$text) {
    $bytes = [System.IO.File]::ReadAllBytes($p)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    [System.IO.File]::WriteAllText($p, $text, [System.Text.UTF8Encoding]::new($hasBom))
}

# --- discover the current version from the single source of truth ---
$cppText = Read-Text $cppPath
if ($cppText -notmatch 'kVersion\[\]\s*=\s*L"([^"]+)"') { Fail 'kVersion not found in src/panel.cpp' }
$cur = $Matches[1]
if ($cur -eq $Version) { Fail "already at $Version" }
$curEsc = [regex]::Escape($cur)

$changed = New-Object System.Collections.Generic.List[object]
function Note($file, $what, $before, $after) {
    $changed.Add([pscustomobject]@{ File = $file; What = $what; Before = $before; After = $after })
}

# --- src/panel.cpp: kVersion + kReleaseDate (callback form avoids $1/$Version quoting traps) ---
$newCpp = [regex]::Replace($cppText, '(kVersion\[\]\s*=\s*L")[^"]+(")',
    { param($m) $m.Groups[1].Value + $Version + $m.Groups[2].Value })
Note 'src/panel.cpp' 'kVersion' $cur $Version

$dateBefore = if ($cppText -match 'kReleaseDate\[\]\s*=\s*L"([^"]+)"') { $Matches[1] } else { '(unknown)' }
$newCpp = [regex]::Replace($newCpp, '(kReleaseDate\[\]\s*=\s*L")[^"]+(")',
    { param($m) $m.Groups[1].Value + $Date + $m.Groups[2].Value })
Note 'src/panel.cpp' 'kReleaseDate' $dateBefore $Date

# --- res/app.manifest: only the app's own assemblyIdentity (anchored on name=) ---
$manText = Read-Text $manPath
$newMan = [regex]::Replace($manText,
    "version=`"$curEsc\.0`"(\s+name=`"MultiMonitorPanel\.App`")",
    { param($m) "version=`"$Version.0`"" + $m.Groups[1].Value })
if ($newMan -eq $manText) {
    Write-Warning "app.manifest: version=`"$cur.0`" name=`"MultiMonitorPanel.App`" not found (already bumped?)"
} else {
    Note 'res/app.manifest' 'assemblyIdentity' "$cur.0" "$Version.0"
}

if (-not $DryRun) {
    Save-Text $cppPath $newCpp
    Save-Text $manPath $newMan
}

# --- report ---
$tag = if ($DryRun) { '[dry-run] ' } else { '' }
Write-Host ""
Write-Host "${tag}MultiMonitorPanel version bump: $cur -> $Version"
Write-Host ""
Write-Host "Changed ($($changed.Count)):"
foreach ($c in $changed) {
    Write-Host ("  {0,-18} {1,-14} {2} -> {3}" -f $c.File, $c.What, $c.Before, $c.After)
}

# leftover scan across tracked text files — the old version should now survive
# ONLY in release history (RELEASE.md etc). Scan the POST-edit content (even under
# -DryRun, where nothing was written to disk) so the report reflects the result,
# not the pre-bump file — otherwise the source-of-truth spots show up as leftovers.
$updated = @{ 'src/panel.cpp' = $newCpp; 'res/app.manifest' = $newMan }
$leftover = New-Object System.Collections.Generic.List[object]
$tracked = (git -C $root ls-files) -split "`n" | Where-Object { $_ }
foreach ($rel in $tracked) {
    if ($rel -notmatch '\.(md|cpp|h|hpp|manifest|rc|bat|ps1|txt|json)$') { continue }
    if ($updated.ContainsKey($rel)) {
        $lines = $updated[$rel] -split "`r?`n"
    } else {
        $abs = Join-Path $root $rel
        if (-not (Test-Path -LiteralPath $abs)) { continue }
        $lines = [System.IO.File]::ReadAllLines($abs)
    }
    for ($n = 0; $n -lt $lines.Count; $n++) {
        if ($lines[$n] -match $curEsc) { $leftover.Add([pscustomobject]@{ Loc = "${rel}:$($n + 1)"; Text = $lines[$n].Trim() }) }
    }
}
Write-Host ""
Write-Host "Old version `"$cur`" still present in tracked files ($($leftover.Count)) - should be release history, verify:"
if ($leftover.Count -eq 0) {
    Write-Host "  (none)"
} else {
    foreach ($l in $leftover) {
        $t = if ($l.Text.Length -gt 90) { $l.Text.Substring(0, 87) + '...' } else { $l.Text }
        Write-Host ("  {0,-22} {1}" -f $l.Loc, $t)
    }
}
Write-Host ""
Write-Host "Still to do by hand: rebuild (build.bat) and write the GitHub release notes (RU + EN)."
if ($DryRun) { Write-Host "(dry run - nothing written)" }
Write-Host ""

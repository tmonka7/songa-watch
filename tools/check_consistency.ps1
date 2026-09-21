<#
    Static consistency checks that do not need a compiler.

    ESP-IDF is not always installed on the machine where this code is
    edited, so these catch the mistakes that a build would otherwise catch
    much later: a screen declared but never defined, an i18n id that does
    not exist, a CONFIG_ symbol no Kconfig defines, and unbalanced braces.

        pwsh -File tools/check_consistency.ps1
#>
[CmdletBinding()]
param([string]$Root)

# $PSScriptRoot is empty when the script is launched by a relative path under
# Windows PowerShell 5.1, so fall back to the invocation path before giving up.
if ([string]::IsNullOrEmpty($Root)) {
    $here = $PSScriptRoot
    if ([string]::IsNullOrEmpty($here)) {
        $here = Split-Path -Parent ([System.IO.Path]::GetFullPath($MyInvocation.MyCommand.Path))
    }
    $Root = Split-Path -Parent $here
}

$ErrorActionPreference = 'Stop'
$problems = @()

function Add-Problem($category, $detail) {
    $script:problems += [pscustomobject]@{ Check = $category; Detail = $detail }
}

$ownSources = Get-ChildItem -Path (Join-Path $Root 'components/watch_hal'),
                                  (Join-Path $Root 'components/watch_svc'),
                                  (Join-Path $Root 'components/watch_ui'),
                                  (Join-Path $Root 'main') `
                            -Recurse -Include *.c, *.h -ErrorAction SilentlyContinue

Write-Host "scanning $($ownSources.Count) source files"

# ---------------------------------------------------------------- screens
$screensHeader = Get-Content -LiteralPath (Join-Path $Root 'components/watch_ui/include/watch_ui/ui_screens.h') -Raw
$declared = [regex]::Matches($screensHeader, 'lv_obj_t \*(scr_\w+_create)\(void\);') |
            ForEach-Object { $_.Groups[1].Value }

$defined = @()
foreach ($f in ($ownSources | Where-Object { $_.Extension -eq '.c' })) {
    $src = Get-Content -LiteralPath $f.FullName -Raw
    $defined += [regex]::Matches($src, '(?m)^lv_obj_t \*(scr_\w+_create)\(void\)\s*$') |
                ForEach-Object { $_.Groups[1].Value }
}

foreach ($d in $declared) {
    if ($defined -notcontains $d) { Add-Problem 'screen-missing' $d }
}
foreach ($d in $defined) {
    if ($declared -notcontains $d) { Add-Problem 'screen-undeclared' $d }
}
$dupes = $defined | Group-Object | Where-Object { $_.Count -gt 1 }
foreach ($g in $dupes) { Add-Problem 'screen-duplicate' $g.Name }
Write-Host "screens declared: $($declared.Count), defined: $($defined.Count)"

# ---------------------------------------------------------------- i18n ids
$i18nHeader = Get-Content -LiteralPath (Join-Path $Root 'components/watch_svc/include/watch_svc/svc_i18n.h') -Raw
if ($i18nHeader -match '(?s)typedef enum \{(.*?)\} i18n_id_t;') {
    $enumBody = $Matches[1]
    $validIds = [regex]::Matches($enumBody, '\b(STR_[A-Z0-9_]+)\b') |
                ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique

    $usedIds = @()
    foreach ($f in $ownSources) {
        $src = Get-Content -LiteralPath $f.FullName -Raw
        $usedIds += [regex]::Matches($src, '\b(STR_[A-Z0-9_]+)\b') |
                    ForEach-Object { $_.Groups[1].Value }
    }
    foreach ($u in ($usedIds | Sort-Object -Unique)) {
        if ($validIds -notcontains $u) { Add-Problem 'i18n-unknown-id' $u }
    }
    Write-Host "i18n ids defined: $($validIds.Count)"

    # Every id must appear in both language tables.
    $table = Get-Content -LiteralPath (Join-Path $Root 'components/watch_svc/svc_i18n.c') -Raw
    foreach ($tbl in @('s_en', 's_jp')) {
        if ($table -match "(?s)$tbl\[STR_COUNT\]\s*=\s*\{(.*?)\n\};") {
            $body = $Matches[1]
            $present = [regex]::Matches($body, '\[(STR_[A-Z0-9_]+)\]') |
                       ForEach-Object { $_.Groups[1].Value }
            foreach ($id in $validIds) {
                if ($id -eq 'STR_COUNT') { continue }
                if ($present -notcontains $id) { Add-Problem "i18n-missing-$tbl" $id }
            }
        } else {
            Add-Problem 'i18n-table' "could not parse $tbl"
        }
    }
} else {
    Add-Problem 'i18n-enum' 'could not parse i18n_id_t'
}

# ------------------------------------------------------------ CONFIG_ syms
$kconfigDefined = @()
foreach ($k in (Get-ChildItem -Path (Join-Path $Root 'components') -Recurse -Include Kconfig, Kconfig.projbuild -ErrorAction SilentlyContinue)) {
    $kconfigDefined += [regex]::Matches((Get-Content -LiteralPath $k.FullName -Raw),
                                        '(?m)^\s*config\s+([A-Z0-9_]+)') |
                       ForEach-Object { $_.Groups[1].Value }
}
# ESP-IDF's own symbols are not visible here, so only check the ones this
# project is responsible for defining.
$ourPrefixes = @('WATCH_', 'BSP_')
foreach ($f in $ownSources) {
    $src = Get-Content -LiteralPath $f.FullName -Raw
    foreach ($m in [regex]::Matches($src, 'CONFIG_([A-Z0-9_]+)')) {
        $sym = $m.Groups[1].Value
        if (($ourPrefixes | Where-Object { $sym.StartsWith($_) }) -and
            ($kconfigDefined -notcontains $sym)) {
            Add-Problem 'kconfig-unknown' "$sym (in $($f.Name))"
        }
    }
}
Write-Host "kconfig symbols defined by this project: $($kconfigDefined.Count)"

# ---------------------------------------------------------------- braces
foreach ($f in ($ownSources | Where-Object { $_.Extension -eq '.c' })) {
    $src = Get-Content -LiteralPath $f.FullName -Raw
    # Strip strings, chars and comments so braces inside them do not count.
    $clean = $src -replace '(?s)/\*.*?\*/', '' `
                  -replace '(?m)//.*$', '' `
                  -replace '"(\\.|[^"\\])*"', '""' `
                  -replace "'(\\.|[^'\\])*'", "''"
    $open = ([regex]::Matches($clean, '\{')).Count
    $close = ([regex]::Matches($clean, '\}')).Count
    if ($open -ne $close) {
        Add-Problem 'braces' "$($f.Name): $open open vs $close close"
    }
}

# ---------------------------------------------------------------- report
Write-Host ""
if ($problems.Count -eq 0) {
    Write-Host "OK - no consistency problems found." -ForegroundColor Green
    exit 0
}
Write-Host "$($problems.Count) problem(s):" -ForegroundColor Red
$problems | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
exit 1

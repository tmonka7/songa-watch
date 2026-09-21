<#
    Verify every Japanese UI string against the glyph set of the LVGL font
    that renders it.

    LVGL ships lv_font_source_han_sans_sc_16_cjk as a fixed subset - about
    1373 glyphs - not a complete CJK face. A character outside that subset
    draws as nothing on the device, and the build gives no warning, so this
    check is the only thing standing between a typo and a blank label on the
    hardware.

    Run it after touching any Japanese text in svc_i18n.c:

        pwsh -File tools/check_i18n_font.ps1

    Exit code 0 = every character is covered.
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$fontFile = Join-Path $Root 'components/lvgl/src/font/lv_font_source_han_sans_sc_16_cjk.c'
$i18nFile = Join-Path $Root 'components/watch_svc/svc_i18n.c'

foreach ($f in @($fontFile, $i18nFile)) {
    if (-not (Test-Path -LiteralPath $f)) {
        Write-Error "missing: $f"
    }
}

# The generator records its full --symbols list in the file header, which is
# the authoritative glyph set. Parsing that beats decoding the cmap tables.
$header = (Get-Content -LiteralPath $fontFile -TotalCount 8 -Encoding UTF8) -join "`n"
if ($header -notmatch '--symbols\s(.*?)\s--font') {
    Write-Error "could not read the --symbols list from $fontFile"
}
$symbols = $Matches[1]

$covered = [System.Collections.Generic.HashSet[char]]::new()
foreach ($c in $symbols.ToCharArray()) { [void]$covered.Add($c) }
# The font also carries printable ASCII (-r 0x20-0x7f).
foreach ($i in 0x20..0x7E) { [void]$covered.Add([char]$i) }

Write-Host "font glyphs available: $($covered.Count)"

# Scan every string literal in the firmware's own sources, not just the
# translation table - weekday abbreviations, units and inline labels are
# just as capable of reaching for a glyph the font does not carry.
$sources = Get-ChildItem -Path (Join-Path $Root 'components/watch_svc'),
                               (Join-Path $Root 'components/watch_ui'),
                               (Join-Path $Root 'main') `
                         -Recurse -Include *.c, *.h -ErrorAction SilentlyContinue

$problems = @()
$scanned = 0
foreach ($file in $sources) {
    $src = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    if ([string]::IsNullOrEmpty($src)) { continue }

    foreach ($m in [regex]::Matches($src, '"((?:[^"\\\r\n]|\\.)*)"')) {
        $val = $m.Groups[1].Value
        # Only literals with characters outside ASCII can be a problem.
        if ($val -cmatch '^[\x00-\x7F]*$') { continue }
        $scanned++

        $missing = [System.Collections.Generic.HashSet[char]]::new()
        foreach ($c in $val.ToCharArray()) {
            if (-not $covered.Contains($c)) { [void]$missing.Add($c) }
        }
        if ($missing.Count -gt 0) {
            # Report the line so the fix is one jump away.
            $line = ($src.Substring(0, $m.Index) -split "`n").Count
            $problems += [pscustomobject]@{
                Where   = "{0}:{1}" -f $file.Name, $line
                Text    = $val
                Missing = ($missing | ForEach-Object { "$_ (U+{0:X4})" -f [int]$_ }) -join ', '
            }
        }
    }
}
Write-Host "non-ASCII string literals checked: $scanned"
if ($scanned -eq 0) { Write-Error "no non-ASCII literals found - the scan is not seeing the sources" }

if ($problems.Count -eq 0) {
    Write-Host ""
    Write-Host "OK - every non-ASCII character used is in the font." -ForegroundColor Green
    exit 0
}

Write-Host ""
Write-Host "$($problems.Count) string(s) use characters the font does not have:" -ForegroundColor Red
$problems | Format-Table -AutoSize -Wrap | Out-String -Width 200 | Write-Host
Write-Host "Reword these, or regenerate the font with lv_font_conv and widen its --symbols list." -ForegroundColor Yellow
exit 1

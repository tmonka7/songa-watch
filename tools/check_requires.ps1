<#
    Check that every component declares the components it #includes from.

    With the IDF component manager disabled, nothing derives dependencies from
    a manifest: whatever a component includes must appear in its own REQUIRES
    or PRIV_REQUIRES. Missing entries configure cleanly and only fail deep into
    compilation with "No such file or directory", one header at a time.

        pwsh -File tools/check_requires.ps1

    Vendored and project components are always checked. If IDF_PATH is set,
    ESP-IDF's own components are checked too - without it, headers that IDF
    provides are skipped rather than guessed at.
#>
[CmdletBinding()]
param(
    [string]$Root,
    [string]$IdfPath = $env:IDF_PATH
)

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

# Components every component gets for free (COMPONENT_REQUIRES_COMMON).
$common = @(
    'cxx', 'newlib', 'freertos', 'esp_hw_support', 'heap', 'log', 'soc', 'hal',
    'esp_rom', 'esp_common', 'esp_system', 'xtensa', 'riscv'
)

function Get-ComponentDirs($base) {
    if (-not (Test-Path $base)) { return @() }
    Get-ChildItem -Path $base -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'CMakeLists.txt') }
}

$projectDirs = @(Get-ComponentDirs (Join-Path $Root 'components'))
$mainDir = Get-Item (Join-Path $Root 'main') -ErrorAction SilentlyContinue
$idfDirs = @()
if ($IdfPath -and (Test-Path $IdfPath)) {
    $idfDirs = @(Get-ComponentDirs (Join-Path $IdfPath 'components'))
} else {
    Write-Host 'IDF_PATH not set - ESP-IDF headers will be skipped, not verified'
}

# header path -> owning component. Ambiguous basenames are dropped rather than
# guessed: lvgl and thorvg both ship a bare config.h.
$owner = @{}
$ambiguous = New-Object System.Collections.Generic.HashSet[string]

foreach ($d in ($projectDirs + $idfDirs)) {
    $incDirs = Get-ChildItem -Path $d.FullName -Directory -Recurse -Filter 'include' `
                             -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notmatch '(test_apps|examples)' }
    foreach ($inc in $incDirs) {
        foreach ($h in (Get-ChildItem -Path $inc.FullName -Recurse -Filter '*.h' -ErrorAction SilentlyContinue)) {
            $rel = $h.FullName.Substring($inc.FullName.Length + 1).Replace([char]92, '/')
            # A header in a subdirectory (bsp/config.h) can only be included by
            # its full relative path, so do not register its bare basename -
            # that would claim every unrelated config.h in the tree.
            foreach ($key in @($rel)) {
                if ($owner.ContainsKey($key) -and $owner[$key] -ne $d.Name) {
                    [void]$ambiguous.Add($key)
                } else {
                    $owner[$key] = $d.Name
                }
            }
        }
    }
}

$problems = @()
$checked = 0

foreach ($d in (@($projectDirs) + @($mainDir | Where-Object { $_ }))) {
    $cml = Join-Path $d.FullName 'CMakeLists.txt'
    if (-not (Test-Path $cml)) { continue }

    # Punctuation to spaces so "esp_io_expander") still matches as a word.
    $declared = ((Get-Content $cml -Raw) -replace '[^A-Za-z0-9_]', ' ') -split '\s+'

    $sources = Get-ChildItem -Path $d.FullName -Recurse -Include *.c, *.h -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notmatch '(test_apps|examples)' }

    $seen = @{}
    foreach ($src in $sources) {
        $checked++
        foreach ($line in (Get-Content $src.FullName -ErrorAction SilentlyContinue)) {
            if ($line -notmatch '^\s*#\s*include\s+"([^"]+)"') { continue }
            $inc = $Matches[1]
            if ($ambiguous.Contains($inc)) { continue }
            $o = $owner[$inc]
            if (-not $o -or $o -eq $d.Name) { continue }
            if ($common -contains $o) { continue }
            if ($declared -contains $o) { continue }
            $key = "$($d.Name)|$o|$inc"
            if ($seen.ContainsKey($key)) { continue }
            $seen[$key] = $true
            $problems += [pscustomobject]@{
                Component = $d.Name
                Needs     = $o
                Because   = $inc
                Where     = $src.FullName.Substring($Root.Length + 1).Replace([char]92, '/')
            }
        }
    }
}

Write-Host "components mapped: $($projectDirs.Count + $idfDirs.Count)"
Write-Host "source files checked: $checked"
Write-Host ''

if ($problems.Count -eq 0) {
    Write-Host 'OK - every included header is covered by a declared dependency.'
    exit 0
}

foreach ($p in $problems) {
    Write-Host ("{0}: add '{1}' to REQUIRES - {2} includes {3}" -f `
        $p.Component, $p.Needs, $p.Where, $p.Because)
}
Write-Host ''
Write-Host "$($problems.Count) missing dependency declaration(s)."
exit 1

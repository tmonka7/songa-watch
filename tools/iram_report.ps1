<#
    Report what is occupying IRAM, by object file.

    IRAM is hard-limited: the exception vectors reach their handlers with a
    `j` instruction that encodes +/-128 KB, so an oversized .iram0.text fails
    at link with "dangerous relocation: j: cannot encode: xt_debugexception",
    naming a file in xtensa/ and telling you nothing about the cause.

        pwsh -File tools/iram_report.ps1

    GNU ld writes the .map even when the link fails, so this works on exactly
    the build that could not be linked - which is when you need it.
#>
[CmdletBinding()]
param(
    [string]$Root,
    [string]$Map,
    [int]$Top = 25
)

if ([string]::IsNullOrEmpty($Root)) {
    $here = $PSScriptRoot
    if ([string]::IsNullOrEmpty($here)) {
        $here = Split-Path -Parent ([System.IO.Path]::GetFullPath($MyInvocation.MyCommand.Path))
    }
    $Root = Split-Path -Parent $here
}
if ([string]::IsNullOrEmpty($Map)) {
    $Map = Join-Path $Root 'build/songa_watch.map'
}

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Map)) {
    Write-Host "no map file at $Map"
    Write-Host 'Build at least as far as the link step, then run this again.'
    exit 2
}

$lines = Get-Content $Map
$byFile = @{}
$total = 0
$pending = $null

# Two shapes appear in a GNU ld map: everything on one line, or a long section
# name on its own line with the address, size and object on the next.
$combined = '^\s(\.iram0\.[^\s]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$'
$nameOnly = '^\s(\.iram0\.[^\s]+)\s*$'
$detail   = '^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$'

foreach ($line in $lines) {
    if ($pending -ne $null) {
        if ($line -match $detail) {
            $size = [Convert]::ToInt64($Matches[2], 16)
            $file = $Matches[3]
            $total += $size
            $byFile[$file] = [int64]$byFile[$file] + $size
        }
        $pending = $null
        continue
    }
    if ($line -match $combined) {
        $size = [Convert]::ToInt64($Matches[3], 16)
        $file = $Matches[4]
        $total += $size
        $byFile[$file] = [int64]$byFile[$file] + $size
        continue
    }
    if ($line -match $nameOnly) { $pending = $Matches[1] }
}

if ($total -eq 0) {
    Write-Host 'no .iram0 sections found - is this the right map file?'
    exit 2
}

$limit = 128 * 1024
Write-Host ("IRAM (.iram0.*) total: {0:N0} bytes of the {1:N0} byte jump range" -f $total, $limit)
if ($total -ge $limit) {
    Write-Host ("OVER by {0:N0} bytes - the link will fail" -f ($total - $limit))
} else {
    Write-Host ("{0:N0} bytes of headroom" -f ($limit - $total))
}
Write-Host ''
Write-Host "largest $Top contributors:"

$byFile.GetEnumerator() |
    Sort-Object -Property Value -Descending |
    Select-Object -First $Top |
    ForEach-Object {
        $name = $_.Key
        if ($name.Length -gt 62) { $name = '...' + $name.Substring($name.Length - 59) }
        Write-Host ("{0,9:N0}  {1}" -f $_.Value, $name)
    }

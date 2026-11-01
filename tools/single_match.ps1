<#
.SYNOPSIS
    Boots the WCW vs. nWo: World Tour recomp straight into a CPU-vs-CPU match.

.DESCRIPTION
    Uses the prebuilt recomp in _tools\WCWRecompiled (portable install) and its
    bundled Quick Match mod. Writes the mod's config (Enabled, Venue, Match),
    makes sure the mod is enabled, then launches with WCW_AUTOBOOT pointing at
    the ROM in _roms\ so the launcher menu is skipped. The game walks its own
    menus (title, Exhibition, CPU vs CPU) and the bell rings roughly a minute
    after boot.

.EXAMPLE
    .\tools\single_match.ps1
    .\tools\single_match.ps1 -Venue NWO
    .\tools\single_match.ps1 -Match "Game's rotation"
    .\tools\single_match.ps1 -Off     # turn Quick Match off and start normally
#>
param(
    [ValidateSet("Game's pick", 'WCW', 'NWO', 'DOA', 'IU')]
    [string]$Venue = "Game's pick",

    [ValidateSet("Game's rotation", 'Single Match')]
    [string]$Match = 'Single Match',

    [string]$Rom,

    [switch]$Off
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$install = Join-Path $repo '_tools\WCWRecompiled'
$exe = Join-Path $install 'WCWRecompiled.exe'

if (-not (Test-Path $exe)) {
    throw "WCWRecompiled.exe not found in $install. Extract WCWRecompiled-v0.2.0-Windows.zip from https://github.com/jessetbh/WCWvsNWOWorldTourRecomp/releases there."
}
if (-not $Rom) {
    $Rom = Join-Path $repo '_roms\WCW vs. nWo - World Tour (USA).z64'
}
if (-not (Test-Path $Rom)) {
    throw "ROM not found: $Rom (pass -Rom <path to the US World Tour .z64>)"
}

# Keep config, saves and mods inside the install folder instead of %LOCALAPPDATA%.
$portable = Join-Path $install 'portable.txt'
if (-not (Test-Path $portable)) { New-Item -ItemType File $portable | Out-Null }

# Read the shipped mod's version so the config file matches it.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$nrm = Join-Path $install 'assets\mods\aki_quickmatch.nrm'
$zip = [System.IO.Compression.ZipFile]::OpenRead($nrm)
try {
    $reader = New-Object System.IO.StreamReader($zip.GetEntry('mod.json').Open())
    $manifest = $reader.ReadToEnd() | ConvertFrom-Json
    $reader.Close()
} finally {
    $zip.Dispose()
}

# Mod option values: mod_config\<mod id>.json, enum values stored by their option text.
$configDir = Join-Path $install 'mod_config'
New-Item -ItemType Directory -Force $configDir | Out-Null
$modConfig = [ordered]@{
    mod_id         = 'aki_quickmatch'
    mod_version    = $manifest.version
    recomp_version = '0.2.0'
    storage        = [ordered]@{
        enabled = -not $Off
        venue   = $Venue
        match   = $Match
    }
}
$modConfig | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $configDir 'aki_quickmatch.json') -Encoding ascii

# Make sure the mod is on in mods.json (the first run creates it with all bundled mods enabled).
$modsPath = Join-Path $install 'mods.json'
if (Test-Path $modsPath) {
    $mods = Get-Content $modsPath -Raw | ConvertFrom-Json
    if ($mods.enabled_mods -notcontains 'aki_quickmatch') {
        $mods.enabled_mods = @($mods.enabled_mods) + 'aki_quickmatch'
        if ($mods.mod_order -notcontains 'aki_quickmatch') { $mods.mod_order = @($mods.mod_order) + 'aki_quickmatch' }
        $mods | ConvertTo-Json -Depth 4 | Set-Content $modsPath -Encoding ascii
    }
}

$env:WCW_AUTOBOOT = (Resolve-Path $Rom).Path
if ($Off) {
    Write-Host 'Quick Match off; booting normally.'
} else {
    Write-Host "Quick Match: $Match, venue $Venue. The match starts about a minute after boot."
}
Write-Host "Log: $(Join-Path $install 'WCWRecompiled.log')"
Start-Process $exe -WorkingDirectory $install

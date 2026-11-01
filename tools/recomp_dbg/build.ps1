<#
.SYNOPSIS
    Builds the aki_debug mod (aki_debug.nrm + aki_debug.dll) and installs it into the
    portable WCWRecompiled in _tools\WCWRecompiled (or, with -Game revenge, the portable
    RevengeRecompiled in _tools\RevengeRecompiled).

.DESCRIPTION
    Needs the toolchain in _tools\dbg_toolchain (see README.md): zig 0.13 (MIPS compiler and
    lld), N64Recomp's RecompModTool, and the recomp's WCWSyms\dump.toml. The DLL is built
    with MSVC (Visual Studio 2022) through CMake and Ninja.

.EXAMPLE
    .\tools\recomp_dbg\build.ps1
    .\tools\recomp_dbg\build.ps1 -NoInstall
    .\tools\recomp_dbg\build.ps1 -Game revenge
#>
param([switch]$NoInstall, [ValidateSet('wcw', 'revenge')][string]$Game = 'wcw')

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = Resolve-Path "$here\..\.."
$tc = "$repo\_tools\dbg_toolchain"
$out = "$repo\_tools\dbg_build\$Game"
$zig = "$tc\zig-windows-x86_64-0.13.0\zig.exe"
$modTool = "$tc\N64Recomp\build\RecompModTool.exe"
if ($Game -eq 'revenge') {
    $syms = "$repo\_recomps\WCWnWoRevengeRecomp\syms\dump.toml"
    $gameId = 'wcwnworevenge'; $gameDef = 1; $exeDir = "$repo\_tools\RevengeRecompiled"
} else {
    $syms = "$repo\_recomps\WCWvsNWOWorldTourRecomp\WCWSyms\dump.toml"
    $gameId = 'wcwnwoworldtour'; $gameDef = 0; $exeDir = "$repo\_tools\WCWRecompiled"
}
$include = "$tc\MMRecompModTemplate\include"
$vs = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
foreach ($p in $zig, $modTool, $syms, $include) { if (-not (Test-Path $p)) { throw "missing $p (see tools\recomp_dbg\README.md)" } }
New-Item -ItemType Directory -Force $out | Out-Null

# Mod: MIPS object -> elf -> .nrm. +nooddspreg: the mod recompiler mishandles odd single FPRs.
& $zig cc -target mips-freestanding-none -mcpu=mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg `
    -Xclang -target-feature -Xclang +nooddspreg -fomit-frame-pointer -fno-builtin -nostdinc -ffreestanding `
    -ffunction-sections -Wall "-DAKI_GAME_REVENGE=$gameDef" -I $include -c "$here\mod\aki_debug.c" -o "$out\aki_debug.o"
if ($LASTEXITCODE) { throw 'compiling the mod failed' }
& $zig ld.lld "$out\aki_debug.o" -nostdlib -T "$tc\AkiMods\common\mod.ld" --unresolved-symbols=ignore-all `
    --emit-relocs -e 0 --no-nmagic -gc-sections -o "$out\mod.elf"
if ($LASTEXITCODE) { throw 'linking the mod failed' }
(Get-Content "$here\mod\mod.toml" -Raw).Replace('SYMS_PLACEHOLDER', "$syms".Replace([char]92, [char]47)).Replace('GAME_ID_PLACEHOLDER', $gameId) |
    Set-Content "$out\mod.toml" -Encoding ascii
Push-Location $out
try { & $modTool mod.toml . ; if ($LASTEXITCODE) { throw 'RecompModTool failed' } } finally { Pop-Location }

# Native library.
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$bat = "$out\build_dll.bat"
@"
@call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
"$cmake" -S "$here\native" -B "$out\native" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAKI_GAME_REVENGE=$gameDef || exit /b 1
"$cmake" --build "$out\native" || exit /b 1
"@ | Set-Content $bat -Encoding ascii
cmd /c $bat
if ($LASTEXITCODE) { throw 'building aki_debug.dll failed' }

if (-not $NoInstall) {
    $mods = "$exeDir\mods"
    New-Item -ItemType Directory -Force $mods | Out-Null
    Copy-Item "$out\aki_debug.nrm", "$out\native\aki_debug.dll" $mods -Force
    Write-Host "Installed aki_debug.nrm and aki_debug.dll into $mods"
}

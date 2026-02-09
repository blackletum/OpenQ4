param(
    [string]$GameLibsRepo = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openQ4Root = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))

if ([string]::IsNullOrWhiteSpace($GameLibsRepo)) {
    $GameLibsRepo = Join-Path $openQ4Root "..\OpenQ4-GameLibs"
}

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
$gameLibsGameDir = Join-Path $gameLibsRoot "source\game"
$openQ4GameDir = Join-Path $openQ4Root "src\game"
$excludedGameLibsCallbacks = Join-Path $gameLibsGameDir "gamesys\Callbacks.cpp"

if (-not (Test-Path $gameLibsRoot)) {
    throw "OpenQ4-GameLibs repository not found at '$gameLibsRoot'. Set OPENQ4_GAMELIBS_REPO or pass -GameLibsRepo."
}

if (-not (Test-Path $gameLibsGameDir)) {
    throw "GameLibs source path not found: '$gameLibsGameDir'."
}

if (-not (Test-Path $openQ4GameDir)) {
    throw "OpenQ4 game source path not found: '$openQ4GameDir'."
}

Write-Host "Syncing GameLibs game sources:"
Write-Host "  From: $gameLibsGameDir"
Write-Host "    To: $openQ4GameDir"

& robocopy $gameLibsGameDir $openQ4GameDir /MIR /R:2 /W:1 /NFL /NDL /NP /NJH /NJS /NC /NS /XF $excludedGameLibsCallbacks
$robocopyExit = [int]$LASTEXITCODE
if ($robocopyExit -ge 8) {
    throw "robocopy failed while syncing GameLibs (exit code $robocopyExit)."
}

if ($robocopyExit -eq 0) {
    Write-Host "GameLibs sync complete (no changes)."
} else {
    Write-Host "GameLibs sync complete (changes applied)."
}

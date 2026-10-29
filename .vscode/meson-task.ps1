param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('setup', 'compile', 'install', 'fastbuild')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$mesonWrapper = Join-Path $workspaceRoot 'tools\build\meson_setup.ps1'
$buildDir = Join-Path $workspaceRoot 'builddir-perf'
$installDir = Join-Path $workspaceRoot '.install'
$fastStageScript = Join-Path $workspaceRoot 'tools\build\stage_fast_install.py'
$checkStagedContentScript = Join-Path $workspaceRoot 'tools\build\check_staged_content_edits.py'

if (-not (Test-Path $mesonWrapper)) {
    throw "openQ4 Meson wrapper not found at '$mesonWrapper'."
}

function Invoke-openQ4Meson([string[]]$MesonArgs) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $mesonWrapper @MesonArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Invoke-PythonTool([string[]]$ToolArgs) {
    & python @ToolArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Test-MesonBuildDirectory([string]$Path) {
    $coreData = Join-Path $Path 'meson-private\coredata.dat'
    $ninjaFile = Join-Path $Path 'build.ninja'
    return (Test-Path $coreData) -and (Test-Path $ninjaFile)
}

# Match the release packages: optimized code with symbols, and C assert() compiled
# out (b_ndebug), including in the SDL3, GLEW and stb_vorbis subprojects.
$expectedBuildOptions = [ordered]@{
    'buildtype' = 'debugoptimized'
    'b_ndebug' = 'true'
}

function Get-StaleBuildOptions([string]$Path) {
    $optionsFile = Join-Path $Path 'meson-info\intro-buildoptions.json'
    if (-not (Test-Path $optionsFile)) {
        return @($expectedBuildOptions.Keys)
    }
    $configured = @{}
    foreach ($option in (Get-Content $optionsFile -Raw | ConvertFrom-Json)) {
        $configured[$option.name] = "$($option.value)".ToLowerInvariant()
    }
    return @($expectedBuildOptions.Keys | Where-Object { $configured[$_] -ne $expectedBuildOptions[$_] })
}

function Invoke-openQ4Setup {
    $optionArgs = @('--backend', 'ninja', '--wrap-mode=forcefallback')
    foreach ($name in $expectedBuildOptions.Keys) {
        $optionArgs += "-D$name=$($expectedBuildOptions[$name])"
    }
    if (Test-MesonBuildDirectory $buildDir) {
        Invoke-openQ4Meson (@('setup', $buildDir, $workspaceRoot, '--reconfigure') + $optionArgs)
    } else {
        Invoke-openQ4Meson (@('setup', '--wipe', $buildDir, $workspaceRoot) + $optionArgs)
    }
}

# An existing build directory keeps whatever it was configured with, and the
# compile tasks never reconfigure on their own, so bring it up to the expected
# options before compiling.
function Invoke-openQ4Compile {
    $stale = @(Get-StaleBuildOptions $buildDir)
    if ($stale.Count -gt 0) {
        Write-Host "builddir-perf options out of date ($($stale -join ', ')); reconfiguring."
        Invoke-openQ4Setup
    }
    Invoke-openQ4Meson @(
        'compile',
        '-C',
        $buildDir
    )
}

switch ($Action) {
    'setup' {
        Invoke-openQ4Setup
    }
    'compile' {
        Invoke-openQ4Compile
    }
    'fastbuild' {
        if (-not (Test-Path $fastStageScript)) {
            throw "openQ4 fast staging script not found at '$fastStageScript'."
        }
        if (-not (Test-Path $checkStagedContentScript)) {
            throw "openQ4 staged content edit check script not found at '$checkStagedContentScript'."
        }

        Invoke-openQ4Compile
        Invoke-PythonTool @(
            $checkStagedContentScript,
            '--source-root',
            $workspaceRoot
        )
        Invoke-PythonTool @(
            $fastStageScript,
            '--source-root',
            $workspaceRoot,
            '--build-dir',
            $buildDir,
            '--install-dir',
            $installDir
        )
    }
    'install' {
        Invoke-openQ4Meson @(
            'install',
            '-C',
            $buildDir,
            '--no-rebuild',
            '--skip-subprojects'
        )
    }
}

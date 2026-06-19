[CmdletBinding()]
param(
    [ValidateSet("Probe", "Bootstrap", "Assets", "Sync", "Build", "Smoke", "Renderer", "Launcher", "All")]
    [string[]]$Action = @("All"),
    [Parameter(Mandatory = $true)]
    [string]$MacHost,
    [string]$MacUser = "codex",
    [int]$SshPort = 22,
    [string]$IdentityFile,
    [string]$MacWorkspace = "~/openq4-work",
    [string]$MacBasePath = "~/openq4-work/Quake4",
    [string]$HostQuake4Path = "C:\Program Files (x86)\Steam\steamapps\common\Quake 4",
    [string]$HostGameLibsPath,
    [string]$TmpRoot,
    [ValidateSet("opengl", "metal")]
    [string]$MacOSGraphicsBridge = "opengl",
    [string]$BuildType = "debug",
    [string]$BuildDir = "builddir",
    [int]$SmokeLimit = 1,
    [switch]$SkipAssets,
    [switch]$SkipSync
)

$ErrorActionPreference = "Stop"
$script:RemoteTempRoot = ""

function Get-RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).ProviderPath
}

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Quote-Sh {
    param([AllowNull()][string]$Value)
    if ($null -eq $Value) {
        $Value = ""
    }
    return "'" + $Value.Replace("'", "'\''") + "'"
}

function Get-SshArgs {
    $args = @("-p", [string]$SshPort, "-o", "BatchMode=yes")
    if ($IdentityFile) {
        $args += @("-i", (Get-FullPath $IdentityFile))
    }
    return $args
}

function Get-ScpArgs {
    $args = @("-P", [string]$SshPort, "-o", "BatchMode=yes")
    if ($IdentityFile) {
        $args += @("-i", (Get-FullPath $IdentityFile))
    }
    return $args
}

function Invoke-MacSsh {
    param([Parameter(Mandatory = $true)][string]$Command)
    $target = "${MacUser}@${MacHost}"
    & ssh @(Get-SshArgs) $target $Command
    if ($LASTEXITCODE -ne 0) {
        throw "ssh failed with exit code ${LASTEXITCODE}: $Command"
    }
}

function Copy-ToMac {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$RemotePath
    )
    $target = "${MacUser}@${MacHost}:$RemotePath"
    & scp @(Get-ScpArgs) $Source $target
    if ($LASTEXITCODE -ne 0) {
        throw "scp failed with exit code ${LASTEXITCODE}: $Source -> $RemotePath"
    }
}

function Assert-NoArchiveLinks {
    param([Parameter(Mandatory = $true)][string]$SourceDir)

    $sourceFullPath = Get-FullPath $SourceDir
    $badEntries = @()
    Get-ChildItem -LiteralPath $sourceFullPath -Recurse -Force | ForEach-Object {
        if (($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            $badEntries += $_.FullName
        }
    }

    if ($badEntries.Count -ne 0) {
        $sample = ($badEntries | Select-Object -First 10) -join "`n  - "
        throw "Refusing to archive symlink/junction/reparse-point entries from ${sourceFullPath}:`n  - $sample"
    }
}

function New-TransferArchive {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [string[]]$Exclude = @()
    )

    if (-not (Test-Path -LiteralPath $SourceDir)) {
        throw "Source directory was not found: $SourceDir"
    }
    Assert-NoArchiveLinks -SourceDir $SourceDir

    $archiveFullPath = Get-FullPath $ArchivePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archiveFullPath) | Out-Null
    if (Test-Path -LiteralPath $archiveFullPath) {
        Remove-Item -LiteralPath $archiveFullPath -Force
    }

    $parent = Split-Path -Parent (Get-FullPath $SourceDir)
    $leaf = Split-Path -Leaf (Get-FullPath $SourceDir)
    $tarArgs = @("-C", $parent, "-cf", $archiveFullPath)
    foreach ($pattern in $Exclude) {
        $tarArgs += "--exclude=$pattern"
    }
    $tarArgs += $leaf

    & tar @tarArgs
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed with exit code $LASTEXITCODE while archiving $SourceDir"
    }

    return $archiveFullPath
}

function Expand-RemoteArchive {
    param(
        [Parameter(Mandatory = $true)][string]$RemoteArchive,
        [Parameter(Mandatory = $true)][string]$RemoteTarget
    )

    $script = @"
set -euo pipefail
archive=$(Quote-Sh $RemoteArchive)
target=$(Quote-Sh $RemoteTarget)

mkdir -p "`$target"
tmp_dir="`$(mktemp -d /tmp/openq4-extract.XXXXXX)"
trap 'rm -rf "`$tmp_dir"' EXIT

tar -tf "`$archive" | while IFS= read -r entry; do
    case "`$entry" in
        ""|/*|../*|*/../*|*\\*)
            echo "Unsafe archive path: `$entry" >&2
            exit 1
            ;;
    esac
done

tar -xf "`$archive" -C "`$tmp_dir" --strip-components 1
bad_entry="`$(find "`$tmp_dir" \( -type l -o \( ! -type f -a ! -type d \) \) -print -quit)"
if [[ -n "`$bad_entry" ]]; then
    echo "Archive contains a symlink or special file: `$bad_entry" >&2
    exit 1
fi

rsync -a --delete "`$tmp_dir/" "`$target/"
"@
    Invoke-MacSsh -Command "bash -lc $(Quote-Sh $script)"
}

function Copy-GuestScript {
    param([Parameter(Mandatory = $true)][string]$ScriptName)

    $localScript = Join-Path (Join-Path $PSScriptRoot "guest") $ScriptName
    if (-not (Test-Path -LiteralPath $localScript)) {
        throw "Guest script was not found: $localScript"
    }

    if (-not $script:RemoteTempRoot) {
        throw "Remote temporary root was not initialized."
    }

    Invoke-MacSsh -Command "umask 077 && mkdir -p $(Quote-Sh $script:RemoteTempRoot)"
    $remoteScript = "$script:RemoteTempRoot/$ScriptName"
    Copy-ToMac -Source $localScript -RemotePath $remoteScript
    Invoke-MacSsh -Command "chmod +x $(Quote-Sh $remoteScript)"
    return $remoteScript
}

function Invoke-GuestScript {
    param(
        [Parameter(Mandatory = $true)][string]$ScriptName,
        [string]$ScriptAction = "",
        [hashtable]$Environment = @{}
    )

    $remoteScript = Copy-GuestScript -ScriptName $ScriptName
    $envParts = @(
        "OPENQ4_GUEST_WORKSPACE=$(Quote-Sh $MacWorkspace)",
        "OPENQ4_BASEPATH=$(Quote-Sh $MacBasePath)"
    )
    foreach ($key in $Environment.Keys) {
        $envParts += "$key=$(Quote-Sh ([string]$Environment[$key]))"
    }

    $cmd = (@("/usr/bin/env") + $envParts + @("bash", (Quote-Sh $remoteScript))) -join " "
    if ($ScriptAction) {
        $cmd += " $(Quote-Sh $ScriptAction)"
    }
    Invoke-MacSsh -Command $cmd
}

function Sync-SourceTrees {
    $transferRoot = Join-Path $TmpRoot "transfer"
    New-Item -ItemType Directory -Force -Path $transferRoot | Out-Null

    $repoArchive = New-TransferArchive `
        -SourceDir $repoRoot `
        -ArchivePath (Join-Path $transferRoot "openQ4.tar") `
        -Exclude @(
            "openQ4/.git",
            "openQ4/.claude",
            "openQ4/.codex",
            "openQ4/.home",
            "openQ4/.install",
            "openQ4/.tmp",
            "openQ4/.vscode",
            "openQ4/.voice_eng",
            "openQ4/builddir",
            "openQ4/builddir_*",
            "openQ4/builddir-*",
            "openQ4/tmp-game-libs"
        )

    $remoteRepoArchive = "$script:RemoteTempRoot/openQ4-source.tar"
    Copy-ToMac -Source $repoArchive -RemotePath $remoteRepoArchive
    Expand-RemoteArchive -RemoteArchive $remoteRepoArchive -RemoteTarget "$MacWorkspace/openQ4"

    if (Test-Path -LiteralPath $HostGameLibsPath) {
        $gameLibsArchive = New-TransferArchive `
            -SourceDir $HostGameLibsPath `
            -ArchivePath (Join-Path $transferRoot "openQ4-GameLibs.tar") `
            -Exclude @(
                "openQ4-GameLibs/.git",
                "openQ4-GameLibs/.claude",
                "openQ4-GameLibs/.codex",
                "openQ4-GameLibs/.tmp",
                "openQ4-GameLibs/.vscode",
                "openQ4-GameLibs/builddir",
                "openQ4-GameLibs/builddir_*",
                "openQ4-GameLibs/builddir-*"
            )
        $remoteGameLibsArchive = "$script:RemoteTempRoot/openQ4-GameLibs.tar"
        Copy-ToMac -Source $gameLibsArchive -RemotePath $remoteGameLibsArchive
        Expand-RemoteArchive -RemoteArchive $remoteGameLibsArchive -RemoteTarget "$MacWorkspace/openQ4-GameLibs"
    } else {
        Write-Warning "openQ4-GameLibs was not found at $HostGameLibsPath; macOS build may fail until the companion repo is synced."
    }
}

function Install-Assets {
    if (-not (Test-Path -LiteralPath (Join-Path $HostQuake4Path "q4base"))) {
        throw "Quake 4 assets were not found at '$HostQuake4Path'. Expected q4base under that directory."
    }

    $transferRoot = Join-Path $TmpRoot "transfer"
    New-Item -ItemType Directory -Force -Path $transferRoot | Out-Null
    $assetRootName = Split-Path -Leaf (Get-FullPath $HostQuake4Path)
    $assetArchive = New-TransferArchive `
        -SourceDir $HostQuake4Path `
        -ArchivePath (Join-Path $transferRoot "Quake4-assets.tar") `
        -Exclude @(
            "$assetRootName/.tmp",
            "$assetRootName/CrashReports",
            "$assetRootName/q4base/generated",
            "$assetRootName/q4base/*.cfg",
            "$assetRootName/q4base/*.log",
            "$assetRootName/q4base/q4key",
            "$assetRootName/q4base/quake4key",
            "$assetRootName/q4base/savegames",
            "$assetRootName/q4base/screenshots",
            "$assetRootName/q4mp/*.cfg",
            "$assetRootName/q4mp/*.log",
            "$assetRootName/q4mp/screenshots"
        )

    $remoteAssetArchive = "$script:RemoteTempRoot/Quake4-assets.tar"
    Copy-ToMac -Source $assetArchive -RemotePath $remoteAssetArchive
    Invoke-GuestScript `
        -ScriptName "openq4-macos-install-quake4-assets.sh" `
        -Environment @{ "OPENQ4_Q4_TAR" = $remoteAssetArchive }
}

$repoRoot = Get-RepoRoot
if (-not $TmpRoot) {
    $TmpRoot = Join-Path (Join-Path $repoRoot ".tmp") "macos-vm"
}
$TmpRoot = Get-FullPath $TmpRoot
New-Item -ItemType Directory -Force -Path $TmpRoot | Out-Null
$script:RemoteTempRoot = "/tmp/openq4-macos-$([System.Guid]::NewGuid().ToString("N"))"
Invoke-MacSsh -Command "umask 077 && mkdir -p $(Quote-Sh $script:RemoteTempRoot)"

if (-not $HostGameLibsPath) {
    $HostGameLibsPath = Join-Path (Split-Path -Parent $repoRoot) "openQ4-GameLibs"
}
$HostGameLibsPath = Get-FullPath $HostGameLibsPath

foreach ($item in $Action) {
    switch ($item) {
        "Probe" {
            Invoke-MacSsh -Command "uname -a && sw_vers && xcode-select -p 2>/dev/null || true && command -v brew 2>/dev/null || true"
        }
        "Bootstrap" {
            Invoke-GuestScript -ScriptName "openq4-macos-bootstrap.sh"
        }
        "Assets" {
            if (-not $SkipAssets) {
                Install-Assets
            }
        }
        "Sync" {
            if (-not $SkipSync) {
                Sync-SourceTrees
            }
        }
        "Build" {
            Invoke-GuestScript `
                -ScriptName "openq4-macos-sync-build-test.sh" `
                -ScriptAction "build" `
                -Environment @{
                    "OPENQ4_MACOS_GRAPHICS_BRIDGE" = $MacOSGraphicsBridge
                    "OPENQ4_BUILDTYPE" = $BuildType
                    "OPENQ4_BUILDDIR" = $BuildDir
                }
        }
        "Smoke" {
            Invoke-GuestScript `
                -ScriptName "openq4-macos-sync-build-test.sh" `
                -ScriptAction "smoke" `
                -Environment @{ "OPENQ4_SMOKE_LIMIT" = [string]$SmokeLimit }
        }
        "Renderer" {
            Invoke-GuestScript -ScriptName "openq4-macos-sync-build-test.sh" -ScriptAction "renderer"
        }
        "Launcher" {
            Invoke-GuestScript -ScriptName "openq4-macos-sync-build-test.sh" -ScriptAction "launcher"
        }
        "All" {
            Invoke-GuestScript -ScriptName "openq4-macos-bootstrap.sh"
            if (-not $SkipAssets) {
                Install-Assets
            }
            if (-not $SkipSync) {
                Sync-SourceTrees
            }
            Invoke-GuestScript `
                -ScriptName "openq4-macos-sync-build-test.sh" `
                -ScriptAction "all" `
                -Environment @{
                    "OPENQ4_MACOS_GRAPHICS_BRIDGE" = $MacOSGraphicsBridge
                    "OPENQ4_BUILDTYPE" = $BuildType
                    "OPENQ4_BUILDDIR" = $BuildDir
                    "OPENQ4_SMOKE_LIMIT" = [string]$SmokeLimit
                }
        }
    }
}

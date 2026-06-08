# Linux Mint VMware Workflow

This workflow creates a Linux Mint VM for OpenQ4 Linux compatibility testing through VMware Workstation on the Windows development host.

## Create The VM

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/New-OpenQ4LinuxMintVm.ps1 -Start
```

Defaults:

- VM files: `.tmp/vmware/OpenQ4-LinuxMint/`
- ISO: `E:\ISO\linuxmint-22.3-cinnamon-64bit.iso`
- OS disk: 96 GB growable VMDK
- Data disk: growable VMDK attached as `scsi0:1` for Quake 4 assets
- Memory/CPU: 8 GB RAM, 4 vCPUs
- Shared folders:
  - `OpenQ4` -> this repository, writable only so guest logs can return under `.tmp/`
  - `OpenQ4-GameLibs` -> companion repo, read-only
  - `Quake4` -> Steam Quake 4 install, read-only
  - `Downloads` -> host Downloads folder, read-only, for the Quake 4 patch payload

## Install Mint

The Cinnamon ISO uses the desktop Ubiquity installer, so the OS install is the interactive part of the workflow. When VMware opens:

1. Choose **Start Linux Mint**.
2. Run **Install Linux Mint** from the desktop.
3. Install Mint to the 96 GB OS disk. Leave the second disk for the Quake 4 asset copy.
4. Create a normal sudo-capable user. The scripts default to `codex`, so use that username unless you plan to pass `-GuestUser`.
5. Reboot into the installed desktop.

The VM boots the ISO first for installation. Do not click VMware Workstation's yellow **I Finished Installing** bar until the Mint installer has actually finished. If that bar is clicked early, VMware disconnects the ISO and the VM falls through to PXE with `Operating System not found`.

When the Mint installer asks you to remove the installation medium, shut down or power off the VM and switch it to disk-first boot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/New-OpenQ4LinuxMintVm.ps1 -DiskFirst -DetachIso
```

Then start the VM again from VMware or with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/New-OpenQ4LinuxMintVm.ps1 -DiskFirst -DetachIso -Start
```

## Bootstrap And Build

From the Windows host, after the installed Mint desktop is running:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 -Action All -GuestUser codex
```

`-Action All` does the following through `vmrun` guest operations:

- waits for VMware Tools / open-vm-tools
- installs Linux build dependencies, open-vm-tools, SSH, rsync, Mesa tools, and the SDL3-native package set
- mounts VMware shared folders at `/mnt/hgfs`
- formats/mounts the second disk as `/mnt/openq4-data`
- copies Quake 4 assets to `/mnt/openq4-data/Quake4`
- applies the Downloads 1.4.2 patch overlay only when the copied assets do not already look patched
- syncs `OpenQ4` and `OpenQ4-GameLibs` into `~/openq4-work/` on the guest ext4 disk
- configures, builds, and stages OpenQ4 with `tools/build/meson_setup.sh`
- runs the smoke profile from `tools/tests/renderer_gameplay_benchmark.py` against `/mnt/openq4-data/Quake4`
- installs an executable `OpenQ4.desktop` launcher on the Mint desktop, pointing at the staged `.install` runtime and copied Quake 4 assets
- copies workflow logs back to `.tmp/vmware-linux-mint-results/` when the host share is writable

You can run narrower actions as needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 -Action Bootstrap -GuestUser codex
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 -Action Assets -GuestUser codex
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 -Action Build -GuestUser codex
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 -Action Smoke -GuestUser codex
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 -Action Launcher -GuestUser codex
```

If VMware HGFS returns I/O errors when reading the broad repository shares, mirror the source trees into a clean host staging directory under `.tmp/`, add those mirrors as VMware shared folders, and point the guest workflow at them:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/vmware/Invoke-OpenQ4LinuxMintWorkflow.ps1 `
  -Action Build `
  -GuestUser codex `
  -GuestHostRepoShare /mnt/hgfs/OpenQ4Staged `
  -GuestHostGameLibsShare /mnt/hgfs/OpenQ4GameLibsStaged `
  -GuestHostResultsDir /mnt/hgfs/OpenQ4Staged/.tmp/vmware-linux-mint-results
```

## Guest Paths

- Guest source workspace: `~/openq4-work/OpenQ4`
- Guest GameLibs workspace: `~/openq4-work/OpenQ4-GameLibs`
- Guest staged runtime: `~/openq4-work/OpenQ4/.install`
- Guest Quake 4 assets: `/mnt/openq4-data/Quake4`
- Guest desktop launcher: `~/Desktop/OpenQ4.desktop`
- Guest workflow logs: `~/openq4-work/results/`
- Host-copied logs: `.tmp/vmware-linux-mint-results/`

Builds intentionally happen on the guest filesystem, not directly on HGFS shared folders, to avoid case-sensitivity and filesystem-behavior noise in Linux compatibility results.

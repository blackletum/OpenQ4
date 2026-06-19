# macOS VM Testing Workflow

This workflow is the macOS counterpart to `docs-dev/linux-mint-vmware-workflow.md`.
It is intentionally SSH-based instead of VMware-on-Windows based: macOS test VMs
must run on Apple-branded hardware or a compliant Apple-hosted service.

The YouTube guide in issue/debugging discussions (`TD3H2J65u90`) describes
installing macOS Sequoia in VMware on a Windows PC. Do not use that path for
openQ4 automation. Keep this workflow on Apple hardware through Virtualization
framework, UTM, VMware Fusion, Parallels, or a hosted Mac provider.

## Host And Media Layout

- Windows-side macOS installer/restore-image inventory: `E:\ISO\macos\`
- Guest VM name recommendation: `openQ4-macOS`
- Guest user recommendation: `codex`
- Guest workspace: `~/openq4-work/`
- Guest source checkout: `~/openq4-work/openQ4`
- Guest GameLibs checkout: `~/openq4-work/openQ4-GameLibs`
- Guest Quake 4 assets: `~/openq4-work/Quake4`
- Guest results: `~/openq4-work/results/`

`E:\ISO\macos\` is only an inventory/staging location on the Windows host. The
actual VM creation still happens on the Apple host with the hypervisor you use
there. Store Apple restore images, IPSW files, installer app archives, or a
short README that points to the Apple host's VM storage in this directory.

## Create The VM

On Apple hardware:

1. Create a macOS VM with Virtualization framework, UTM, VMware Fusion, or
   Parallels.
2. Allocate at least 4 vCPUs, 8 GB RAM, and 96 GB disk. Use 16 GB RAM if
   shader and packaging validation will run often.
3. Create a sudo-capable `codex` user.
4. Enable Remote Login:

   ```bash
   sudo systemsetup -setremotelogin on
   ```

5. Install Xcode Command Line Tools:

   ```bash
   xcode-select --install
   ```

6. Optional but recommended: install Homebrew from `https://brew.sh/`.

From the Windows host, check connectivity:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action Probe `
  -MacHost <mac-vm-ip-or-hostname> `
  -MacUser codex
```

## Bootstrap, Assets, Build, And Test

Run the full setup from the Windows host:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action All `
  -MacHost <mac-vm-ip-or-hostname> `
  -MacUser codex
```

`-Action All` does the following:

- verifies the macOS command-line toolchain
- installs or updates a user-local Meson/Ninja venv
- copies the Windows Quake 4 install into `~/openq4-work/Quake4`
- copies `openQ4` and `openQ4-GameLibs` into the guest workspace
- configures, builds, and stages openQ4 through `tools/build/meson_setup.sh`
- runs a renderer smoke profile against the copied Quake 4 assets
- runs the macOS-facing GL 4.1 renderer validation set
- creates `~/Desktop/openQ4.command` pointing at the staged runtime and assets

Run narrower actions as needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Bootstrap -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Assets -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Sync -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Build -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Smoke -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Renderer -MacHost <host>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 -Action Launcher -MacHost <host>
```

To test the Metal bridge package path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/macos/Invoke-openQ4MacOSWorkflow.ps1 `
  -Action Sync,Build,Smoke,Renderer `
  -MacHost <host> `
  -MacOSGraphicsBridge metal
```

## Expected Validation

For macOS debugging, do not stop at static checks. Use this VM workflow for:

- `renderer_gameplay_benchmark.py --profile smoke`
- `renderer_validation_matrix.py --tiers auto,gl41`
- staged launch from `~/Desktop/openQ4.command`
- log inspection under the guest `~/openq4-work/results/` run directory

The remaining stock Quake 4 duplicate material warnings from retail assets are
not macOS VM setup failures. Investigate missing images, shader/link errors, GL
errors, PK4 mount errors, app bundle failures, dylib load errors, input/audio
device failures, and crashes.

## No Persistent Mac Yet

If no Apple VM or hosted Mac is available yet, use the manual GitHub Actions
workflow `.github/workflows/macos-debug.yml` as the interim macOS debug target.
It builds and stages the macOS OpenGL and/or Metal bridge variants on Apple's
hosted macOS runner, uploads `.install`, Meson logs, host diagnostics, and
optional assetless renderer-probe logs.

Run it from GitHub Actions as **macOS Debug**. Use `bridge=both` for broad
coverage, or pick `opengl`/`metal` while chasing a focused issue. Enable
`run_runtime_assetless` only when startup/runtime logs are needed; hosted macOS
runner display capabilities can vary, so that step is allowed to fail while
still publishing whatever logs were produced.

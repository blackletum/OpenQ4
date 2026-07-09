# Shadow-regression golden references

The `shadow-regression` gameplay-benchmark profile compares live captures against
blessed reference screenshots. The reference images are ~11 MB TGAs (2560x1440) and
are deliberately **not committed**; this directory holds their SHA256 manifest so a
blessed set is reproducible and verifiable.

## Layout (local, untracked)

```
.tmp/renderer-references/shadow-regression/windows-x64/
  <case-id>/screenshots/renderer-bench/sp_0.tga
```

Reference lookup is per-case (`<case-id>/...` takes precedence over flat paths), since
every case captures the same relative screenshot name.

## Regenerating a blessed set

1. Build and stage a known-good tree (`tools\build\meson_setup.ps1 compile` + `install`).
2. Capture: `python tools\tests\renderer_gameplay_benchmark.py --profile shadow-regression --output-dir .tmp\renderer-gameplay\shadow-refgen`
3. Copy each case's `savepaths\<case-id>\baseoq4\screenshots\renderer-bench\sp_0.tga`
   to `.tmp\renderer-references\shadow-regression\windows-x64\<case-id>\screenshots\renderer-bench\sp_0.tga`.
4. Update `shadow-regression-manifest.txt` with the new SHA256s and blessing commit.

## Running the regression

```
python tools\tests\renderer_gameplay_benchmark.py --profile shadow-regression ^
  --reference-dir .tmp\renderer-references\shadow-regression\windows-x64 --require-references
```

A shadow rendering change that is *intended* to alter output must re-bless the set
(and say so in its commit message); anything else failing the RMS threshold is a
regression. References are machine/driver-specific: compare only against references
captured on the same GPU/driver family.

## Known capture variance

The profile freezes game time (`g_stopTime 1`), which makes
`shadow-projected-airdefense2`, `shadow-character-airdefense2`, and
`shadow-cutout-storage2` deterministic (run-to-run RMS < 1 against the default
threshold of 2.0). Two cases still frame realtime-animated content that game time
does not freeze (GUI monitor surfaces / sky):

- `shadow-point-storage2` — run-to-run RMS ~8
- `shadow-csm-airdefense1` — run-to-run RMS ~12

Treat comparison failures on those two as signal only when the RMS moves well above
those baselines (or inspect the diff visually); tightening them requires re-aiming
the capture views away from realtime-animated surfaces.

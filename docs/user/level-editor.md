# Level editor (experimental)

Run `editorExperimental` in the client console to open the separate experimental
workspace. It uses the engine's GUI and renderer interfaces and works with
OpenGL and Vulkan. Install the matching bundled data with the engine.

The legacy Radiant source and its `editor` command in tool-enabled builds are
retained unchanged. The supported Meson build still does not build Radiant;
this experiment does not restore or replace it. Experimental settings and
recovery files use their own namespace. This is an early
authoring workspace. The
[development roadmap](../dev/level-editor-modernization.md) tracks geometry,
docking, asset and build/play work still needed toward an idStudio-like standard.

## Graphical workspace

- Enter a map path and choose **Open**, or use **Maps**, select a source, then
  choose **Open**. **Entities** lists source entities and **Classes** lists
  available entity-definition names. Type a filter and press Enter to search.
- Select an entity in the list or click its visible geometry/point marker in
  the preview. **Frame** centers the camera on it; **All** frames the map.
  Right-drag orbits, Shift + right-drag pans, and the wheel zooms. Press **F**
  while the viewport has focus to frame the selection. Camera buttons provide
  the same navigation without dragging.
- Select a property to fill **Key** and **Value**, then use **Apply** or Enter
  to commit it. **Remove** deletes that property. **Undo** and **Redo** operate
  on committed changes. Changing a field alone does not edit the document.
- Choose a class/name and **Create** to add a point entity, then edit its origin
  and other properties. **Delete** removes the selected entity; worldspawn is
  protected. Class validity is still the author's responsibility.
- Drag the gaps beside the preview to resize the browser and inspector.
  Widths persist through `editorExperimental_leftPane` and
  `editorExperimental_rightPane`. **Layout** switches between two presets.
  Resizing preserves uncommitted inspector text.
- **New**, **Open** and **Close map** ask before replacing unsaved work. A
  failed replacement read or parse preserves the current document even after
  confirming discard. **Return to menu** keeps the document available; reopen
  the workspace with `editorExperimental`.

The preview renders source brushes and bounded patch tessellation with a
camera light. Point entities use small markers (lights yellow, player starts
green, other classes blue, selected markers orange), rather than their actual
models. It does not show compiled lighting, game simulation or full material
effects. Brushes/patches cannot yet be created or transformed. Invisible or
translucent geometry may need selection through the entity list. A skipped
primitive remains intact in the source; its preview failure does not remove it
from saves. Detailed failures appear in the console.

Known experimental limitation: a full `vid_restart` while the workspace is open
can leave some labels missing on either renderer. Additional frames do not
reliably restore them, and reopening alone is not a reliable workaround. Save
your work before restarting the client. This visual defect remains open;
document edits and saves passed the restart workflow checks.

## Console and files

Run `editorExperimental help` to list commands. `mapEdit` is an equivalent command name.
The workspace edits a separate map document; changes do not mutate the running
game. Use `editorExperimental entities` to see session-local entity handles, and
`editorExperimental get <handle>` to inspect properties.

```text
editorExperimental open game/airdefense1
editorExperimental entities light
editorExperimental get 1
editorExperimental set 1 _editor_note "Lighting review"
editorExperimental undo
editorExperimental redo
editorExperimental savecopy tools/airdefense_review
editorExperimental save
```

Paths are relative to `maps/`; `.map` is optional. Reads use the engine's normal
asset search, including installed PK4s. Saves go to
`fs_savepath/baseoq4/maps/`, leaving installed PK4s untouched. Specify a new name
with `editorExperimental save tools/my_level` to make it the working path, or use
`editorExperimental savecopy tools/review_copy` to keep the existing working path and dirty
state. New destinations must not already exist. Subsequent saves to the working
path refuse to overwrite external changes, and changed existing maps retain the
previous bytes in a sibling `.map.bak`.

`editorExperimental new` starts with a worldspawn. `editorExperimental create light key_light` adds a
point entity and prints its handle. Use `editorExperimental set <handle> origin "0 0 128"`
and other property edits to configure it. `editorExperimental unset <handle> <key>` removes
a property, and `editorExperimental delete <handle>` removes an entity. Worldspawn and
required classnames are protected. Geometry is preserved as source; creating or
transforming brushes and patches is not implemented yet. Class/asset validity
and entity references still need compilation and gameplay validation.

Undo/redo retains up to 256 edits within a 16 MiB budget. Undoing away from a
saved revision marks the document dirty; returning to that revision makes it
clean. New/Open/Close refuse to discard unsaved edits. Use `editorExperimental close discard`
only when those edits can be abandoned.

Recovery saves default to every 120 seconds while dirty. Change
`editorExperimental_autoSaveSeconds` or run `editorExperimental autosave` immediately. Zero disables the
periodic timer; normal shutdown still attempts a recovery save. Recovery files
live under `fs_savepath/baseoq4/editor_experimental/recovery/`, mirroring the working map path.
For an unnamed document the filename is `unnamed.map`. Recovery does not count
as saving the working map.

```text
editorExperimental recover game/airdefense1
editorExperimental save tools/airdefense_recovered
```

Use `editorExperimental recover` without a path for an unnamed document. Restored work opens
unnamed and dirty, so save it to a fresh destination. Recovery is a latest-copy
mechanism, not persistent undo history or a version-control system.

Use the existing `dmap <map>` and SP `devmap <map>` or MP `spawnServer <map>`
commands to compile and test work. Property-only changes do not require geometry
recompilation when they only affect gameplay; properties that affect compiled
geometry still require `dmap`. A saved copy with a new map name may also need the original
map's script/navigation/compiled companions; this service does not copy those.

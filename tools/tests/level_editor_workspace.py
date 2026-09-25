#!/usr/bin/env python3
"""Qualify the actual editor workspace controller and engine-rendered viewport.

Commands invoke the same actions as GUI widgets; no keyboard/mouse events are
injected. Native GUI interaction still needs manual qualification. The final
saved source is loaded into SP gameplay before the client exits.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re

from level_editor_runtime import ROOT, SOURCE_MAP, stock_map, check
from renderer_gameplay_benchmark import collect_runtime_files


def run(args: argparse.Namespace) -> dict:
    output = args.output.resolve()
    output.relative_to(ROOT / ".tmp")
    output.mkdir(parents=True, exist_ok=False)
    runtime = ROOT / ".install"
    executable = runtime / ("openQ4-client_x64.exe" if os.name == "nt" else "openQ4-client_x64")
    before = collect_runtime_files(runtime)
    (output / "runtime-before.json").write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
    game = output / "baseoq4"
    original = stock_map(args.assets)
    source = game / SOURCE_MAP
    source.parent.mkdir(parents=True)
    source.write_bytes(original)
    malformed = game / "maps/tools/workspace_malformed.map"
    malformed.parent.mkdir(parents=True)
    overflow_brush = '{ brushDef3 {\n' + '\n'.join(
        f'( {plane} -8 ) ( ( 1e40 0 0 ) ( 0 1 0 ) ) "_white"'
        for plane in ('1 0 0', '-1 0 0', '0 1 0', '0 -1 0', '0 0 1', '0 0 -1')
    ) + '\n} }\n'
    malformed.write_text('Version 3\n{\n"classname" "worldspawn"\n{ brushDef3 { ( 1 0 0 ) } }\n' +
                         overflow_brush + '}\n', encoding="ascii")
    (game / "maps/tools/workspace_invalid.map").write_text('Version 3\n{ broken }\n', encoding="ascii")
    (game / "maps/tools/workspace_navigation.map").write_text(
        'Version 3\n{\n"classname" "worldspawn"\n}\n'
        '{\n"classname" "info_player_start"\n"name" "navigation_target"\n"origin" "0 0 0"\n}\n', encoding="ascii")
    commands = [
        "echo EXPERIMENTAL_COMMANDS_BEGIN", "listToolCmds editor*", "echo EXPERIMENTAL_COMMANDS_END",
        "editorExperimental open tools/workspace_malformed", "editorExperimental", "wait 2", "editorExperimental close",
        "editorExperimental open tools/workspace_navigation", "editorExperimental",
        "editorExperimentalWorkspace field edBrowser_sel_0 1", "editorExperimentalWorkspace action select", "editorExperimentalWorkspace action frame",
        "echo WORKSPACE_PICK_BEFORE", "editorExperimentalWorkspace status",
        "editorExperimentalWorkspace view orbit 45 10 1", "editorExperimentalWorkspace view pan 0.05 -0.05", "editorExperimentalWorkspace action frame",
        "editorExperimentalWorkspace view pick 0.5 0.5 1", "echo WORKSPACE_PICK_AFTER", "editorExperimentalWorkspace status",
        "editorExperimental open game/airdefense1", "editorExperimental",
        "editorExperimentalWorkspace view panes 154 188", "editorExperimentalWorkspace status",
        'editorExperimentalWorkspace field ed_filter "info_player_start"',
        "editorExperimentalWorkspace action filter", "editorExperimentalWorkspace field edBrowser_sel_0 0",
        "editorExperimentalWorkspace action select", "editorExperimentalWorkspace action frame",
        "waitMsec 2000", 'screenshot "screenshots/workspace-initial.tga"',
        "editorExperimentalWorkspace field ed_key _editor_workspace_test",
        "editorExperimentalWorkspace field ed_value one", "editorExperimentalWorkspace action apply",
        "editorExperimentalWorkspace action undo", "editorExperimental savecopy tools/workspace_undo",
        "editorExperimentalWorkspace action redo",
        "echo WORKSPACE_REJECTED_OPEN_BEGIN", "editorExperimentalWorkspace field ed_path tools/workspace_invalid",
        "editorExperimentalWorkspace action open", "editorExperimentalWorkspace action confirm", "editorExperimental status", "echo WORKSPACE_REJECTED_OPEN_END",
        "editorExperimentalWorkspace action new",
        "editorExperimentalWorkspace view panes 210 220", "editorExperimentalWorkspace status",
        "wait 2", 'screenshot "screenshots/workspace-unsaved.tga"',
        "editorExperimentalWorkspace action cancel",
        "editorExperimentalWorkspace field ed_path tools/workspace_copy", "editorExperimentalWorkspace action copy",
        "editorExperimentalWorkspace field ed_path game/airdefense1", "editorExperimentalWorkspace action save",
        "editorExperimentalWorkspace field ed_key _draft_key", "editorExperimentalWorkspace field ed_value not_applied",
        "editorExperimentalWorkspace view panes 999 999", "editorExperimentalWorkspace status", "wait 2",
        'screenshot "screenshots/workspace-layout.tga"',
        "set r_windowWidth 1600", "set r_windowHeight 900", "set r_customWidth 1600", "set r_customHeight 900",
        "vid_restart", "waitMsec 1000", 'screenshot "screenshots/workspace-restart.tga"',
        "wait 30", 'screenshot "screenshots/workspace-restart-settled.tga"',
        "editorExperimental", "wait 3", 'screenshot "screenshots/workspace-restart-reopen.tga"',
        "editorExperimentalWorkspace action hide", "editorExperimental status", "devmap game/airdefense1",
    ]
    (game / "editor_workspace.cfg").write_text("\n".join(commands) + "\n", encoding="ascii")
    (game / "editor_workspace_play.cfg").write_text(
        'set g_autoExecAfterMapLoad ""\nwaitMsec 3000\nscreenshot "screenshots/workspace-gameplay.tga"\n'
        'editorExperimental\nwait 2\nscreenshot "screenshots/workspace-in-game.tga"\n'
        'editorExperimentalWorkspace action hide\nwaitMsec 1500\nscreenshot "screenshots/workspace-return-menu.tga"\n'
        "wait 2\necho EDITOR_WORKSPACE_COMPLETE\nquit\n", encoding="ascii")
    settings = {
        "fs_basepath": str(args.assets.resolve()), "fs_savepath": str(output), "fs_devpath": str(output),
        "fs_game": "baseoq4", "si_gameType": "singleplayer", "r_fullscreen": "0", "r_hiddenWindow": "1",
        "r_renderApi": "vulkan" if args.renderer == "vk" else "gl", "r_mode": "-1",
        "r_windowWidth": "1280", "r_windowHeight": "720", "r_customWidth": "1280", "r_customHeight": "720",
        "in_mouse": "0", "in_joystick": "0", "s_noSound": "1", "logFile": "2", "logFileName": "logs/openq4.log",
        "developer": "1", "com_allowConsole": "1", "editorExperimental_autoSaveSeconds": "0", "r_useLightGrid": "0",
        "com_skipLoadingContinue": "1", "com_loadingContinueAutoAdvance": "1", "g_autoSkipCinematics": "1",
        "g_autoExecAfterMapLoad": "editor_workspace_play.cfg", "g_autoExecAfterMapLoadDelayMs": "1000",
        "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances": "1",
    }
    launch = [str(executable)]
    for key, value in settings.items():
        launch += ["+set", key, value]
    launch += ["+exec", "editor_workspace.cfg"]
    (output / "launch.json").write_text(json.dumps(launch, indent=2) + "\n")
    with (output / "process.log").open("wb") as stream:
        process = subprocess.run(launch, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT, timeout=360)
    after = collect_runtime_files(runtime)
    check(before == after, "shared runtime changed during qualification")
    log_path = game / "logs/openq4.log"
    log = log_path.read_text(encoding="utf-8", errors="replace")
    check(process.returncode == 0, f"client exited {process.returncode}")
    check("EDITOR_WORKSPACE_COMPLETE" in log, "saved-map gameplay did not complete")
    command_listing = log.split("EXPERIMENTAL_COMMANDS_BEGIN", 1)[1].split("EXPERIMENTAL_COMMANDS_END", 1)[0]
    check(re.search(r"^\s+editorExperimental\s+opens the experimental level editor$", command_listing, re.MULTILINE),
          "experimental workspace command not registered separately")
    legacy_command = re.search(r"^\s+editor\s+(.+)$", command_listing, re.MULTILINE)
    check(not legacy_command or legacy_command.group(1) == "launches the level editor Radiant",
          "experimental workspace replaced the legacy editor command")
    expected_geometry_diagnostics = [line for line in log.splitlines() if "WARNING:" in line and "editor primitive" in line]
    check(expected_geometry_diagnostics and "editor preview: entities=0 triangles=0 skipped=2" in log,
          "malformed geometry was not reported without terminating the editor")
    # The retail MP model catalogue opened by Return to menu references this
    # absent image. Keep this narrowly identified asset issue visible in reports.
    known_asset_diagnostics = [line for line in log.splitlines() if "WARNING:" in line and
                              "Couldn't load image: models/monsters/burn_misc_sm : models/monsters/burn_misc_sm#__0200" in line]
    rejected_open = log.split("WORKSPACE_REJECTED_OPEN_BEGIN", 1)[1].split("WORKSPACE_REJECTED_OPEN_END", 1)[0]
    expected_open_diagnostics = [line for line in rejected_open.splitlines() if "editor workspace rejected:" in line]
    check(len(expected_open_diagnostics) == 1 and "editor status: open=1 dirty=1 entities=2195" in rejected_open,
          "failed confirmed open discarded the dirty source")
    failures = [line for line in log.splitlines() if ("WARNING:" in line or "ERROR:" in line or "editor workspace rejected:" in line)
                and line not in expected_geometry_diagnostics + known_asset_diagnostics + expected_open_diagnostics]
    check(not failures, "workspace diagnostics: " + "\n".join(failures[:10]))
    check("editor preview: entities=" in log, "preview did not build")
    check("editor workspace: rows=2195" in log, "native entity list truncated the stock map")
    statuses = re.findall(r"editor workspace: rows=2 selected=(\d+) left=", log)
    check(len(statuses) == 2 and statuses[0] == statuses[1], "viewport picking did not retain the framed point entity")
    check("left=154 right=188 confirm=1" in log, "modal confirmation allowed workspace navigation")
    check("left=210 right=220 confirm=0 key=_draft_key" in log, "pane resizing lost draft fields or escaped its limits")
    updates = re.findall(r"editor preview update: rebuilt=(\d+) elapsed=(\d+)ms", log)
    check(sum(int(rebuilt) == 0 for rebuilt, _ in updates) >= 3, "property edit/undo/redo rebuilt unchanged geometry")
    check((game / "maps/tools/workspace_undo.map").read_bytes() == original, "workspace undo was not lossless")
    edited = source.read_bytes()
    check(edited.count(b'"_editor_workspace_test" "one"') == 1, "inspector edit did not reach one entity")
    check((game / "maps/tools/workspace_copy.map").read_bytes() == edited, "workspace copy differs")
    check((game / (SOURCE_MAP + ".bak")).read_bytes() == original, "workspace backup differs")
    captures = sorted((game / "screenshots").glob("workspace-*.tga"))
    check(len(captures) == 9 and all(path.stat().st_size > 18 for path in captures), "workspace captures missing")
    restart = (game / "screenshots/workspace-restart.tga").read_bytes()
    check(int.from_bytes(restart[12:14], "little") == 1600 and int.from_bytes(restart[14:16], "little") == 900,
          "renderer restart did not resize the workspace")
    check("editor status: open=1 dirty=0 entities=2195" in log, "cancelled replacement did not preserve the saved document")
    report = {
        "result": "PASS", "renderer": args.renderer, "runtimeUnchanged": before == after,
        "command": "editorExperimental", "legacy_command": legacy_command.group(1) if legacy_command else "not built",
        "runtimeFiles": before, "log": str(log_path), "captures": [str(p) for p in captures],
        "source_sha256": hashlib.sha256(original).hexdigest(), "edited_sha256": hashlib.sha256(edited).hexdigest(),
        "visual_review": "required", "native_input_review": "not performed; no input-control permission",
        "preview": [line for line in log.splitlines() if line.startswith("editor preview:")],
        "expected_geometry_diagnostics": expected_geometry_diagnostics,
        "known_asset_diagnostics": known_asset_diagnostics, "preview_updates": updates,
        "expected_open_diagnostics": expected_open_diagnostics,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--renderer", choices=("gl", "vk"), default="gl")
    result = run(parser.parse_args())
    print(json.dumps({key: result[key] for key in ("result", "renderer", "runtimeUnchanged", "preview")}, indent=2))

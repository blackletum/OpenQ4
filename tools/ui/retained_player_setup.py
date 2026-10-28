"""Player Setup edits inside the Multiplayer page, without another menu level.

The session owns committed identity, catalog and model assets. Text Enter and
value controls submit typed proposals with the observed value/catalog identity;
neither GUI source nor a deferred row number can select another asset silently.
"""
from __future__ import annotations

from build_retained_screens import (CANVAS_W, FULL, OLIVE, ORANGE, VALUE, U, Document, absolute, colour, group, keyword,
                                    label, length, linear, number, path, rgb, solid, stroke, transform, typeface, vector)
from retained_create_server import text_row
from retained_mp_menus import any_text, in_game_plate, value_row

MODEL_ROWS = 128  # Bounded choice page, not a limit on the installed catalog.
ROTATION_LABEL = "#str_231547"
WIDTH, VALUE_X, ROW_H = 500.0, 190.0, 45.0


def proposal(doc: Document, ident: str, field: str, *, value: dict, expected: str = "", kind: str | None = None) -> str:
    action = f"setup.{ident}"
    doc.actions[action] = {"operation": "player.setup.apply", "arguments": {
        "field": field, "value": value, "catalog": {"state": "setup.catalog"},
        "expected": {"state": expected} if expected else ""}}
    if kind is not None:
        doc.actions[action]["input"] = kind
    return action


def rail_row(doc: Document) -> tuple[dict, list[str]]:
    """Stock hues, vector chips, 44 dp targets and a selected chip's taller ink."""
    swatches = ((1, 0, 0), (1, .501, 0), (1, 1, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1), (1, 0, .501))
    shape = [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}),
             (0, {"fraction": 1, "dp": -1}), (0, 7)]
    children = [vector("setup-rail-plate", FULL, [path("plate", shape, fill=solid(rgb(OLIVE, .12)),
                                                                  stroke=stroke(solid(rgb(OLIVE, .32)), 1))]),
                label("setup-rail-label", "#str_200981", {**absolute(left=21, top=0, width=VALUE_X-29, height=52),
                      **typeface("lowpixel", 14, 26, rgb(OLIVE))})]
    controls = []
    for row, tint in enumerate(swatches):
        ident = f"setup-rail-{row}"
        selected = {"op": "==", "args": [{"state": "setup.rail"}, row]}
        ring = vector(f"{ident}-ring", {**absolute(left=13, top=11, width=18, height=22), "opacity": number(0)}, [
            path("ring", [(.75, .75), (17.25, .75), (17.25, 21.25), (.75, 21.25)], stroke=stroke(solid(rgb(ORANGE)), 1.5))])
        chip = vector(f"{ident}-chip", {**absolute(left=16, top=17, width=12, height=10), "transform": transform()}, [
            path("chip", [(0, 0), (12, 0), (12, 10), (0, 10)], fill=solid([*tint, 1]))])
        doc.bind(f"{ident}.selected", f"{ident}-chip", "transform",
                 [0, {"op": "select", "args": [selected, -3, 0]}, 1, {"op": "select", "args": [selected, 1.6, 1]}, 0])
        states = doc.states(ident, {state: [(f"{ident}-ring", "opacity", number(alpha))]
                                  for state, alpha in (("default", 0), ("hover", .6), ("focus", 1), ("pressed", 1), ("disabled", 0))})
        action = proposal(doc, f"rail{row}", "rail", value=row, expected="setup.observed_rail")
        children.append(group(ident, absolute(left=VALUE_X + 44*row, top=4, width=44, height=44), [ring, chip],
                              control={"role": "button", "label": "#str_200981", "action": action, "states": states,
                                       "context": {"op": "numberText", "args": [{"state": "setup.catalog"}]}}))
        controls.append(ident)
    return group("setup-rail", {"position": keyword("relative"), "width": length(WIDTH), "height": length(52),
                                 "flex-shrink": number(0)}, children), controls


def player_setup(doc: Document) -> dict:
    """A same-level editor: stock leading rows and right-side animated preview.

    Back closes this editor onto PLAYER SETUP; committed changes already apply,
    and unfinished field text is discarded by the ordinary hidden-owner rules.
    The title and bands remain on the Multiplayer page throughout.
    """
    doc.state.update({"setup.open": {"type": "boolean", "initial": False},
                      "setup.catalog": {"type": "number", "initial": 0},
                      "setup.model_row": {"type": "number", "initial": -1},
                      "setup.model_count": {"type": "number", "initial": 0},
                      "setup.previous": {"type": "boolean", "initial": False},
                      "setup.next": {"type": "boolean", "initial": False},
                      "setup.rotation": {"type": "number", "initial": 200},
                      "setup.rail": {"type": "number", "initial": -1}})
    for state in ("name", "clan", "observed_name", "observed_clan", "observed_model", "observed_rail",
                  "model_name", "body", "head", "skin", "head_skin", "notice"):
        doc.state[f"setup.{state}"] = {"type": "string", "initial": ""}
    rows = []
    for ident, key, field, size, empty in (("name", "#str_200078", "name", 23, False),
                                          ("clan", "#str_200079", "clan", 12, True)):
        node = f"setup-{ident}"
        row = text_row(doc, node, key, WIDTH, VALUE_X, f"setup.{field}", "unused", size, ROW_H, allow_empty=empty)
        # Text Enter carries its exact proposal to the session; it is never
        # reconstructed by reading a later GUI-state string from a bare verb.
        generated_action = row["control"]["action"]
        doc.actions.pop(generated_action)
        row["control"]["action"] = proposal(doc, ident, field, value={"input": "value"},
                                             expected=f"setup.observed_{field}", kind="string")
        rows.append(row)
    options = []
    for row in range(MODEL_ROWS):
        doc.state[f"setup.model.{row}.name"] = {"type": "string", "initial": ""}
        options.append(({"state": f"setup.model.{row}.name"}, row))
    action = proposal(doc, "model", "model", value={"input": "value"}, expected="setup.observed_model", kind="number")
    rows.append(value_row(doc, "setup-model", "#str_200080", "choice", WIDTH, VALUE_X,
                          value={"state": "setup.model_row"}, action=action, entries=options,
                          count={"state": "setup.model_count"}, row_h=ROW_H, placement="setup-fields", wrap=True))
    pages = []
    for ident, key, value, allowed in (("previous", "#str_42885", -1, "setup.previous"), ("next", "#str_42886", 1, "setup.next")):
        action = proposal(doc, ident, "page", value=value)
        button = in_game_plate(doc, f"setup-model-{ident}", key, 0 if value < 0 else WIDTH/2+6, 0,
                               width=WIDTH/2-6, action=action)
        button["control"]["enabled"] = True
        doc.bind(f"setup-model-{ident}.enabled",f"setup-model-{ident}","enabled",{"state":allowed})
        button["control"]["context"] = {"op": "numberText", "args": [{"state": "setup.catalog"}]}
        pages.append(button)
    rows.append(group("setup-model-pages", {"position": keyword("relative"), "width": length(WIDTH), "height": length(44),
                                            "flex-shrink": number(0)}, pages))
    action = proposal(doc, "rotation", "rotation", value={"input": "value"}, kind="number")
    rows.append(value_row(doc, "setup-rotation", ROTATION_LABEL, "slider", WIDTH, VALUE_X,
                          value={"state": "setup.rotation"}, action=action, slider=(0, 360, 5, 0), row_h=ROW_H, wrap=True))
    rail, rail_controls = rail_row(doc)
    rows.append(rail)
    doc.session("playerSetupClose", "playerSetupClose")
    rows.append(in_game_plate(doc, "setup-close", "#str_230045", 0, 0, width=WIDTH, action="playerSetupClose"))
    # Let the rows flow in a viewport: enlarged text and narrow windows retain
    # every control rather than pushing the last one under the prompt bar.
    for row in rows:
        row["properties"].update({"position": keyword("relative"), "left": length(0), "top": length(0),
                                   "flex-shrink": number(0)})
    fields = group("setup-fields", {**absolute(left=U*58, top=U*167, width=WIDTH+16, height=U*243),
                                      "overflow": keyword("auto"), "display": keyword("flex"), "flex-direction": keyword("column"),
                                      "row-gap": length(6)}, rows)
    model = {"id": "setup-preview", "type": "model", "properties": {**absolute(left=0, top=0, width=CANVAS_W, height=720),
                                                                                        "pointer-events": keyword("none")},
             "scene": {"parts": [
                 {"model": {"state": "setup.body"}, "skin": {"state": "setup.skin"}, "anim": "idle", "animClass": "player_ui_anims"},
                 {"model": {"state": "setup.head"}, "skin": {"state": "setup.head_skin"}, "joint": "chest", "anim": "idle",
                  "animClass": {"state": "setup.head"}},
                 {"model": "weapon_machinegun_world", "joint": "PST_ATTACHER", "anim": "idle", "animClass": "weapon_blaster_gui"}],
                 "origin": [-50, 0, -40], "rotation": [0, {"state": "setup.rotation"}, 0], "camera": [-160, 65, -5],
                 "lights": [{"origin": [-100, -50, -60], "colour": [.45, .25, .125]}, {"origin": [-120, 120, 120], "colour": [.8, .9, 1]}]}}
    heading = label("setup-heading", "#str_231122", {**absolute(left=U*67, top=U*130, width=WIDTH, height=32),
                                                     **typeface("marine", 20, 32, rgb(OLIVE))})
    current = label("setup-current-model", "#str_230030", {**absolute(left=U*421, top=U*380, width=U*200, height=48),
                                                          **typeface("lowpixel", 16, 22, rgb(VALUE)), "overflow": keyword("hidden"),
                                                          "text-align": keyword("center"), "text-overflow": keyword("ellipsis")})
    doc.bind("setup-current-model.text", "setup-current-model", "text", {"state": "setup.model_name"})
    notice = label("setup-notice", "#str_230030", {**absolute(left=U*67, top=U*413, width=U*520, height=40),
                                                   **typeface("lowpixel", 14, 20, rgb(VALUE)), "overflow": keyword("hidden")})
    doc.bind("setup-notice.text", "setup-notice", "text", {"state": "setup.notice"})
    pane = group("setup-panel", {**absolute(left=0, top=0, width=CANVAS_W, height=720), "display": keyword("none")},
                 [model, heading, current, fields, notice])
    doc.bind("setup-panel.display", "setup-panel", "display", {"op": "select", "args": [{"state": "setup.open"}, "block", "none"]})
    return pane

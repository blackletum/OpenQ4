"""The retained SETTINGS page (docs/dev/ui-visual-design.md section 9,
"Menu levels"; UI-01).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. SETTINGS on the title or the pause hands off to
guis/menu/settings.q4ui as MULTIPLAYER does to its page
(idSessionLocal::BeginRetainedPage): the home docks its bands and carries its
label into the title slot, and at 550 ms the page presents in the page state,
that label its title. It is the stock settings screen's level: its buttons
CONTROLS, GAME OPTIONS, DISPLAY, AUDIO and LOAD DEFAULTS on the home
navigation plates, with BACK under them. CONTROLS, GAME OPTIONS and AUDIO
open their retained pages, and DISPLAY the retained SYSTEM page where it is
on; each returns here onto its section. Where one cannot, the page leaves as
the Multiplayer page does for a stock page (its plates fade over 150 ms as it
sweeps 640 u toward the trailing edge over 300 ms) and the stock settings
screen takes over at 500 ms on that panel; its Back returns here
(settingsOpen). LOAD DEFAULTS asks first, in the stock dialog's words, and
restores the defaults as the stock dialog's YES did. BACK returns the home.
"""
from __future__ import annotations

from build_retained_screens import (BOTTOM_TO_PAGE, CANVAS_W, FULL, OLIVE, PAGE_TITLE_H, PAGE_TITLE_W, TITLE_SLOT, TOP_TO_PAGE, U,
                                    absolute, colour, confirmation, fit_narrow, framing_bands, group, keyword, label, length,
                                    lit_field, navigation_plate, number, page_prompts, rgb, track, transform, typeface, wrap_menu,
                                    Document)
from retained_difficulty import check_fits

NAV_TOP = 181.0    # the Multiplayer page's first plate, on the same level
# Each section: its id, label (the stock settings screen's button) and verb;
# LOAD DEFAULTS asks first (defaultsModal).
SECTIONS = (("controls", "#str_200083", "settingsControls"), ("game", "#str_200084", "settingsGame"),
            ("display", "#str_229900", "settingsDisplay"), ("audio", "#str_229901", "settingsAudio"),
            ("defaults", "#str_200086", None))
DEPART_MS = 300    # the stock screen's slide; it arrives at 500 ms
DETAILS = (210.0, 140.0)    # the content column's box, u
DETAIL_TEXT, DETAIL_LINE = 18, 26


def check_settings_fits() -> None:
    """Every shipped language's labels fit: the section labels in the plate's
    314 u label box at the focus scale (1.03), the title in its slot and the
    description in the content column's lines."""
    labels = [key for _, key, _ in SECTIONS] + ["#str_230045"]
    check_fits(tuple((key, "marine", 24, U * 314 / 1.03) for key in labels) +
               (("#str_200009", "marine", 18, U * PAGE_TITLE_W),),
               (("#str_231546", "lowpixel", DETAIL_TEXT, U * DETAILS[0], int(U * DETAILS[1] // DETAIL_LINE)),))


def settings_page_document() -> dict:
    """The SETTINGS page in the page state (sections 8 and 9): the bands
    docked, the title in the top band's slot, the sections on the home
    navigation plates and the page's description in the content column, over
    the lit field; the prompt bar's Select and Back.
    onActivate   every arrival: the plates at once, the description fading in
                 over 150 ms and the focus on CONTROLS; back from a section
                 (the session sets from_<section> first) the focus is on it;
    depart       a section opens the stock screen: the plates fade over 150 ms
                 while the page sweeps 640 u toward the trailing edge over
                 300 ms, and the title and description go with them;
    defaultsModalShow  LOAD DEFAULTS' question; YES asks the session to
                 restore the defaults (settingsDefaults);
    onDeactivate places it at rest while it is hidden, docked with its title
                 in the slot and its content out, so it never presents moved
                 and a return fades it in from nothing."""
    check_settings_fits()
    doc = Document("openq4.settings")
    doc.session("back", "settingsBack")
    doc.session("settingsDefaults", "settingsDefaults")
    defaults = confirmation(doc, "defaultsModal", "#str_200086", "#str_200156", None, yes_event="defaultsConfirmed")
    # YES closes the question and asks the session to restore the defaults.
    doc.events["defaultsConfirmed"] = [{"op": "call", "event": "defaultsModalHide"},
                                       {"op": "action", "action": "settingsDefaults"}]
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    buttons = []
    for ident, key, action in SECTIONS:
        if action is None:
            buttons.append(navigation_plate(doc, ident, key, event="defaultsModalShow"))
            continue
        doc.session(action, action)
        buttons.append(navigation_plate(doc, ident, key, action=action))
    buttons.append(navigation_plate(doc, "back", "#str_230045", action="back"))
    wrap_menu(buttons)
    description = label("description", "#str_231546", {**absolute(left=0, top=0, width=U * DETAILS[0], height=U * DETAILS[1]),
                                                       **typeface("lowpixel", DETAIL_TEXT, DETAIL_LINE, rgb(OLIVE))})
    bands = framing_bands("band")
    page_top = transform(*TOP_TO_PAGE)
    bands[0]["properties"]["transform"] = page_top
    bands[1]["properties"]["transform"] = transform(*BOTTOM_TO_PAGE)
    canvas = {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}
    content = group("content", {**canvas, "transform": transform(), "opacity": number(1)}, [
        group("navigation", {**absolute(left=0, top=U * NAV_TOP, width=U * 357), "display": keyword("flex"),
                             "flex-direction": keyword("column"), "opacity": number(1)}, buttons),
        group("details", {**absolute(left=U * 395, top=U * 190, width=U * DETAILS[0], height=U * DETAILS[1]), "opacity": number(0)},
              [description]),
    ])
    titles = group("titles", {**canvas, "pointer-events": keyword("none")}, [
        group("title-clip", {**absolute(left=U * TITLE_SLOT[0], top=0, width=U * PAGE_TITLE_W, height=U * 40), "overflow": keyword("hidden")}, [
            label("title", "#str_200009", {**absolute(left=0, top=U * TITLE_SLOT[1], width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                                           **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, 0.5]), "white-space": keyword("nowrap"),
                                           "transform": transform()})]),
    ])
    white = lambda alpha: colour([1, 1, 1, alpha])
    identity, swept = transform(), transform(640 * U, 0)
    doc.timelines.add("arrive", 150, [
        track("content", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
        track("content", "transform", [(0, identity), (1, identity), (150, identity)]),
        track("details", "opacity", [(0, number(0)), (1, number(0)), (150, number(1))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5)), (150, white(0.5))]),
        track("prompts", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
    ])
    doc.timelines.add("rest", 1, [
        track("band-top", "transform", [(0, page_top), (1, page_top)]),
        track("content", "transform", [(0, identity), (1, identity)]),
        track("content", "opacity", [(0, number(0)), (1, number(0))]),
        track("details", "opacity", [(0, number(0)), (1, number(0))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5))]),
        track("prompts", "opacity", [(0, number(1)), (1, number(1))]),
    ])
    doc.timelines.add("depart", DEPART_MS, [
        track("content", "opacity", [(0, number(1)), (150, number(0)), (DEPART_MS, number(0))]),
        track("content", "transform", [(0, identity), (DEPART_MS, swept)]),
        track("title", "color", [(0, white(0.5)), (150, white(0)), (DEPART_MS, white(0))]),
        track("prompts", "opacity", [(0, number(1)), (150, number(0)), (DEPART_MS, number(0))]),
    ])
    # Back from a section returns onto it (the session sets from_<section>
    # first, a stock panel's included); arriving from the home focuses
    # CONTROLS, the first.
    returns = [ident for ident, _key, action in SECTIONS if action is not None]
    for ident in returns:
        doc.state[f"from_{ident}"] = {"type": "boolean", "initial": False}
    doc.events["onActivate"] = [{"op": "playTimeline", "timeline": "arrive"}, {"op": "focus", "control": "controls"}] + [
        {"op": "if", "condition": {"state": f"from_{ident}"}, "then": [{"op": "focus", "control": ident}]} for ident in returns]
    doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}]
    doc.events["depart"] = [{"op": "playTimeline", "timeline": "depart"}]
    return fit_narrow(doc.build(group("screen", {**FULL, "background-color": colour([0, 0, 0, 1])}, [
        *lit_field("field", 0.7), *bands, content, titles,
        # The pages' prompt bar, as legends: the page's own BACK is its button.
        page_prompts(doc, in_canvas=False), defaults,
    ])))

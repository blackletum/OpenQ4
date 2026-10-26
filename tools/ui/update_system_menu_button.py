#!/usr/bin/env python3
"""Give the SYSTEM page the Menu button's Apply (visual specification 13.5).

The Menu button applies a screen's pending changes. The adapter runs a
document's onMenuButton where it declares one and otherwise treats the button
as Back. The SYSTEM page's onMenuButton runs its existing programs and adds
no operation of its own:

- in the display confirmation and the discard dialog, what Back does there
  (confirmationBack, continueEditing), so Menu never confirms a display
  change unseen or leaves through the dialog;
- with a typed number not yet committed, the focus moves to it, as Continue
  Editing does, since Apply waits for it;
- with pending changes, the page's apply event, whose own condition still
  decides whether Apply can run;
- otherwise onBack, so a clean page closes as it did.

This tool owns onMenuButton. --check verifies reproducible generation without
writing.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from update_system_number_fields import load

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('content/baseoq4/pak0/guis/menu/settings/system.q4ui')
EVENT = 'onMenuButton'
USES = ('onBack', 'apply', 'continueEditing', 'confirmationBack')


def call(event):
    return [{'op': 'call', 'event': event}]


def branch(condition, then, otherwise):
    return [{'op': 'if', 'condition': condition, 'then': then, 'else': otherwise}]


def compose(document):
    result = copy.deepcopy(document)
    events = result['events']
    missing = [name for name in USES if name not in events]
    if missing:
        raise ValueError('SYSTEM no longer declares ' + ', '.join(missing) + '; review the Menu button')
    if 'focusNumberDraft' not in result['actions']:
        raise ValueError('SYSTEM no longer declares focusNumberDraft; review the Menu button')
    program = branch({'state': 'settings.confirmationVisible'}, call('confirmationBack'),
              branch({'state': 'page.discardVisible'}, call('continueEditing'),
              branch({'state': 'ui.numberDraftsPending'}, [{'op': 'action', 'action': 'focusNumberDraft'}],
              branch({'state': 'settings.dirty'}, call('apply'), call('onBack')))))
    ordered = {}
    for name, steps in events.items():
        if name == EVENT:
            continue
        ordered[name] = steps
        if name == 'onBack':
            ordered[EVENT] = program
    result['events'] = ordered
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--source', type=Path, default=ROOT / SOURCE)
    parser.add_argument('--check', action='store_true'); args = parser.parse_args()
    prefix, document = load(args.source); result = compose(document)
    if args.check:
        if result != document or list(result['events']) != list(document['events']):
            raise SystemExit('SYSTEM onMenuButton differs from its Menu button definition')
    else: args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__': main()

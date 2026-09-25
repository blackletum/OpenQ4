#!/usr/bin/env python3
"""Author precise SYSTEM dimensions with the existing editable Number artwork.

Window fields edit one draft key. Custom fullscreen fields select Custom mode
and edit the dimension atomically; the service validates the complete tuple
again before Apply. Display and refresh catalogs remain separate requirements.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from update_system_presets import length, load, nodes, renamed, typed

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui'
FIELDS = (
    ('settings_window_width', 'r_windowWidth', '#str_229946', 320, 'WindowWidth'),
    ('settings_window_height', 'r_windowHeight', '#str_229947', 240, 'WindowHeight'),
    ('settings_custom_width', 'r_customWidth', '#str_229948', 320, 'CustomWidth'),
    ('settings_custom_height', 'r_customHeight', '#str_229949', 240, 'CustomHeight'),
)


def op(name, *args): return {'op': name, 'args': list(args)}
def state(key): return {'state': key}


def editable():
    # Local uncommitted Number text must remain editable. The adapter's existing
    # draft barrier blocks Apply/presets/toggles without disabling these fields.
    terms = [state('settings.open'), op('==', state('settings.phase'), 1)]
    terms += [op('!', state(key)) for key in ('settings.busy', 'settings.confirmationVisible', 'page.discardVisible')]
    result = terms[0]
    for term in terms[1:]: result = op('&&', result, term)
    return result


def compose(document):
    result = copy.deepcopy(document)
    index = nodes(result['root'])
    original = 'settings_brightness_number'
    fields = []
    bindings, timelines = [], []
    for ident, key, label, minimum, alias in FIELDS:
        custom = key.startswith('r_custom')
        field = renamed(index[original], original, ident)
        field['properties'].update({'width': length(100, '%'), 'min-width': length(0),
            'max-width': length(100, '%'), 'padding': length(12), 'margin-bottom': length(8)})
        label_node = copy.deepcopy(index['settings_brightness-label'])
        label_node['id'] = ident + '-label'
        label_node['properties'].update({'text': typed('text', label), 'width': length(100, '%'), 'margin-bottom': length(8)})
        plate = renamed(index['settings_brightness-plate'], 'settings_brightness', ident + '-outer')
        field['children'][0:0] = [plate, label_node]
        parts = nodes(field)
        parts[ident + '-validation']['properties']['text'] = typed('text', '#str_230021' if minimum == 320 else '#str_230022')
        action = ('size.' if custom else 'edit.') + key
        arguments = {key: {'input': 'value'}}
        if custom: arguments['r_mode'] = -1
        proposal = {'input': 'number', 'operation': 'settings.system.edit', 'arguments': arguments}
        if not custom and result['actions'].get(action) != proposal:
            raise ValueError('Review the existing dimension action: ' + key)
        result['actions'][action] = proposal
        field['control'].update(label=label, action=action, value=state('settings.draft.' + key),
            minimum=minimum, maximum=16384, exponent=False, integer=True, maxBytes=32)
        fields.append(field)
        available = op('!', state('settings.draft.' + ('r_fullscreenDesktop' if custom else 'r_fullscreen')))
        bindings.extend([
            {'id': ident + '.enabled', 'node': ident, 'property': 'enabled', 'value': op('&&', editable(), available)},
            {'id': ident + '.availabilityOpacity', 'node': ident, 'property': 'opacity', 'value': op('select', available, 1, .45)},
        ])
        timelines.extend(renamed(item, original, ident) for item in result['timelines'] if item['id'].startswith(original + '.'))
        for phase in ('draft', 'baseline'):
            variable = phase + alias
            result['presentationVariables'][variable] = {'type': 'number', 'initial': 0, 'value': state('settings.' + phase + '.' + key)}
            result['aliases'][variable] = {'variable': variable}
    for phase in ('draft', 'baseline'):
        variable = phase + 'Mode'
        result['presentationVariables'][variable] = {'type': 'number', 'initial': 0, 'value': state('settings.' + phase + '.r_mode')}
        result['aliases'][variable] = {'variable': variable}
    prefixes = tuple(ident + '.' for ident, *_ in FIELDS)
    for group, additions in [('bindings', bindings), ('timelines', timelines)]:
        result[group] = [item for item in result[group] if not item['id'].startswith(prefixes)]
        position = next(i for i, item in enumerate(result[group]) if item['id'].startswith('settings_fullscreen.'))
        result[group][position:position] = additions
    column = copy.deepcopy(index['display-column'])
    column['id'] = 'dimensions-column'
    title = copy.deepcopy(column['children'][0]); title['id'] = 'dimensions-title'
    title['properties']['text'] = typed('text', '#str_229943')
    hint = copy.deepcopy(index['settings-message']); hint['id'] = 'dimensions-hint'
    hint['properties'].update({'text': typed('text', '#str_230024'), 'width': length(100, '%'), 'margin-bottom': length(8)})
    column['children'] = [title, *fields[:2], hint, *fields[2:]]
    body = index['settings-body']
    body['children'] = [child for child in body['children'] if child['id'] != column['id']]
    position = next(i for i, child in enumerate(body['children']) if child['id'] == 'image-column')
    body['children'].insert(position, column)
    result['extensions']['openq4']['dimensionControls'] = {
        'source': 'Legacy custom-size entry selects r_mode=-1; new selection and dimension changes share one owned draft patch.',
        'validation': 'Exact decimal integers within renderer bounds; intermediate size tuples remain drafts and cannot Apply until the complete display request validates.',
        'availability': 'Window size is editable in a windowed draft. Custom fullscreen size is editable with Exclusive policy, including while preparing that policy in windowed mode.',
        'remaining': 'Dynamic display/resolution/refresh catalogs, visible fullscreen/platform qualification, native input and full SYSTEM/editor acceptance.'}
    nodes(result['root'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    prefix, document = load(args.source); result = compose(document)
    if args.check:
        if result != document: raise SystemExit('SYSTEM dimensions differ from their authoring definition')
    else: args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__': main()

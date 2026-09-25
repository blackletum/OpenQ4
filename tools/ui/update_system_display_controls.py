#!/usr/bin/env python3
"""Author SYSTEM display controls using its shared editable vector artwork.

Selections only edit the existing typed draft. Apply/Keep/Revert and display
recovery remain owned by the settings service, including unsupported requests.
Dynamic display/resolution catalogs are a separate outstanding part of SYSTEM.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from update_system_presets import allowed, length, load, nodes, renamed, typed

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui'
# id, CVar, label, source artwork, alias, option labels, exact typed values
FIELDS = (
    ('settings_fullscreen', 'r_fullscreen', '#str_200147', 'settings_shadows', 'Fullscreen', None, None),
    ('settings_borderless', 'r_borderless', '#str_229909', 'settings_shadows', 'Borderless', None, None),
    ('settings_fullscreen_policy', 'r_fullscreenDesktop', '#str_229910', 'settings_vsync', 'FullscreenPolicy', '#str_229911', (True, False)),
    ('settings_msaa', 'r_multiSamples', '#str_41093', 'settings_postaa', 'MSAA', '#str_230020', (0, 2, 4, 8, 16)),
)


def compose(document):
    result = copy.deepcopy(document)
    index = nodes(result['root'])
    controls = []
    result['state']['settings.msaaAvailable'] = {'type': 'boolean', 'initial': False}
    result['presentationVariables']['msaaAvailable'] = {
        'type': 'boolean', 'initial': False, 'value': {'state': 'settings.msaaAvailable'}}
    result['aliases']['msaaAvailable'] = {'variable': 'msaaAvailable'}
    for ident, key, label, original, alias, labels, options in FIELDS:
        field = renamed(index[original], original, ident)
        control = field['control']
        control.update(label=label, action='edit.' + key, value={'state': 'settings.draft.' + key})
        fi = nodes(field)
        fi[ident + '-label']['properties']['text'] = typed('text', label)
        kind = 'number' if key == 'r_multiSamples' else 'boolean'
        action = {'input': kind, 'operation': 'settings.system.edit', 'arguments': {key: {'input': 'value'}}}
        if result['actions'].get('edit.' + key) != action:
            raise ValueError('Review the existing display proposal contract: ' + key)
        if options is not None:
            option_template = copy.deepcopy(control['options'][0])
            row_template = copy.deepcopy(fi[ident + '-option-0'])
            control['options'] = []
            fi[ident + '-content']['children'] = []
            for i, value in enumerate(options):
                option_id = ident + '-option-' + str(i)
                option = renamed(option_template, ident + '-option-0', option_id)
                option.update(label=labels, labelIndex=i, value=value)
                control['options'].append(option)
                row = renamed(row_template, ident + '-option-0', option_id)
                nodes(row)[option_id + '-label']['properties']['text'] = typed('text', labels)
                fi[ident + '-content']['children'].append(row)
        controls.append(field)
        for phase in ('draft', 'baseline'):
            variable = phase + alias
            result['presentationVariables'][variable] = {
                'type': kind, 'initial': 0 if kind == 'number' else False,
                'value': {'state': 'settings.' + phase + '.' + key}}
            result['aliases'][variable] = {'variable': variable}
        for group in ('bindings', 'timelines'):
            result[group] = [entry for entry in result[group] if not entry['id'].startswith(ident + '.')]
            position = next(i for i, entry in enumerate(result[group]) if entry['id'].startswith('settings_ui_scale.'))
            available = ({'op': '&&', 'args': [allowed(), {'state': 'settings.msaaAvailable'}]}
                         if key == 'r_multiSamples' else allowed())
            entries = ([{'id': ident + '.enabled', 'node': ident, 'property': 'enabled', 'value': available}]
                       if group == 'bindings' else [renamed(entry, original, ident) for entry in result[group]
                                                   if entry['id'].startswith(original + '.')])
            if group == 'bindings' and key == 'r_multiSamples':
                entries.append({'id': ident + '.availabilityOpacity', 'node': ident, 'property': 'opacity',
                                'value': {'op': 'select', 'args': [{'state': 'settings.msaaAvailable'}, 1, .45]}})
            result[group][position:position] = entries
    column = copy.deepcopy(index['image-column'])
    column['id'] = 'display-column'
    title = copy.deepcopy(column['children'][0]); title['id'] = 'display-title'
    title['properties']['text'] = typed('text', '#str_229900')
    column['properties'].update({'width': length(20, 'em'), 'min-width': length(0), 'max-width': length(100, '%')})
    column['children'] = [title, *controls]
    body = index['settings-body']
    body['children'] = [child for child in body['children'] if child['id'] != 'display-column']
    position = next(i for i, child in enumerate(body['children']) if child['id'] in ('dimensions-column', 'image-column'))
    body['children'].insert(position, column)
    result['extensions']['openq4']['displayControls'] = {
        'scope': 'Fullscreen, borderless, fullscreen policy and MSAA edit the existing draft. Apply uses owned Keep/Revert recovery; unsupported device requests do not bypass validation.',
        'remaining': 'Dynamic display/resolution/refresh catalogs, dimension editors, full settings effects and screen acceptance remain required.',
        'msaaAvailability': 'Read-only active-backend observation. Vulkan and unavailable renderers disable MSAA; strict Apply still checks supported GL sample counts.',
        'localization': ['#str_229900', '#str_200147', '#str_229909', '#str_229910', '#str_229911', '#str_41093', '#str_230020']}
    nodes(result['root'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    prefix, document = load(args.source); result = compose(document)
    if args.check:
        if result != document: raise SystemExit('SYSTEM display controls differ from their authoring definition')
    else:
        args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__': main()

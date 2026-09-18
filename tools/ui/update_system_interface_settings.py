#!/usr/bin/env python3
"""Author transactional interface sizes from SYSTEM's existing vector controls."""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from update_system_presets import allowed, keyword, length, load, nodes, number, renamed, typed

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui'
FIELDS = (
    ('settings_ui_scale', 'ui_retainedScale', '#str_230015', ((.75, 3), (1, 5), (1.25, 6), (1.5, 7), (2, 8)), 'UIScale'),
    ('settings_text_scale', 'ui_retainedTextScale', '#str_230016', ((1, 5), (1.25, 6), (1.5, 7), (2, 8)), 'TextScale'),
)


def compose(document):
    result = copy.deepcopy(document)
    index = nodes(result['root'])
    controls = []
    originals = []
    for ident, key, label, options, alias in FIELDS:
        original = 'settings_resolution_scale'
        choice = renamed(index[original], original, ident)
        ci = nodes(choice)
        choice['properties'].update({'width': length(20, 'em'), 'min-width': length(0),
            'max-width': length(100, '%'), 'flex-grow': number(1), 'margin-bottom': length(0)})
        control = choice['control']
        control.update(label=label, action='edit.' + key, value={'state': 'settings.draft.' + key})
        option_template = copy.deepcopy(control['options'][0])
        row_template = copy.deepcopy(ci[ident + '-option-0'])
        control['options'] = []
        ci[ident + '-content']['children'] = []
        for number_index, (value, label_index) in enumerate(options):
            option_id = ident + '-option-' + str(number_index)
            option = renamed(option_template, ident + '-option-0', option_id)
            option.update(value=value, labelIndex=label_index)
            control['options'].append(option)
            ci[ident + '-content']['children'].append(renamed(row_template, ident + '-option-0', option_id))
        ci[ident + '-label']['properties']['text'] = typed('text', label)
        controls.append(choice)
        originals.append((ident, original))
        result['actions']['edit.' + key] = {'input': 'number', 'operation': 'settings.system.edit',
                                           'arguments': {key: {'input': 'value'}}}
        for phase in ('draft', 'baseline'):
            state = 'settings.' + phase + '.' + key
            result['state'][state] = {'type': 'number', 'initial': 1}
            name = phase + alias
            result['presentationVariables'][name] = {'type': 'number', 'initial': 1, 'value': {'state': state}}
            result['aliases'][name] = {'variable': name}

    reset = renamed(index['settings_autodetect'], 'settings_autodetect', 'settings_reset_sizes')
    reset['properties'].update({'width': length(100, '%'), 'min-width': length(0), 'flex-grow': number(0)})
    reset['control'].update(label='#str_230017', event='resetSizes')
    nodes(reset)['settings_reset_sizes-label']['properties']['text'] = typed('text', '#str_230017')
    originals.append(('settings_reset_sizes', 'settings_autodetect'))
    result['actions']['resetSizes'] = {'operation': 'settings.system.edit',
        'arguments': {'ui_retainedScale': 1, 'ui_retainedTextScale': 1}}
    result['events']['resetSizes'] = [{'op': 'if', 'condition': allowed(),
                                     'then': [{'op': 'action', 'action': 'resetSizes'}]}]
    title_template = next(child for child in index['image-column']['children'] if child['type'] == 'text')
    title = renamed(title_template, title_template['id'], 'interface-title')
    title['properties'].update({'text': typed('text', '#str_230018'), 'width': length(100, '%'),
                                'margin-bottom': length(0)})
    note = renamed(title, 'interface-title', 'interface-fit-note')
    note['properties'].update({'text': typed('text', '#str_230019'), 'font-size': length(14),
                              'line-height': length(20)})
    band = {'id': 'interface-band', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('flex'), 'box-sizing': keyword('border-box'),
        'width': length(100, '%'), 'min-width': length(0), 'flex-wrap': keyword('wrap'),
        'column-gap': length(16), 'row-gap': length(12)}, 'children': [title, reset, *controls, note]}
    body = index['settings-body']
    children = [child for child in body['children'] if child['id'] != 'interface-band']
    body['children'] = children[:1] + [band] + children[1:]
    for ident, original in originals:
        for group in ('bindings', 'timelines'):
            result[group] = [item for item in result[group] if not item['id'].startswith(ident + '.')]
            position = next(i for i, item in enumerate(result[group]) if item['id'].startswith('settings_preset.'))
            entries = ([{'id': ident + '.enabled', 'node': ident, 'property': 'enabled', 'value': allowed()}]
                       if group == 'bindings' else [renamed(item, original, ident) for item in result[group]
                                                   if item['id'].startswith(original + '.')])
            result[group][position:position] = entries
    result['extensions']['openq4']['interfaceSettings'] = {
        'scope': 'UI and text sizes edit the SYSTEM draft and take effect through Apply. Reset sizes changes only these two draft values.',
        'localization': ['#str_230015', '#str_230016', '#str_230017', '#str_230018', '#str_230019'],
        'catalogVersion': 2}
    nodes(result['root'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    prefix, document = load(args.source)
    result = compose(document)
    if args.check:
        if result != document:
            raise SystemExit('SYSTEM interface preferences differ from their authoring definition')
    else:
        args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()

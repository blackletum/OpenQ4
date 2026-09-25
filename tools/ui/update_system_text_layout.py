#!/usr/bin/env python3
"""Keep SYSTEM typography flowing at independent 100–200% text scale.

Only authoring sources are changed. Numeric geometry is owned by its existing
component generator; this pass owns the page's responsive text/column envelope.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
from update_system_number_fields import index_nodes, keyword, length, load
from update_system_presets import number, renamed

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'content/baseoq4/pak0/guis/menu/settings/system.q4ui'


def scrollbar(document, nodes, ident, viewport, label):
    original = 'settings_body_scrollbar'
    bar = renamed(nodes[original], original, ident)
    # Let the row stretch this item after intrinsic heading measurement. A
    # percentage height here feeds the entire dialog back into its own row.
    bar['properties']['height'] = keyword('auto')
    bar['control'].update({'viewport': viewport, 'label': label})
    timelines = [renamed(t, original, ident) for t in document['timelines'] if t['id'].startswith(original + '.')]
    document['timelines'] = [t for t in document['timelines'] if not t['id'].startswith(ident + '.')]
    position = next(i for i, t in enumerate(document['timelines']) if t['id'].startswith(original + '.'))
    document['timelines'][position:position] = timelines
    return bar


def modal_scroll(document, nodes, name, text_ids):
    """Keep decisions fixed while long headings/instructions scroll explicitly."""
    body_id = name + '-body'
    bar = scrollbar(document, nodes, name + '_body_scrollbar', body_id,
                    nodes[text_ids[0]]['properties']['text']['value'])
    body = {'id': body_id, 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('block'),
        'box-sizing': keyword('border-box'), 'width': length(0), 'min-width': length(0),
        'min-height': length(0), 'flex-grow': number(1), 'overflow': keyword('auto')},
        'children': [nodes[key] for key in text_ids]}
    wrapper = {'id': name + '-scroll-region', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('flex'),
        'box-sizing': keyword('border-box'), 'width': length(100, '%'),
        'height': keyword('auto'), 'min-height': length(0), 'flex-shrink': number(1),
        'overflow': keyword('hidden'),
        'column-gap': length(8), 'align-items': keyword('stretch'), 'margin-bottom': length(12)},
        'children': [body, bar]}
    nodes[text_ids[-1]]['properties']['margin-bottom'] = length(0)
    plate = nodes[name + '-panel-plate']
    plate['properties'].update({'display': keyword('flex'), 'flex-direction': keyword('column'),
                                 'width': length(35, 'em'), 'max-height': length(100, '%'),
                                 'padding': length(16)})
    actions = nodes[name + '-actions']
    actions['properties'].update({'flex-shrink': number(0), 'column-gap': length(8)})
    plate['children'] = [wrapper, actions]


def compose(document):
    result = copy.deepcopy(document)
    nodes = index_nodes(result['root'])
    result['root']['properties']['word-break'] = keyword('break-word')
    for name in ('image-column', 'render-column'):
        nodes[name]['properties'].update({'width': length(20, 'em'), 'min-width': length(0),
                                         'max-width': length(100, '%')})
    # The fixed actions can wrap, but may not be displaced by a long status.
    nodes['settings-actions']['properties'].update({'width': length(26, 'em'),
        'min-width': length(0), 'max-width': length(100, '%')})
    # The header may scroll when a translated status is long. Reserve enough
    # main-body height to edit a whole field or read a dropdown row, and retain
    # one complete title line even at the combined density/text extreme.
    header_body = {'id': 'settings-header-body', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('block'),
        'box-sizing': keyword('border-box'), 'width': length(0), 'min-width': length(0),
        'min-height': length(0), 'flex-grow': number(1), 'overflow': keyword('auto'),
        },
        'children': [nodes['settings-title'], nodes['settings-message']]}
    header = {'id': 'settings-header', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('flex'),
        'box-sizing': keyword('border-box'), 'width': length(100, '%'),
        'height': keyword('auto'), 'min-height': length(2.125, 'em'),
        'flex-shrink': number(1), 'overflow': keyword('hidden'),
        'align-items': keyword('stretch'), 'column-gap': length(8)},
        'children': [header_body, scrollbar(result, nodes, 'settings_header_scrollbar',
                       'settings-header-body', nodes['settings-title']['properties']['text']['value'])]}
    nodes['settings-title']['properties'].pop('max-width', None)
    for key in ('width', 'min-width', 'max-width', 'flex-grow', 'flex-shrink'):
        nodes['settings-message']['properties'].pop(key, None)
    nodes['settings-message']['properties']['text-align'] = keyword('left')
    nodes['settings-footer']['children'] = [nodes['settings-actions']]
    nodes['settings-footer']['properties']['justify-content'] = keyword('flex-end')
    nodes['settings-scroll-region']['properties'].update({'min-height': length(3, 'em'),
        'margin-top': length(4), 'margin-bottom': length(4)})
    panel = nodes['settings-panel']
    # The rail follows layout; an absolute y=64 dp line crossed enlarged text.
    panel['paths'] = [path for path in panel['paths'] if path['id'] != 'header-rail']
    rail = {'id': 'settings-header-rail', 'type': 'vector', 'properties': {
        'position': keyword('relative'), 'display': keyword('block'),
        'width': length(100, '%'), 'height': length(2), 'flex-shrink': number(0),
        'pointer-events': keyword('none')}, 'paths': [{'id': 'header-rail', 'commands': [
            {'id': 'point-0', 'op': 'move', 'points': [[0, 0]]},
            {'id': 'point-1', 'op': 'line', 'points': [[{'fraction': 1, 'dp': 0}, 0]]},
            {'id': 'point-2', 'op': 'line', 'points': [[{'fraction': 1, 'dp': -2}, 2]]},
            {'id': 'point-3', 'op': 'line', 'points': [[0, 2]]},
            {'id': 'close', 'op': 'close'}],
            'fill': {'type': 'solid', 'color': {'type': 'color', 'value': [.5451, .5882, .2941, .7]}}}]}
    panel['children'] = [header, rail] + [child for child in panel['children']
        if child['id'] not in ('settings-title', 'settings-header', 'settings-header-rail')]
    modal_scroll(result, nodes, 'discard', ('discard-panel-title',))
    modal_scroll(result, nodes, 'confirmation', ('confirmation-panel-title', 'confirmation-message', 'confirmation-countdown'))
    for name in ('discard-actions', 'confirmation-actions'):
        for button in nodes[name]['children']:
            if 'control' in button:
                button['properties'].update({'width': length(8 if name == 'confirmation-actions' else 10, 'em'), 'min-width': length(0),
                                              'max-width': length(100, '%'), 'margin-right': length(0)})
                nodes[button['id'] + '-label']['properties']['word-break'] = keyword('normal')
    for node in nodes.values():
        control = node.get('control', {})
        if control.get('role') == 'choice':
            selected = nodes[control['parts']['value']]['properties']
            selected.pop('height', None)
            selected['min-height'] = length(23)
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
            raise SystemExit('SYSTEM text layout differs from its responsive authoring definition')
    else:
        args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n',
                               encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Rendering backend/device controls in the shared SYSTEM performance band."""
import copy

SLOTS = 32


def compose(document, helpers):
    nodes, renamed, typed, length, number, keyword, allowed = helpers
    index = nodes(document['root'])
    band = index['performance-band']
    state = document['state']
    kinds = {'catalog': 'string', 'count': 'number', 'selected': 'number', 'active': 'string',
             'pending': 'boolean', 'autoDetect': 'boolean', 'canRestart': 'boolean',
             'canChoose': 'boolean', 'vulkan': 'boolean', 'gl': 'boolean', 'gles': 'boolean',
             'notice': 'string', 'backend': 'string'}
    for i in range(SLOTS):
        kinds[f'{i}.label'] = 'string'
        kinds[f'{i}.usable'] = 'boolean'
    for key, kind in kinds.items():
        state['settings.render.' + key] = {'type': kind, 'initial': '' if kind == 'string' else False if kind == 'boolean' else -1 if key == 'selected' else 0}
    for prefix in ('draft', 'baseline'):
        for key, value in (('r_renderApi', 'best'), ('r_renderDevice', 'auto')):
            state[f'settings.{prefix}.{key}'] = {'type': 'string', 'initial': ''}

    def st(key): return {'state': 'settings.render.' + key}
    def op(name, *args): return {'op': name, 'args': list(args)}
    def bind(ident, prop, value, suffix=None):
        document['bindings'].append({'id': ident + '.' + (suffix or prop), 'node': ident, 'property': prop, 'value': value})

    # Rebuild just these authored nodes/feedback on every pass.
    owned = ('settings_backend', 'settings_gpu', 'settings_restart_renderer', 'rendering-active', 'rendering-notice')
    for group in ('bindings', 'timelines'):
        document[group] = [entry for entry in document[group] if not entry['id'].startswith(owned)]

    def choice(ident, label, action, value, entries):
        field = renamed(copy.deepcopy(index['settings_preset']), 'settings_preset', ident)
        parts = nodes(field)
        control = field['control']
        control.update(label=label, action=action, value=value, visibleRows=8)
        parts[ident + '-label']['properties']['text'] = typed('text', label)
        template = copy.deepcopy(control['options'][0])
        row = copy.deepcopy(parts[ident + '-option-0'])
        control['options'] = []
        parts[ident + '-content']['children'] = []
        for i, entry in enumerate(entries):
            target = ident + '-option-' + str(i)
            option = renamed(template, ident + '-option-0', target)
            option.pop('labelIndex', None)
            option.update(entry)
            control['options'].append(option)
            child = renamed(row, ident + '-option-0', target)
            nodes(child)[target + '-label']['properties']['text'] = typed('text', label)
            parts[ident + '-content']['children'].append(child)
        for key in ('popup', 'viewport'):
            parts[ident + '-' + key]['properties']['height'] = length(min(8, len(entries)) * 36)
        document['timelines'] += [renamed(t, 'settings_preset', ident) for t in document['timelines'] if t['id'].startswith('settings_preset.')]
        return field

    backend = choice('settings_backend', '#str_230141', 'render.backend', st('backend'), [
        {'label': '#str_230143', 'labelIndex': i, 'value': api,
         'enabled': True if api == 'best' else st(api)}
        for i, api in enumerate(('best', 'gl', 'vulkan', 'gles'))])
    gpu = choice('settings_gpu', '#str_230142', 'render.device', st('selected'),
        [{'label': '#str_230144', 'value': -1}] +
        [{'label': st(f'{i}.label'), 'value': i, 'enabled': st(f'{i}.usable')} for i in range(SLOTS)] +
        [{'label': '#str_230154', 'value': SLOTS, 'enabled': False}])
    gpu['control']['optionCount'] = op('+', st('count'), 1)
    bind('settings_backend', 'enabled', allowed())
    bind('settings_gpu', 'enabled', op('&&', allowed(), st('canChoose')))
    bind('settings_backend', 'opacity', op('select', allowed(), 1, .4))
    bind('settings_gpu', 'opacity', op('select', op('&&', allowed(), st('canChoose')), 1, .4))
    document['actions']['render.backend'] = {'input': 'string', 'operation': 'settings.system.backend',
                                            'arguments': {'api': {'input': 'value'}}}
    document['actions']['render.device'] = {'input': 'number', 'operation': 'settings.system.renderDevice',
                                           'arguments': {'index': {'input': 'value'}, 'catalog': st('catalog')}}

    auto_allowed = op('&&', allowed(), st('autoDetect'))
    next(b for b in document['bindings'] if b['id'] == 'settings_autodetect.enabled')['value'] = auto_allowed
    document['events']['autoDetect'][0]['condition'] = auto_allowed
    document['bindings'] = [b for b in document['bindings'] if b['id'] != 'settings_autodetect.opacity']
    bind('settings_autodetect', 'opacity', op('select', auto_allowed, 1, .4))

    def text(ident, label):
        node = renamed(copy.deepcopy(index['dimensions-hint']), 'dimensions-hint', ident)
        node['properties'].update({'text': typed('text', label), 'width': length(100, '%'),
                                   'white-space': keyword('normal'), 'margin-top': length(0), 'margin-bottom': length(0)})
        return node
    active = text('rendering-active', '#str_230152')
    notice = text('rendering-notice', '#str_230145')
    bind('rendering-active', 'text', st('active'))
    bind('rendering-notice', 'text', st('notice'))

    restart = renamed(copy.deepcopy(index['settings_autodetect']), 'settings_autodetect', 'settings_restart_renderer')
    restart['control'].update(label='#str_230153', event='restartRenderer')
    nodes(restart)['settings_restart_renderer-label']['properties']['text'] = typed('text', '#str_230153')
    restart_allowed = op('&&', allowed(), st('canRestart'))
    bind('settings_restart_renderer', 'enabled', restart_allowed)
    bind('settings_restart_renderer', 'opacity', op('select', restart_allowed, 1, .4))
    bind('settings_restart_renderer', 'display', op('select', st('pending'), 'flex', 'none'))
    document['actions']['restartRenderer'] = {'operation': 'settings.system.restartRenderer', 'arguments': {}}
    document['events']['restartRenderer'] = [{'op': 'if', 'condition': restart_allowed,
                                             'then': [{'op': 'action', 'action': 'restartRenderer'}]}]
    document['timelines'] += [renamed(t, 'settings_autodetect', 'settings_restart_renderer')
                              for t in document['timelines'] if t['id'].startswith('settings_autodetect.')]
    band['children'] = [backend, gpu] + band['children'] + [restart, active, notice]
    return document

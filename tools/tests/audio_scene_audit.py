#!/usr/bin/env python3
"""Audit authored speech in real campaign scenes: dialogue routing, Dialogue Volume and subtitles.

One hidden single-player client plays the opening of each map in turn with its
cinematics, AI and scripts running and s_logSpeech on: every start that may be
speech (classified dialogue, flagged voice-over, the radio channel or declared
by a lipSync) is logged with the routing the mixer gives it and the transcript
status the subtitle overlay has for it. Periodic probes list the audible voices
and the active subtitle cues, first with Dialogue Volume at 1 and then muted.

Checks: declared or flagged speech always follows Dialogue Volume; muting it
silences every dialogue voice while other sounds continue; every cue shown has
a transcript. The report keeps shader and sample identifiers and counts, never
transcript text. No physical device, microphone or host input is used.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from mp_round_remote_smoke import verify_runtime_is_current

ROOT = Path(__file__).resolve().parents[2]
STOCK_MAPS = ['game/airdefense1', 'game/airdefense2', 'game/hangar1', 'game/hangar2', 'game/mcc_landing',
              'game/medlabs', 'game/walker', 'game/dispersal', 'game/recomp', 'game/mcc_1', 'game/convoy1',
              'game/building_b', 'game/convoy2', 'game/convoy2b', 'game/hub1', 'game/hub2', 'game/mcc_2',
              'game/storage1', 'game/storage2', 'game/tram1', 'game/tram1b', 'game/process1', 'game/waste',
              'game/process2', 'game/network1', 'game/network2', 'game/putra', 'game/core1', 'game/core2']
AWAKENING_MAPS = ['game/m01_stranarus_trench1', 'game/m01_stranarus_trench2', 'game/m02_trianfac',
                  'game/m03_airassault', 'game/m04_prison', 'game/m05_bio', 'game/m06_mcc', 'game/m06_mcc_invasion',
                  'game/m07_race', 'game/m07_race1', 'game/m07_race2', 'game/m08_cryofac', 'game/m09_valkaryne']
SPEECH = re.compile(r'^SOUND_SPEECH game=(\d) time=(-?\d+) emitter=(\d+) speaker=(-?\d+) channel=(-?\d+) shader=(\S+) '
                    r'sample=(\S+) dialogue=(\d) authored=(\d) vo=(\d) radio=(\d) music=(\d) global=(\d) private=(\d) '
                    r'declared=(\d) transcript=(\w+)', re.M)
FIELDS = ('game', 'time', 'emitter', 'speaker', 'channel', 'shader', 'sample', 'dialogue', 'authored', 'vo', 'radio',
          'music', 'global', 'private', 'declared', 'transcript')
ROW = re.compile(r'^\s*\d+\s+\d+\s+([\d.]+)\s+([\d.]+)\s+(\S+)', re.M)
CUE = re.compile(r'^Subtitle shader=(\S+) emitter=\d+ speaker=\d+ time=\d+ duration=\d+ radio=\d textBytes=\d+', re.M)
PROBE = re.compile(r'^SCENE_PROBE (\S+) (\d+) (full|mute)\s*$', re.M)
ADMISSION = re.compile(r'^Audio admission: reserved=(\d+) KiB peak=(\d+) KiB budget=(\d+) MiB refused=(\d+)', re.M)
MEMORY = re.compile(r'^Audio memory: CPU=(\d+) KiB OpenAL=(\d+) KiB pending-input<=(\d+) KiB aggregate<=(\d+) KiB', re.M)
STREAMING = re.compile(r'^Vorbis streaming: reserved=(\d+) KiB', re.M)
VOICE = re.compile(r'^SOUND_VOICE source=\d+ state=(\w+) routed=\d queued=(\d) serial=\d+ offsetMs=[\d.-]+ sample=(\S+)', re.M)


def sample_key(name):
    """Canonical sample identity: lower case, forward slashes, no extension."""
    name = name.replace('\\', '/').lower()
    stem, dot, extension = name.rpartition('.')
    return stem if dot and '/' not in extension else name


def scene_entries(maps, repeat):
    """(label, map) per opening; a repeated sweep labels each pass's openings passN/map.
    The console's echo splits punctuation such as '#' into separate words, so labels
    keep to letters, digits and slashes, as map names do."""
    return [(f'pass{run}/{name}' if repeat > 1 else name, name) for run in range(1, repeat + 1) for name in maps]


def scene_cfgs(entries, seconds, interval):
    probes = max(1, (seconds * 1000) // (interval + 150))
    files = []
    for index, (name, _) in enumerate(entries):
        steps = ['god', f'echo SCENE_BEGIN {name}']
        for probe in range(probes):
            steps += [f'waitMsec {interval}', f'echo SCENE_PROBE {name} {probe} full', 'listPlayingSounds', 'listSubtitles',
                      'set s_dialogueVolume 0', 'waitMsec 150', f'echo SCENE_PROBE {name} {probe} mute', 'listPlayingSounds',
                      'set s_dialogueVolume 1']
        # Residency after the opening: a filter no sample matches prints only the totals.
        # The voice report shows which streamed tracks are playing on bounded queues.
        steps += [f'echo SCENE_MEMORY {name}', 'listSounds openq4_residency_totals_only', 'listSoundVoices',
                  f'echo SCENE_END {name}']
        if index + 1 < len(entries):
            # Re-armed in the same command pass as the load, so it fires on the next map only.
            steps += [f'set g_autoExecAfterMapLoad "scene-{index + 1}.cfg"', f'map {entries[index + 1][1]}']
        else:
            steps += ['set g_autoExecAfterMapLoad ""', 'echo SCENE_COMPLETE', 'quit']
        files.append('\n'.join(steps) + '\n')
    return files


def launch(args, root, entries):
    game_dir = 'q4xbase' if args.awakening else 'baseoq4'
    game = root / 'save' / game_dir
    game.mkdir(parents=True, exist_ok=True)
    for index, text in enumerate(scene_cfgs(entries, args.seconds, args.interval)):
        (game / f'scene-{index}.cfg').write_text(text, encoding='utf-8')
    config = root / 'alsoft.conf'
    config.write_text('[general]\ndrivers=wave\nchannels=stereo\nhrtf=false\n[wave]\nfile=' + str(root / 'mix.wav') + '\n',
                      encoding='utf-8')
    cvars = {
        'fs_basepath': str(args.assets.resolve(strict=True)), 'fs_savepath': str(root / 'save'), 'fs_devpath': str(root / 'save'),
        'fs_game': game_dir, 'com_gameMode': 'SP', 'si_gameType': 'singleplayer', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
        'win_allowMultipleInstances': '1', 'r_hiddenWindow': '1', 'r_windowWidth': '1280', 'r_windowHeight': '720',
        'r_mode': '-1', 'r_customWidth': '1280', 'r_customHeight': '720', 'r_renderApi': 'gl', 'r_multiSamples': '0',
        'r_swapInterval': '1', 'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0',
        's_noSound': '0', 's_muteUnfocused': '0', 'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '0',
        's_numberOfSpeakers': '2', 's_openALHRTF': '0', 's_dialogueVolume': '1', 's_logSpeech': '1', 'ui_subtitles': '1',
        'com_skipLoadingContinue': '1', 'com_maxfps': '60', 'ui_autoJoin': '0', 'ui_retained': '1', 'sys_lang': 'english',
        'g_autoExecAfterMapLoad': 'scene-0.cfg', 'g_autoExecAfterMapLoadDelayMs': '2000',
    }
    if args.awakening:
        cvars['fs_awakeningpath'] = str(args.awakening.resolve(strict=True))
    exe = ROOT / '.install' / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    command = [str(exe)]
    for key, value in cvars.items():
        command += ['+set', key, value]
    command += ['+map', entries[0][1]]
    options = {'env': {**os.environ, 'ALSOFT_CONF': str(config), 'ALSOFT_DRIVERS': 'wave'}}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    timeout = args.timeout or 90 + len(entries) * (args.seconds + 60)
    started = time.monotonic()
    with (root / 'process.log').open('w') as out:
        try:
            code = subprocess.run(command, cwd=ROOT / '.install', stdout=out, stderr=subprocess.STDOUT,
                                  timeout=timeout, **options).returncode
        except subprocess.TimeoutExpired:
            code = None  # a map that failed to load stalls the chain; report what ran
    path = game / 'logs/openq4.log'
    log = path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''
    return code, log, command, round(time.monotonic() - started, 1)


def audit(log, labels):
    """Per opening: its speech starts and probes, from the log segment ending at its SCENE_END."""
    scenes = {}
    begin = 0
    for name in labels:
        marker = re.search(rf'^SCENE_END {re.escape(name)}\s*$', log[begin:], re.M)
        segment = log[begin:begin + marker.end()] if marker else log[begin:]
        if marker:
            begin += marker.end()
        speech = [dict(zip(FIELDS, m)) for m in SPEECH.findall(segment)]
        speech = [s for s in speech if s['game'] == '1']
        dialogue_samples = {sample_key(s['sample']) for s in speech if s['dialogue'] == '1'}
        text_shaders = {s['shader'].lower() for s in speech if s['transcript'] == 'text'}
        mapped_samples = {sample_key(s['sample']): s['shader'].lower() for s in speech
                          if s['dialogue'] == '1' and s['transcript'] == 'text'}
        probes = {}
        marks = list(PROBE.finditer(segment))
        for i, m in enumerate(marks):
            body = segment[m.end():marks[i + 1].start() if i + 1 < len(marks) else len(segment)]
            probes[(int(m[2]), m[3])] = {'playing': [sample_key(r[2]) for r in ROW.findall(body)],
                                        'cues': [c.lower() for c in CUE.findall(body)]}
        misrouted = sorted({f"{s['shader']} {s['sample']}" for s in speech
                            if (s['declared'] == '1' or s['authored'] == '1') and s['music'] == '0' and s['dialogue'] == '0'})
        mute_leaks, effects_while_muted, heard, cue_without_text = [], 0, 0, []
        reach, missed = 0, []
        for (index, kind), probe in sorted(probes.items()):
            playing = set(probe['playing'])
            if kind == 'mute':
                mute_leaks += sorted(playing & dialogue_samples)
                effects_while_muted += 1 if playing - dialogue_samples else 0
                continue
            heard += 1 if playing & dialogue_samples else 0
            cue_without_text += [c for c in probe['cues'] if c not in text_shaders]
            for sample in playing & set(mapped_samples):
                if mapped_samples[sample] in probe['cues']:
                    reach += 1
                elif len(probe['cues']) < 2:  # the overlay shows the two newest utterances
                    missed.append(sample)
        statuses = Counter(s['transcript'] for s in speech if s['dialogue'] == '1')
        memory = {}
        at = re.search(rf'^SCENE_MEMORY {re.escape(name)}\s*$', segment, re.M)
        if at:
            tail = segment[at.end():]
            # The totals cover every sample; the filter only empties the per-sample listing.
            admission, totals, streaming = ADMISSION.search(tail), MEMORY.search(tail), STREAMING.search(tail)
            if admission and totals:
                queued = VOICE.findall(tail)
                memory = {'reserved_kib': int(admission[1]), 'process_peak_kib': int(admission[2]),
                          'budget_mib': int(admission[3]), 'refused_total': int(admission[4]),
                          'cpu_kib': int(totals[1]), 'openal_kib': int(totals[2]), 'aggregate_kib': int(totals[4]),
                          'stream_reserved_kib': int(streaming[1]) if streaming else None,
                          'voices_in_use': len(queued),
                          'streams_playing': sorted(sample_key(v[2]) for v in queued if v[1] == '1' and v[0] == 'playing'),
                          'streams_not_playing': sorted(sample_key(v[2]) for v in queued if v[1] == '1' and v[0] != 'playing')}
        scenes[name] = {
            'loaded': bool(marker),
            'memory': memory,
            'speech_starts': len(speech),
            'dialogue_starts': sum(s['dialogue'] == '1' for s in speech),
            'unique_dialogue_shaders': len({s['shader'].lower() for s in speech if s['dialogue'] == '1'}),
            'radio_starts': sum(s['radio'] == '1' for s in speech),
            'radio_not_dialogue': sorted({s['shader'] for s in speech if s['radio'] == '1' and s['dialogue'] == '0'}),
            'vo_flag_only': sorted({s['shader'] for s in speech if s['vo'] == '1' and s['dialogue'] == '0' and s['declared'] == '0'}),
            'transcripts': dict(statuses),
            'untranscribed_shaders': sorted({s['shader'] for s in speech if s['dialogue'] == '1' and s['transcript'] != 'text'}),
            'misrouted_speech': misrouted,
            'probes': len(probes) // 2,
            'dialogue_heard_at_full': heard,
            'mute_leaks': sorted(set(mute_leaks)),
            'effects_while_muted': effects_while_muted,
            'cues_without_transcript': sorted(set(cue_without_text)),
            'mapped_lines_shown': reach,
            'mapped_lines_missed': sorted(set(missed)),
        }
    return scenes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True, help='Read-only Quake 4 install root (the folder holding q4base)')
    parser.add_argument('--awakening', type=Path, help='Read-only Awakening content root (the folder holding q4xbase)')
    parser.add_argument('--output', type=Path, required=True, help='A new directory under the project .tmp')
    parser.add_argument('--maps', help='Comma-separated maps (default: every campaign map of the game)')
    parser.add_argument('--seconds', type=int, default=30, help='Seconds of each opening to audit')
    parser.add_argument('--interval', type=int, default=3000, help='Milliseconds between probes')
    parser.add_argument('--repeat', type=int, default=1, help='Play the maps this many times in one client (a long session)')
    parser.add_argument('--timeout', type=int, default=0)
    args = parser.parse_args()
    args.repeat = max(1, args.repeat)
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('choose a new output directory under the project .tmp')
    verify_runtime_is_current(ROOT / '.install')
    maps = [m.strip() for m in args.maps.split(',')] if args.maps else (AWAKENING_MAPS if args.awakening else STOCK_MAPS)
    entries = scene_entries(maps, args.repeat)
    labels = [label for label, _ in entries]
    output.mkdir(parents=True)
    code, log, command, seconds = launch(args, output, entries)
    scenes = audit(log, labels)
    loaded = [name for name, scene in scenes.items() if scene['loaded']]
    total = Counter()
    for scene in scenes.values():
        total.update({'speech_starts': scene['speech_starts'], 'dialogue_starts': scene['dialogue_starts'],
                      'radio_starts': scene['radio_starts'], 'probes': scene['probes'],
                      'mapped_lines_shown': scene['mapped_lines_shown']})
        total.update({'transcript_' + key: value for key, value in scene['transcripts'].items()})
    checks = {
        'complete': code == 0 and 'SCENE_COMPLETE' in log and len(loaded) == len(entries),
        'speech_heard': total['dialogue_starts'] > 0,
        'declared_speech_is_dialogue': all(not s['misrouted_speech'] for s in scenes.values()),
        'muted_dialogue_silent': all(not s['mute_leaks'] for s in scenes.values()),
        'effects_continue_while_muted': all(s['effects_while_muted'] > 0 for s in scenes.values() if s['loaded']),
        'dialogue_audible_at_full': sum(s['dialogue_heard_at_full'] for s in scenes.values()) > 0,
        'cues_have_transcripts': all(not s['cues_without_transcript'] for s in scenes.values()),
        # Every opening, and the level changes between them, fit the default budget.
        'residency_recorded': all(s['memory'] for s in scenes.values() if s['loaded']),
        'no_audio_refusals': all(s['memory'].get('refused_total', 1) == 0 for s in scenes.values() if s['loaded']),
        # Every map after the first starts its streams on recycled voices, whose
        # sources must start over to play (streams_not_playing is reported only:
        # an underrun on a loaded machine can catch one between restarts).
        'streams_play_on_recycled_voices': len(entries) < 2 or
            any(s['memory'].get('streams_playing') for s in list(scenes.values())[1:]),
    }
    if args.repeat > 1:
        # A long session: an opening played again holds no more audio, voices or
        # stream reservations than its first pass, beyond what one opening varies by.
        growth = {}
        for name in maps:
            passes = [scenes[f'pass{run}/{name}']['memory'] for run in range(1, args.repeat + 1)]
            if not all(passes):
                continue
            first = passes[0]
            growth[name] = {
                'reserved_kib': [m['reserved_kib'] for m in passes],
                'voices_in_use': [m['voices_in_use'] for m in passes],
                'stream_reserved_kib': [m['stream_reserved_kib'] for m in passes],
                'stable': all(m['reserved_kib'] <= first['reserved_kib'] + max(8192, first['reserved_kib'] // 10) and
                              m['voices_in_use'] <= first['voices_in_use'] + 8 and
                              (m['stream_reserved_kib'] or 0) <= (first['stream_reserved_kib'] or 0) + 8192 for m in passes[1:]),
            }
        checks['repeat_holds_steady'] = len(growth) == len(maps) and all(g['stable'] for g in growth.values())
        total['repeat_growth'] = growth
    residency = [s['memory'] for s in scenes.values() if s['memory']]
    if residency:
        largest = max(scenes, key=lambda name: scenes[name]['memory'].get('reserved_kib', -1))
        total.update({'largest_reserved_kib': scenes[largest]['memory']['reserved_kib'],
                      'process_peak_kib': max(m['process_peak_kib'] for m in residency),
                      'budget_mib': residency[-1]['budget_mib']})
        total['largest_map'] = largest
    exe = ROOT / '.install' / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64')
    report = {
        'passed': all(checks.values()), 'checks': checks, 'totals': dict(total), 'scenes': scenes,
        'maps': maps, 'repeat': args.repeat, 'seconds_per_map': args.seconds, 'probe_interval_ms': args.interval, 'run_seconds': seconds,
        'game': 'q4xbase' if args.awakening else 'baseoq4', 'returncode': code, 'command': command,
        'microphone': False, 'host_input': False, 'output': 'OpenAL wave writer', 'transcripts_recorded': False,
        'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
        'runtime_sha256': {str(p.relative_to(ROOT / '.install')): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in (ROOT / '.install/baseoq4').iterdir()
                           if p.is_file() and p.suffix.lower() in ('.dll', '.so', '.dylib', '.pk4')},
        'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'checks': checks, 'totals': dict(total), 'loaded': len(loaded), 'openings': len(entries)}, indent=2))
    print(json.dumps({'result': str(output / 'result.json'), 'passed': report['passed']}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())

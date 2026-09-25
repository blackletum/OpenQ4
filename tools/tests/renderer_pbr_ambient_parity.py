#!/usr/bin/env python3
"""Validate ambient radiance independently, with an optional admitted GL pair."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageFilter

import renderer_vulkan_pbr_ambient as capture


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def linear(value):
    value /= 255
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def compare(paths):
    reports = [json.loads(path.read_text()) for path in paths]
    paired = len(paths) == 2
    backends = ('gl', 'vk') if paired else ('vk',)
    failures, checks, captures = [], {}, []
    for key in ('fixture', 'compiledMapSHA256', 'harnessSHA256', 'ambientHarnessSHA256',
                'requestedSamples', 'basepath'):
        if paired and reports[0][key] != reports[1][key]:
            raise ValueError('incompatible paired provenance: ' + key)
    # An unchanged GL library may qualify both sides of a native revision.
    # Retain that revision difference explicitly; no other runtime input may
    # change, including the client, GL renderer and game modules.
    runtimes = [report['runtimeSHA256'] for report in reports]
    if paired and runtimes[0].keys() != runtimes[1].keys():
        raise ValueError('incompatible runtime file sets')
    revisions = {name: [runtime[name] for runtime in runtimes] for name in runtimes[0]
                 if runtimes[0][name] != runtimes[1][name]} if paired else {}
    if any(not Path(name).name.startswith('renderer-vk') for name in revisions):
        raise ValueError('runtime inputs changed outside the native renderer')
    samples = reports[0]['requestedSamples']
    if samples not in (0, 4):
        raise ValueError('unknown sample count')
    expected_profile = capture.configure(reports[0]['fixture'], samples)
    for backend, report in zip(backends, reports):
        if not report['complete'] or not report['runtimeUnchanged']:
            raise ValueError('incomplete or changed runtime: ' + backend)
        expected = dict(expected_profile)
        if backend == 'gl':
            expected.pop('ambient-alpha-fault')
        if report['ambientProfile'] != expected:
            raise ValueError('capture does not contain the required controls: ' + backend)
        images = {}
        for row in report['results']:
            name = row['case'].removeprefix('ambient-')
            if row['backend'] != backend or row['failures']:
                raise ValueError(f'{backend}/{name}: unqualified capture {row["failures"]}')
            for kind in ('screenshot', 'log'):
                if digest(row[kind]) != row['sha256'][kind]:
                    raise ValueError(f'{backend}/{name}: changed {kind}')
            telemetry = '\n'.join(row['telemetry'])
            if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b', telemetry):
                raise ValueError(f'{backend}/{name}: missing actual MSAA proof')
            images[name] = np.asarray(Image.open(row['screenshot']).convert('RGB'), dtype=np.int16)
            if images[name].shape != (800, 1280, 3):
                raise ValueError(f'{backend}/{name}: invalid extent')
            if backend == 'vk' and name.startswith('alpha-') and name != 'alpha-classic':
                spec = expected['ambient-' + name]
                count = 2 if spec['double'] else spec['lights']
                state = dict(re.findall(r'(\w+)=([^ ]+)', next(
                    (s for s in row['telemetry'] if s.startswith('Vulkan: native PBR transparency:')), '')))
                wanted = {'admitted': '0' if spec['fault'] else '1',
                          'reason': 'direct-geometry' if spec['fault'] else 'ready',
                          'requiredAtLeast': str(count), 'ready': '0' if spec['fault'] else '1',
                          'recorded': '0' if spec['fault'] else str(count),
                          'surfaces': '0' if spec['fault'] else '1',
                          'composites': str(0 if spec['fault'] or spec['debug'] else count)}
                if any(state.get(key) != value for key, value in wanted.items()):
                    failures.append(f'{backend}/{name}: incorrect complete ambient ownership')
                checks[backend + '/' + name + '/ownership'] = {'expected': wanted, 'actual': state}
        if len(report['results']) != len(expected) or set(images) != {n.removeprefix('ambient-') for n in expected}:
            raise ValueError('missing or duplicate controls: ' + backend)
        captures.append(images)

    # Use a fully covered interior separately from the raw full-frame result:
    # GL and Vulkan use reflected 4x sample positions on the qualified GPU.
    def interior(image):
        mask = (image[:, :, 0] == 0) & (image[:, :, 1] == 255) & (image[:, :, 2] == 0)
        return np.asarray(Image.fromarray(mask.astype(np.uint8) * 255).filter(ImageFilter.MinFilter(7))) == 255

    for backend, images in zip(backends, captures):
        mask = interior(images['owned-dark'])
        if np.count_nonzero(mask) < 10000:
            raise ValueError(backend + ': missing complete PBR coverage')

        def values(name, target, area=mask):
            target = np.asarray(target)
            actual = images[name][area]
            if target.ndim == 3:
                target = target[area]
            error = float(np.abs(actual - target).max())
            checks[backend + '/' + name + '/oracle'] = {'pixels': len(actual), 'maximumByteError': error}
            if error > 1.1:
                failures.append(f'{backend}/{name}: independent radiance/composition mismatch ({error:.3f})')

        radiance = np.asarray((1, 0.5, 0.25))

        def diffuse(rgb=(188, 188, 188), metallic=0, stages=1):
            return np.asarray([linear(c) for c in rgb]) * (1 - metallic) * (0.96 / math.pi) * radiance * stages * 255

        for name, stages in (('scalar', 1), ('packed', 1), ('separate', 1), ('ao-zero', 1),
                             ('two', 2), ('two-stages', 2), ('restored', 1)):
            values(name, diffuse(metallic=102 / 255, stages=stages))
        values('dark', (0, 0, 0))
        for kind in ('dielectric', 'metal'):
            for roughness in (0, 5):
                values(f'{kind}-{roughness}', diffuse(metallic=kind == 'metal'))
        for name in ('normal-xyz', 'normal-rg', 'normal-agb', 'normal-zero', 'rotated'):
            values(name, diffuse())
        cutout_mask = interior(images['cutout-owned'])
        cutout_pixels = int(np.count_nonzero(cutout_mask))
        checks[backend + '/cutout/coverage'] = {'interiorPixels': cutout_pixels}
        if cutout_pixels < 1000:
            failures.append(backend + ': insufficient cutout coverage')
        else:
            values('cutout', diffuse(), cutout_mask)
        for name in ('owned', 'owned-two'):
            values(name, (0, 255, 0))
        for name in ('emission-dark', 'emission', 'emission-two'):
            values(name, [min(255, 255 * linear(c) * 4) for c in (30, 200, 255)])
        alpha = 112 / 255
        for suffix, stages in (('dark', 0), ('one', 1), ('two', 2), ('stages', 2)):
            target = images['background-' + suffix] * (1 - alpha) + diffuse((50, 180, 255), stages=stages) * alpha
            values('alpha-' + suffix, target)
        values('alpha-owned', images['background-one'] * (1 - alpha) + np.asarray((0, 112, 0)))
        exact_pairs = [('scalar', name) for name in ('packed', 'separate', 'ao-zero', 'restored')]
        exact_pairs += [('normal-xyz', name) for name in ('normal-rg', 'normal-agb', 'normal-zero', 'dielectric-0', 'dielectric-5')]
        exact_pairs += [('alpha-one', 'alpha-' + name) for name in
                        ('restored', 'image-reload', 'partial-restart', 'full-restart')]
        exact_pairs += [('two', 'two-stages'), ('metal-0', 'metal-5')]
        if backend == 'vk':
            exact_pairs += [('alpha-classic', 'alpha-fault')]
        for a, b in exact_pairs:
            error = int(np.abs(images[a] - images[b]).max())
            checks[f'{backend}/{a}/{b}'] = {'maximumByteError': error}
            if error:
                failures.append(f'{backend}/{a}/{b}: material, restoration or rollback changed ({error})')

    parity = {}
    if paired:
        mask = interior(captures[0]['owned-dark']) & interior(captures[1]['owned-dark'])
        for name, a in captures[0].items():
            b = captures[1][name]
            delta = np.abs(a - b)
            error = int(delta.max())
            interior_error = int(delta[mask].max())
            # Alpha-test boundaries also vary with API coverage. Its absolute
            # radiance is checked above on each backend's actual retained samples.
            if name.startswith('cutout'):
                selected = interior(captures[0]['cutout-owned']) & interior(captures[1]['cutout-owned'])
                interior_error = int(delta[selected].max())
            parity[name] = {'maximumByteError': error, 'pixelsAbove2': int(np.count_nonzero(delta.max(axis=2) > 2)),
                            'interiorMaximumByteError': interior_error}
            if interior_error > 2 or (samples == 0 and error > 2 and not name.startswith('cutout')):
                failures.append(f'{name}: GL/native paired rendering mismatch ({error}, interior {interior_error})')
    return {'status': 'fail' if failures else 'pass', 'samples': samples,
            'fullFrameStatus': ('pass' if all(v['maximumByteError'] <= 2 for v in parity.values()) else 'fail') if paired else 'not-run',
            'scope': 'Absolute ambient lighting and alpha composition, material invariants, native rollback and lifecycle. GL pairing requires an admitted reference; its absence is not a parity pass.',
            'inputs': {str(p.resolve()): digest(p) for p in paths}, 'harnessSHA256': digest(__file__),
            'nativeRevisionDifferences': revisions,
            'checks': checks, 'parity': parity, 'failures': failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gl', type=Path, help='optional reference; blocked or fallback captures are rejected')
    parser.add_argument('--vk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    proof = compare((args.gl, args.vk) if args.gl else (args.vk,))
    args.output.write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({k: proof[k] for k in ('status', 'fullFrameStatus', 'failures')}, indent=2))
    return int(proof['status'] != 'pass')


if __name__ == '__main__':
    raise SystemExit(main())

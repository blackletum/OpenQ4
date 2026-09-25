#!/usr/bin/env python3
"""Qualify raw RGBA8 engine captures without window capture or input control.

Exercise real SMAA targets, repeated/interleaved readbacks, target resizing,
reloads and video restarts. Invalid images/formats/paths must not create files.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

import numpy as np
from PIL import Image

import renderer_smaa_orientation as smaa

lab = smaa.lab


def configure(manifest, samples):
    reference = smaa.configure(manifest, samples)
    base = reference['smaa-material0-hdr0-aa4']['settings']
    pbr = {'r_pbrMaterials': '1', 'r_rendererModernSubmit': '1', 'r_pbrDebug': '6'}
    setup = next(iter(reference.values()))['commands']
    profile = {}

    def add(suffix, settings=None, commands=None, extent=(1280, 800), restore=None, presentation=(1280, 800)):
        profile['image-capture-' + suffix] = dict(settings={**base, **(settings or {})},
            commands=commands or [], extent=list(extent), restore=restore, presentation=list(presentation))

    add('baseline', commands=setup)
    invalid = [
        'screenshot image _image_capture_missing "screenshots/image-denied-missing.tga"',
        'screenshot image _forwardRenderAlbedo "screenshots/image-denied-msaa.tga"',
        'screenshot image _postProcessDepth2 "screenshots/image-denied-depth.tga"',
        'screenshot image _postProcessAlbedo2 "screenshots/../image-denied-parent.tga"',
        'screenshot image _postProcessAlbedo2 "screenshots/image-denied-format.pfm"',
        'screenshot image _postProcessAlbedo2 "image-denied-prefix.tga"',
        'screenshot image _postProcessAlbedo2',
    ]
    add('invalid', commands=invalid, restore='image-capture-baseline')
    add('half', {'r_screenFraction': '50'}, extent=(640, 400))
    add('large', {**pbr, 'r_screenFraction': '150'}, extent=(1920, 1200))
    add('scale-restored', restore='image-capture-baseline')
    for boundary in ('image-reload', 'partial-restart', 'full-restart'):
        add(boundary, commands=lab.CASE_COMMANDS[boundary], restore='image-capture-baseline')
    add('small-window', commands=['r_windowWidth 960', 'r_windowHeight 600',
        'vid_restart partial', 'wait 60'], extent=(960, 600), presentation=(960, 600))
    add('window-restored', commands=['r_windowWidth 1280', 'r_windowHeight 800',
        'vid_restart partial', 'wait 60'], restore='image-capture-baseline')
    add('pbr-preview', pbr)
    add('pbr-hdr', {**pbr, 'r_hdrToneMap': '1'}, commands=['wait 30',
        'screenshot image _forwardRenderResolvedAlbedo "screenshots/image-denied-float.tga"'])
    add('classic-restored', restore='image-capture-baseline')
    return profile


def capture_commands(name):
    return [f'echo "IMAGE_CAPTURE_{name}_BEGIN"', 'gfxInfo',
        f'screenshot "screenshots/{name}-before.tga"',
        f'screenshot image _postProcessAlbedo2 "screenshots/{name}-source-a.tga"',
        f'screenshot image _postProcessAlbedo0 "screenshots/{name}-final.tga"',
        f'screenshot image _postProcessAlbedo2 "screenshots/{name}-source-b.tga"',
        f'screenshot "screenshots/{name}-interleaved.tga"',
        f'screenshot image _postProcessAlbedo2 "screenshots/{name}-source-c.tga"',
        f'echo "IMAGE_CAPTURE_{name}_END"']


def qualify(report, profile, samples):
    failures, captures, checks, references = [], {}, {}, {}
    if not report.get('complete') or not report.get('runtimeUnchanged'):
        failures.append('incomplete sequence or changed runtime')
    if {row['case'] for row in report['results']} != set(profile):
        failures.append('case set differs from requested profile')
    for row in report['results']:
        name, spec = row['case'], profile[row['case']]
        failures.extend(name + ': ' + f for f in row['failures'])
        log = Path(row['log']).read_text(errors='replace')
        section = log.split(f'IMAGE_CAPTURE_{name}_BEGIN', 1)[-1].split(f'IMAGE_CAPTURE_{name}_END', 1)[0]
        if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b', section):
            failures.append(name + ': effective MSAA differs')
        if section.count('RGBA8)') != 4:
            failures.append(name + ': missing successful image capture markers')
        folder = Path(row['screenshot']).parent
        arrays = {}
        for suffix in ('before', 'source-a', 'source-b', 'source-c', 'final', 'interleaved', ''):
            path = folder / (name + ('-' + suffix if suffix else '') + '.tga')
            try:
                with Image.open(path) as image:
                    arrays[suffix] = np.asarray(image.convert('RGBA'))
                captures[name + '/' + (suffix or 'after')] = dict(path=str(path), sha256=lab.digest(path))
            except (OSError, ValueError) as error:
                failures.append(name + ': ' + str(error))
        if len(arrays) != 7:
            continue
        source = arrays['source-a']
        height, width = source.shape[:2]
        same_source = all(np.array_equal(source, arrays[key]) for key in ('source-b', 'source-c'))
        same_display = all(np.array_equal(arrays['before'], arrays[key]) for key in ('interleaved', ''))
        if not same_source or not same_display:
            failures.append(name + ': repeated readback changed the image or presentation')
        if [width, height] != spec['extent'] or arrays['final'].shape != source.shape:
            failures.append(name + ': wrong image extent')
        if list(arrays['before'].shape[1::-1]) != spec['presentation']:
            failures.append(name + ': wrong presentation extent')
        # This off-centre emissive specimen must remain above the midpoint. Its
        # location is independent of readback byte order and TGA descriptor bits.
        mask = source[:, :, :3].max(axis=2) > 64
        ys = np.where(mask)[0]
        centroid = float(ys.mean()) / height if len(ys) else 0.0
        upright = len(ys) > width * height * .01 and .37 < centroid < .43
        if not upright:
            failures.append(name + ': specimen missing or vertically inverted')
        references[name] = source
        if spec['restore'] and not np.array_equal(source, references.get(spec['restore'])):
            failures.append(name + ': raw source changed across a resource boundary')
        checks[name] = dict(extent=[width, height], repeatedSourceExact=same_source,
            presentationExact=same_display, centroidY=centroid, upright=upright)
    if report['results']:
        folder = Path(report['results'][0]['screenshot']).parent
        log = Path(report['results'][0]['log']).read_text(errors='replace')
        if list(folder.glob('image-denied-*')) or list(folder.parent.glob('image-denied-*')):
            failures.append('a rejected capture created a file')
        markers = {'image \'_image_capture_missing\' is unavailable': 1,
            'screenshot image: readback requires': 3, 'screenshot image: use screenshots/': 3,
            'usage: screenshot image': 1}
        for marker, count in markers.items():
            if log.count(marker) != count:
                failures.append('wrong rejection count: ' + marker)
    return dict(status='fail' if failures else 'pass', checks=checks, captures=captures, failures=failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runtime-root', 'output-dir', 'basepath'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--backend', choices=('gl', 'vk'), required=True)
    parser.add_argument('--samples', type=int, choices=(4, 8), default=8)
    args = parser.parse_args()
    manifest = json.loads((args.runtime_root / 'pbr-lab.json').read_text())
    profile = configure(manifest, args.samples)
    for name, spec in profile.items():
        lab.CASES[name] = spec['settings']
        lab.CASE_CAMERAS[name] = 'sampling'
        lab.CASE_CAPTURE_EXTENTS[name] = tuple(spec['presentation'])
        lab.CASE_COMMANDS[name] = [*spec['commands'], 'wait 30', *capture_commands(name)]
        if spec['settings']['r_pbrMaterials'] == '0':
            lab.LEGACY_CASES.add(name)
    sys.argv = [__file__, '--runtime-root', str(args.runtime_root), '--output-dir', str(args.output_dir),
        '--basepath', str(args.basepath), '--backend', args.backend, '--camera', 'sampling',
        '--batch', '--cases', ','.join(profile), '--timeout', '900']
    if args.backend == 'gl':
        sys.argv.append('--gl-debug')
    code = lab.main()
    path = args.output_dir / 'report.json'
    report = json.loads(path.read_text())
    report.update(imageCaptureProfile=profile, requestedSamples=args.samples,
        imageCaptureProof=qualify(report, profile, args.samples))
    for source in (Path(__file__), Path(smaa.__file__), Path(smaa.scene.__file__)):
        shutil.copy2(source, args.output_dir / 'harness' / source.name)
        report['harnessSources'][source.name] = lab.digest(source)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    proof = report['imageCaptureProof']
    print(json.dumps(dict(status=proof['status'], cases=len(profile), failures=proof['failures'])), flush=True)
    return int(bool(code or proof['failures']))


if __name__ == '__main__':
    raise SystemExit(main())

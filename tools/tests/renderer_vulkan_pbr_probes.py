#!/usr/bin/env python3
"""Authored-probe gameplay controls, native lifecycle/failure proof and GL parity.

All captures use the engine screenshot command in a hidden window without input
injection. The extra assets are original laboratory data, never shipped content.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys

import renderer_pbr_laboratory as lab

MATERIAL_PREFIX = 'lights/openq4/probe_validation'


def prepare_fixture(runtime: Path) -> None:
    manifest_path = runtime/'pbr-lab.json'
    manifest = json.loads(manifest_path.read_text())
    materials = []
    # Nine image identities with known colors exercise eight-slot residency.
    for i in range(9):
        cube = f'env/openq4/probe_validation/source{i}'
        for face in ('px','nx','py','ny','pz','nz'):
            source = runtime/f'baseoq4/env/openq4/pbr_lab/{"warm" if i%2==0 else "cool"}_{face}.tga'
            target = lab.fixture.validate_contained_target(runtime, runtime/f'baseoq4/{cube}_{face}.tga')
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(source.read_bytes())
            manifest['sha256'][str(target.relative_to(runtime)).replace('\\','/')] = lab.digest(target)
        materials.append(f'{MATERIAL_PREFIX}/source{i}\n{{\nopenQ4SpecularProbe {{\ncubeMap {cube}\nintensity 2\nblendFraction 0.5\npriority 8\n}}\n{{ map _white }}\n}}\n')
    for name,tint,intensity,priority in (('tinted','0.15 1 0.35',1,8),('priority','0.15 1 0.35',1,16)):
        materials.append(f'{MATERIAL_PREFIX}/{name}\n{{\nopenQ4SpecularProbe {{\ncubeMap env/openq4/pbr_lab/warm\ntint {tint}\nintensity {intensity}\nblendFraction 0.5\npriority {priority}\n}}\n{{ map _white }}\n}}\n')
    target=runtime/'baseoq4/materials/openq4_probe_validation.mtr'
    target.write_text('\n'.join(materials),newline='\n')
    manifest['sha256'][str(target.relative_to(runtime)).replace('\\','/')]=lab.digest(target)
    # Precache these declarations during map loading. Runtime spawn warnings
    # are gate failures; they must not be filtered from the evidence.
    target=runtime/f'baseoq4/{lab.lab.MAP}.map'
    source=target.read_text()
    source=re.sub(r'^"mtr_probe_validation_[^"]+"[^\n]*\n','',source,flags=re.M)
    names=[*(f'source{i}' for i in range(9)),'tinted','priority']
    fields=''.join(f'"mtr_probe_validation_{name}" "{MATERIAL_PREFIX}/{name}"\n' for name in names)
    source=source.replace('"classname" "worldspawn"\n','"classname" "worldspawn"\n'+fields,1)
    target.write_text(source,newline='\n')
    manifest['sha256'][str(target.relative_to(runtime)).replace('\\','/')]=lab.digest(target)
    manifest['probeValidation']={'version':2,'source':lab.digest(Path(__file__))}
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')


def configure(manifest: dict, samples: int, backend: str, selected: set[str] | None = None) -> dict:
    profile={}
    previous_count=0
    lab.BASE.update({'r_vkPBRProbeFailure':'0','r_vkPBRPrepareFailure':'0','r_vkPBRPrepareFailureAfter':'0'})

    def add(name, sources=('warm','cool'), material='data_scalar', enabled=True, radius='600 600 600',
            origin='0 -700 380', angle=0, rotation=None, failure=0, boundary=None, reference=None,
            valid=True, pbr=True, debug=0, tolerance=0, probe_origins=None,
            specimen_origin='0 -700 380', specimen_angle=0, specimen_rotation=None):
        nonlocal previous_count
        if selected is not None and name not in selected: return
        case='probes-'+name
        commands=['g_stopTime 0']
        if not profile:
            commands += [f'script "${s["name"]}.hide()"' for s in manifest['stations']]
            commands += [f'script "${light}.Off()"' for light in ('key','blue_fill','warm_fill','lab_projector')]
            commands += ['script "$probe_warm.remove(); $probe_cool.remove()"','wait 3']
        else:
            commands += ['script "$probe_specimen.remove()"']
            commands += [f'script "$validation_probe_{i}.remove()"' for i in range(previous_count)]
            commands += ['wait 3']
        commands += [f'spawn func_static name probe_specimen model "{lab.lab.MODEL}" shader "{lab.lab.PREFIX}/{material}" origin "{specimen_origin}" angle {specimen_angle} solid 0']
        if specimen_rotation: commands[-1] += f' rotation "{specimen_rotation}"'
        for i,source in enumerate(sources):
            shader = f'lights/openq4/pbr_lab/{source}' if source in ('warm','cool') else f'{MATERIAL_PREFIX}/{source}'
            probe_origin = probe_origins[i] if probe_origins else origin
            commands += [f'spawn light name validation_probe_{i} texture "{shader}" origin "{probe_origin}" angle {angle} light_radius "{radius}" _color "1 1 1" noshadows 1']
            if rotation: commands[-1] += f' rotation "{rotation}"'
        commands += ['wait 30','g_stopTime 1']
        if boundary: commands += lab.CASE_COMMANDS[boundary]
        previous_count=len(sources)
        settings={'r_rendererReflectionProbes':str(int(enabled)), 'r_pbrIBL':'1', 'r_pbrIBLIntensity':'0.35',
                  'r_multiSamples':str(samples),'r_vkPBRProbeFailure':str(failure), 'r_pbrDebug':str(debug),
                  'r_pbrMaterials':str(int(pbr))}
        lab.CASES[case]=settings
        lab.CASE_COMMANDS[case]=commands
        lab.CASE_CAMERAS[case]='sampling'
        if not pbr: lab.LEGACY_CASES.add(case)
        profile[case]={'sources':list(sources),'material':material,'enabled':enabled,
                       'valid':valid,'pbr':pbr,'failure':failure,'reference':reference,'tolerance':tolerance,
                       'settings':settings,'commands':commands}

    add('analytic',enabled=False)
    add('both')
    add('warm',sources=('warm',))
    add('cool',sources=('cool',))
    add('rotated',angle=90)
    add('fade',origin='0 -700 900')
    add('outside',origin='0 -700 1200',reference='analytic')
    add('tinted',sources=('tinted',))
    add('priority-low',sources=('warm','cool','tinted'))
    add('priority-high',sources=('warm','cool','priority'))
    add('rough-low',material='metal_0')
    add('yaw-90',material='metal_0',angle=90)
    add('yaw-180',material='metal_0',angle=180)
    add('yaw-270',material='metal_0',angle=270)
    add('pitch-up',material='metal_0',rotation='1 0 0 0 0 1 0 -1 0')
    add('pitch-down',material='metal_0',rotation='1 0 0 0 0 -1 0 1 0')
    add('rough-high',material='metal_5')
    add('normal',material='baked_normal_xyz')
    add('ao-zero',material='ao_zero')
    add('transparent',material='source_alpha')
    add('transparent-analytic',material='source_alpha',enabled=False)
    add('transparent-classic',material='source_alpha',enabled=False,pbr=False)
    add('cutout',material='cutout')
    add('cutout-analytic',material='cutout',enabled=False)
    # The emission owner produces its diagnostic with all direct lights off.
    # This material uses the same diffuse alpha texture and threshold as cutout.
    add('cutout-owned',material='emission_cutout',debug=7)
    # Distinct visible volumes exercise native cluster addressing, including
    # the bottom-origin Y convention. A shared origin cannot expose that error.
    for axis,origins in (('x',('-110 -800 380','110 -800 380')),
                         ('z',('0 -800 270','0 -800 490'))):
        add('spatial-'+axis,probe_origins=origins,radius='150 150 150')
        add('spatial-'+axis+'-swapped',probe_origins=origins[::-1],radius='150 150 150')
    add('translated',origin='60 -700 420',specimen_origin='60 -700 420')
    add('rotated-model',material='baked_normal_xyz',
        specimen_rotation='1 0 0 0 0 1 0 -1 0')
    add('eight',sources=tuple(f'source{i}' for i in range(8)))
    add('nine',sources=tuple(f'source{i}' for i in range(9)),valid=False,reference='analytic')
    # A relocated tile has different normalized atlas coordinates. Hardware
    # bilinear coordinate rounding may change a stored byte by one; ordinary
    # reload and failure/recovery controls below still require exact images.
    add('evicted',sources=('source8','source1'),reference='both',tolerance=1)
    add('record-overflow',sources=('warm',)*33,valid=False,reference='analytic')
    add('invalid-volume',radius='600 600 300',valid=False,reference='analytic')
    add('restored',reference='both',tolerance=1)
    for boundary in ('image-reload','partial-restart','full-restart'):
        # GL retains atlas placements on image/partial reload; Vulkan's image
        # generator resets them on image reload. Compare each boundary to the
        # matching placement, keeping the boundary itself byte-exact.
        reference = ('restored' if boundary=='image-reload' else 'image-reload') if backend=='gl' and boundary!='full-restart' else 'both'
        add(boundary,boundary=boundary,reference=reference)
    if backend=='vk':
        add('source-failure',failure=1,valid=False,reference='analytic')
        add('source-recovered',reference='both')
        for failure in (2,3):
            add(f'resource-{failure}',material='source_alpha',failure=failure,valid=False,reference='transparent-classic')
            add(f'resource-{failure}-recovered',material='source_alpha',reference='transparent')
    if selected is not None and {'probes-'+name for name in selected}!=set(profile):
        raise ValueError('unknown or unavailable probe controls: '+str(sorted(selected)))
    return profile


def inspect(report: dict, profile: dict, backend: str, samples: int) -> dict:
    failures=[]; checks={}; patches={}; images={}; states={}
    rows={r['case']:r for r in report['results']}
    if set(rows)!=set(profile) or not report.get('complete') or not report.get('runtimeUnchanged'):
        failures.append('incomplete capture set or changed runtime')
    for name,spec in profile.items():
        row=rows.get(name,{})
        if row.get('failures'): failures.append(f'{name}: capture failed')
        for kind in ('screenshot','log'):
            path=Path(row.get(kind,''))
            if not path.is_file() or lab.digest(path)!=row.get('sha256',{}).get(kind):
                failures.append(f'{name}: {kind} provenance differs')
        if not Path(row.get('screenshot','')).is_file(): continue
        patches[name]=lab.normal_patch(Path(row['screenshot']))
        images[name]=lab.capture_rgb(Path(row['screenshot']))
        telemetry='\n'.join(row.get('telemetry',[]))
        if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b',telemetry):
            failures.append(f'{name}: sample count not effective')
        prefix='Vulkan PBR probes:' if backend=='vk' else 'Modern clustered specular probes:'
        line=next((l for l in row.get('telemetry',[]) if l.startswith(prefix)),'')
        state=dict(re.findall(r'(\w+)=([^\s]+)',line)); states[name]=state
        if spec['enabled'] and spec['pbr']:
            ready=spec['valid'] and not spec['failure']
            key='ready' if backend=='vk' else 'frameReady'
            if ready and (state.get(key)!='1' or int(state.get('records' if backend=='vk' else 'uploaded','0'))!=len(spec['sources'])):
                failures.append(f'{name}: incomplete authored probe publication')
            if backend=='vk':
                if ready and int(state.get('views','0'))<1: failures.append(f'{name}: no native descriptor consumer')
                if not ready and int(state.get('views','-1'))!=0: failures.append(f'{name}: rejected resources still published')
                if spec['failure'] in (2,3):
                    owner=next((l for l in row['telemetry'] if l.startswith('Vulkan: native PBR transparency:')),'')
                    if not re.search(r'\bready=0\b',owner) or 'reason=environment-probe-resource' not in owner:
                        failures.append(f'{name}: transparent resource transaction retained ownership')
        patch=patches[name]
        if not patch: failures.append(f'{name}: missing specimen patch')
        if name=='probes-cutout-owned' and patch:
            green=patch[1::3]
            if not (0 in green and 255 in green) or max(patch[0::3]+patch[2::3])>1:
                failures.append(f'{name}: coverage requires green material and black holes')
            if samples==0 and any(value not in (0,255) for value in green):
                failures.append(f'{name}: single-sample coverage is not binary')
            if samples==4 and not any(0<value<255 for value in green):
                failures.append(f'{name}: four-sample coverage has no partial samples')
    for name,spec in profile.items():
        if spec['reference'] and name in patches:
            reference='probes-'+spec['reference']
            if reference not in patches: failures.append(f'{name}: missing reference'); continue
            a,b=images[name],images[reference]
            maximum=max(abs(x-y) for x,y in zip(a,b))
            checks[name]={'reference':reference,'maximumError':maximum,'tolerance':spec['tolerance']}
            if maximum>spec['tolerance']: failures.append(f'{name}: full-frame fallback/restoration differs by {maximum}')
    for a,b in (('both','analytic'),('warm','cool'),('both','rotated'),('both','fade'),
                ('warm','tinted'),('priority-low','priority-high'),('rough-low','rough-high'),
                ('both','normal'),('transparent','transparent-analytic'),('cutout','cutout-analytic'),
                ('rough-low','yaw-90'),('rough-low','yaw-180'),('rough-low','yaw-270'),
                ('rough-low','pitch-up'),('rough-low','pitch-down'),
                ('spatial-x','spatial-x-swapped'),('spatial-z','spatial-z-swapped'),
                ('normal','rotated-model')):
        if 'probes-'+a not in patches or 'probes-'+b not in patches: continue
        mean=sum(abs(x-y) for x,y in zip(patches['probes-'+a],patches['probes-'+b]))/len(patches['probes-'+a])
        checks[f'{a}/{b}']={'meanError':mean}
        if mean<0.2: failures.append(f'{a}/{b}: control has no visible effect')
    if 'probes-ao-zero' in patches and max(patches['probes-ao-zero'])>1:
        failures.append('AO zero retained indirect light')
    return {'status':'fail' if failures else 'pass','failures':failures,'telemetry':states,'imageChecks':checks}


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--basepath',type=Path,required=True)
    parser.add_argument('--backend',choices=('vk','gl'),default='vk')
    parser.add_argument('--samples',type=int,choices=(0,4),default=0)
    parser.add_argument('--timeout',type=int,default=900)
    parser.add_argument('--prepare-fixture',action='store_true')
    parser.add_argument('--cases',help='comma-separated control suffixes for a focused diagnostic run; omitted runs the full suite')
    args=parser.parse_args()
    args.runtime_root=lab.fixture.validate_runtime_root(args.runtime_root)
    args.output_dir=lab.fixture.validate_runtime_root(args.output_dir)
    args.output_dir.mkdir(parents=True,exist_ok=True)
    sources=args.output_dir/'harness'
    sources.mkdir(exist_ok=True)
    shutil.copy2(Path(__file__),sources/Path(__file__).name)
    harness_hash=lab.digest(sources/Path(__file__).name)
    if args.prepare_fixture:
        prepare_fixture(args.runtime_root)
        lab.compile_fixture(args.runtime_root,args.basepath.resolve(),args.output_dir,args.timeout)
    manifest=json.loads((args.runtime_root/'pbr-lab.json').read_text())
    if manifest.get('probeValidation',{}).get('version')!=2: parser.error('use --prepare-fixture once for this independent laboratory copy')
    selected=set(args.cases.split(',')) if args.cases else None
    profile=configure(manifest,args.samples,args.backend,selected)
    sys.argv=[__file__,'--runtime-root',str(args.runtime_root),'--output-dir',str(args.output_dir),
              '--basepath',str(args.basepath),'--backend',args.backend,'--camera','sampling','--batch',
              '--cases',','.join(profile),'--timeout',str(args.timeout)]
    if args.backend=='gl': sys.argv.append('--gl-debug')
    code=lab.main()
    path=args.output_dir/'report.json'; report=json.loads(path.read_text())
    report.update(probeProfile=profile,probeHarnessSHA256=harness_hash,requestedSamples=args.samples,
                  probeScope='selected controls' if selected is not None else 'full suite')
    report['probeProof']=inspect(report,profile,args.backend,args.samples)
    if lab.digest(Path(__file__))!=harness_hash:
        report['probeProof']['status']='fail'
        report['probeProof']['failures'].append('probe harness source changed during execution')
    path.write_text(json.dumps(report,indent=2)+'\n')
    print('Authored probe proof:',report['probeProof']['status'],report['probeProof']['failures'],flush=True)
    return int(bool(code or report['probeProof']['failures']))


if __name__=='__main__': raise SystemExit(main())

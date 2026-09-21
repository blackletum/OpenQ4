#!/usr/bin/env python3
"""Reproduce the windowed, input-free PBR laboratory and retain engine captures.

Use --prepare once to copy the staged package, generate the original fixture,
and compile its map. Every capture uses a fresh save directory and the engine's
registered screenshot command. No OS capture or input injection is used.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/validation'))
import generate_pbr_validation_map as lab
import generate_pbr_fixture as fixture

BASE = {
    'r_fullscreen': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1',
    'r_hiddenWindow': '1', 'in_mouse': '0', 'in_joystick': '0', 's_noSound': '1',
    'r_mode': '-1', 'r_customWidth': '1280', 'r_customHeight': '800',
    'r_windowWidth': '1280', 'r_windowHeight': '800', 'r_swapInterval': '0',
    'r_multiSamples': '0', 'com_maxfps': '60', 'logFile': '2',
    'r_msaaAlphaToCoverage':'1',
    'r_vkPBRSpecularAA':'1',
    'r_gpuSkinning':'0',
    'r_useScissor':'1', 'r_lightScale':'2',
    'logFileName': 'logs/openq4.log', 'developer': '1', 'r_ignoreGLErrors': '0',
    'com_skipLoadingContinue': '1', 'com_loadingContinueAutoAdvance': '1',
    'com_levelLoadModernization': '0', 'g_autoSkipCinematics': '1',
    'g_autoScreenshot': '0', 'ui_showGun': '0', 'g_showHud': '0',
    'r_rendererModernExecutor': '1', 'r_rendererModernSubmit': '1',
    'r_rendererModernOpaque': '1', 'r_rendererModernDeferred': '1',
    'r_rendererForwardPlus': '1', 'r_rendererModernVisible': '1',
    'r_rendererModernLightingParity': '0', 'r_rendererModernQuality': '1',
    'r_rendererModernAutoPromote': '0', 'r_rendererClusteredDecals': '0',
    'r_rendererSharedWorldFogBlend':'0',
    'r_rendererSharedWorldAmbient':'0',
    'r_rendererReflectionProbes': '1', 'r_pbrMaterials': '1', 'r_pbrDebug': '0',
    'r_pbrIBL': '1', 'r_pbrIBLIntensity': '1', 'r_shadows': '0',
    'r_bloom': '0', 'r_ssao': '0', 'r_postAA': '0', 'r_motionBlur': '0',
    'r_hdrToneMap': '0', 'r_hdrAutoExposure': '0', 'r_hdrSceneTarget': '1',
    'r_hdrExposure': '1', 'r_hdrHighlightDesaturation': '0', 'r_hdrGamutCompression': '0',
    'r_hdrWhitePoint':'6', 'r_hdrLift':'0', 'r_hdrPostGamma':'1', 'r_hdrGain':'1',
    'r_hdrMinExposure':'0.25', 'r_hdrMaxExposure':'8', 'r_hdrAutoExposureAsync':'1',
    'r_hdrKeyValue':'0.18',
    'r_hdrAdaptUpSpeed':'3', 'r_hdrAdaptDownSpeed':'1.5',
    'r_hdrVibrance':'0', 'r_hdrSaturation':'1', 'r_hdrContrast':'1',
    'r_useShadowMap': '0', 'r_shadowMapCSM': '0', 'r_rendererShaderReload': '0',
    'g_renderFastNoPost': '1', 'g_renderFastNoPostDirect': '1',
    'r_useLightGrid': '0', 'r_skipPlayerVisibilityEffects': '1',
    'r_skipAmbient':'0',
    'r_lightGridIntensity':'1', 'r_lightGridMaxContribution':'1', 'r_lightGridIrradianceGamma':'1',
    'g_fov': '90', 'r_useConstantMaterials': '1', 'com_machineSpec': '3',
    'g_showPlayerShadow':'1',
    'r_shadowMapSize':'1024',
    'r_shadowMapPointSize':'512',
    'r_shadowMapPointDepthCompare':'1', 'r_shadowMapStaticCache':'1',
    'r_shadowMapReport':'0', 'r_shadowMapReportInterval':'30',
    'r_rendererBenchmarkPreset':'baseline',
    'si_gameType': 'singleplayer', 'win_allowMultipleInstances': '1',
    'sys_allowMultipleInstances': '1',
}
CASES = {
    'lit': {},
    'forced-parity': {'r_rendererModernLightingParity':'15'},
    'production': {'r_rendererModernLightingParity':'0'},
    'production-shadows': {'r_rendererModernLightingParity':'0','r_shadows':'1','r_useShadowMap':'1'},
    'production-native': {'r_rendererModernLightingParity':'0','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    'production-rejected': {'r_rendererModernLightingParity':'0'},
    'production-rejected-native': {'r_rendererModernLightingParity':'0','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    **{f'production-fixed{kind}{suffix}': {'r_rendererModernLightingParity':'0','r_hdrToneMap':'1','r_lightScale':'0.1',
          **({'r_pbrMaterials':'0','r_rendererReflectionProbes':'0'} if suffix else {})}
       for kind in ('','-bump','-diffuse','-colored') for suffix in ('','-native')},
    'production-curved': {'r_rendererModernLightingParity':'0'},
    'production-curved-native': {'r_rendererModernLightingParity':'0','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    **{f'production-{light}{suffix}': {'r_rendererModernLightingParity':'0',
          **({'r_pbrMaterials':'0','r_rendererReflectionProbes':'0'} if suffix else {})}
       for light in ('points','projector') for suffix in ('','-native')},
    'environment-only': {'r_rendererModernLightingParity':'0'},
    'environment-off': {'r_rendererModernLightingParity':'0','r_pbrIBL':'0'},
    **{f'{kind}-{mode}': {'r_pbrMaterials':'1' if mode=='fallback' else '0','r_rendererReflectionProbes':'1' if mode=='fallback' else '0'}
       for kind in ('fog','blend') for mode in ('fallback','native','off')},
    **{f'{kind}-{mode}': {} for kind in ('fog','blend') for mode in ('clear','restored')},
    **{f'{kind}-pbr{suffix}': {'r_hdrToneMap':'1'}
       for kind in ('fog','blend') for suffix in ('','-clear','-off','-restored')},
    **{f'{kind}-pbr{suffix}': {'r_hdrToneMap':'1'}
       for kind in ('fog','blend') for suffix in ('-hidden','-hidden-clear')},
    **{f'{kind}-pbr-shared': {'r_hdrToneMap':'1','r_rendererSharedWorldFogBlend':'1'} for kind in ('fog','blend')},
    **{f'{kind}-pbr-msaa{suffix}': {'r_hdrToneMap':'1','r_multiSamples':'4'}
       for kind in ('fog','blend') for suffix in ('','-clear','-off','-restored')},
    **{f'lightgrid-{mode}': {'r_useLightGrid':'0' if mode=='off' else '1',
           'r_pbrMaterials':'1' if mode=='fallback' else '0',
           'r_rendererReflectionProbes':'1' if mode=='fallback' else '0'}
       for mode in ('fallback','native','off')},
    **{f'lightgrid-pbr{suffix}': {'r_hdrToneMap':'1', 'r_pbrIBL':'0',
           'r_useLightGrid':'0' if suffix in ('-clear','-off') else '1',
           'r_lightGridIntensity':'2' if suffix=='-double' else '0' if suffix=='-zero' else '1',
           'r_lightGridMaxContribution':'0', 'r_lightGridIrradianceGamma':'1'}
       for suffix in ('-clear','','-double','-zero','-off','-restored')},
    'production-no-scissor': {'r_rendererModernLightingParity':'0','r_useScissor':'0'},
    'production-no-scissor-native': {'r_rendererModernLightingParity':'0','r_useScissor':'0','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    'ownership': {'r_pbrDebug': '7'},
    'albedo': {'r_pbrDebug': '1'},
    'normals': {'r_pbrDebug': '2'},
    **{f'normal-{name}': {'r_pbrDebug':'2'} for name in ('xyz','rg','agb','zero','flat')},
    'metallic': {'r_pbrDebug': '3'},
    'roughness': {'r_pbrDebug': '4'},
    'ao': {'r_pbrDebug': '5'},
    'emissive': {'r_pbrDebug': '6'},
    'no-probes': {'r_rendererReflectionProbes': '0'},
    'direct': {'r_pbrIBL': '0', 'r_rendererReflectionProbes': '0'},
    'legacy': {'r_pbrMaterials': '0', 'r_rendererReflectionProbes': '0'},
    'master-off': {'r_rendererModernQuality': '0'},
    'no-constant-cache': {'r_useConstantMaterials': '0'},
    'hdr': {'r_hdrToneMap':'1'},
    'hdr-shadows': {'r_hdrToneMap':'1','r_shadows':'1','r_useShadowMap':'1'},
    'hdr-restored': {'r_hdrToneMap':'1'},
    'hdr-half': {'r_hdrToneMap':'1','r_hdrExposure':'0.5'},
    'hdr-hud': {'r_hdrToneMap':'1','g_showHud':'1'},
    'hdr-hud-half': {'r_hdrToneMap':'1','g_showHud':'1','r_hdrExposure':'0.5'},
    'bloom': {'r_hdrToneMap':'1','r_bloom':'1'},
    'hdr-auto-clamped': {'r_hdrToneMap':'1','r_hdrAutoExposure':'1','r_hdrMinExposure':'0.5','r_hdrMaxExposure':'0.5','r_hdrAdaptUpSpeed':'16','r_hdrAdaptDownSpeed':'16'},
    'hdr-auto': {'r_hdrToneMap':'1','r_hdrAutoExposure':'1','r_hdrAdaptUpSpeed':'16','r_hdrAdaptDownSpeed':'16'},
    'hdr-auto-sync': {'r_hdrToneMap':'1','r_hdrAutoExposure':'1','r_hdrAutoExposureAsync':'0','r_hdrAdaptUpSpeed':'16','r_hdrAdaptDownSpeed':'16'},
    'hdr-emissive': {'r_hdrToneMap':'1','r_pbrDebug':'6'},
    'hdr-extreme-emissive': {'r_hdrToneMap':'1','r_pbrDebug':'6'},
    'hdr-extreme-bloom': {'r_hdrToneMap':'1','r_bloom':'1'},
    'hdr-half-exposure': {'r_hdrToneMap':'1','r_hdrExposure':'0.5','r_pbrDebug':'6'},
    'post-target': {'g_renderFastNoPostDirect':'0','r_hdrToneMap':'1'},
    'smaa': {'r_postAA':'1','r_hdrToneMap':'1'},
    'msaa': {'r_multiSamples':'4','r_hdrToneMap':'1'},
    'msaa-restored': {'r_multiSamples':'4','r_hdrToneMap':'1'},
    'msaa-ownership': {'r_multiSamples':'4','r_pbrDebug':'7'},
    'msaa-cutout': {'r_multiSamples':'4','r_pbrDebug':'7'},
    'msaa-cutout-hard': {'r_multiSamples':'4','r_pbrDebug':'7','r_msaaAlphaToCoverage':'0'},
    'msaa-shadows': {'r_multiSamples':'4','r_hdrToneMap':'1','r_shadows':'1','r_useShadowMap':'1'},
    'msaa-partial-restart': {'r_multiSamples':'4','r_hdrToneMap':'1'},
    'msaa-full-restart': {'r_multiSamples':'4','r_hdrToneMap':'1'},
    'shadows': {'r_shadows':'1','r_useShadowMap':'1'},
    'static-shadows': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0'},
    'shadows-manual-point': {'r_shadows':'1','r_useShadowMap':'1','r_shadowMapPointDepthCompare':'0'},
    'shadows-cache-off': {'r_shadows':'1','r_useShadowMap':'1','r_shadowMapStaticCache':'0'},
    'shadow-moved': {'r_shadows':'1','r_useShadowMap':'1'},
    'shadow-restored': {'r_shadows':'1','r_useShadowMap':'1'},
    'unshadowed-moved': {},
    'multi-shadows': {'r_shadows':'1','r_useShadowMap':'1','r_rendererBenchmarkPreset':'high-end'},
    'multi-unshadowed': {'r_rendererBenchmarkPreset':'high-end'},
    'multi-budget': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0'},
    'multi-legacy': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    'shadow-capacity': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0','r_shadowMapPointSize':'2048','r_rendererBenchmarkPreset':'high-end'},
    'shadow-capacity-legacy': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0','r_shadowMapPointSize':'2048','r_rendererBenchmarkPreset':'high-end','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    'local-global': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0'},
    'local-global-legacy': {'r_shadows':'1','r_useShadowMap':'1','g_showPlayerShadow':'0','r_pbrMaterials':'0','r_rendererReflectionProbes':'0'},
    'shader-reload': {'r_rendererShaderReload':'1'},
    'image-reload': {},
    'partial-restart': {},
    'full-restart': {},
    'map-reload': {},
    **{f'sampler-{name}': {'r_pbrDebug':'1'} for name in ('nearest','linear','clamp','mips')},
    **{f'skin-{backend}{pose}': {'r_gpuSkinning':str(int(backend=='gpu')), 'r_pbrDebug':'2' if pose=='-normal' else '7' if pose=='-ownership' else '0', 'r_shadows':'1', 'r_useShadowMap':'1', 'g_showPlayerShadow':'0'}
       for backend in ('cpu','gpu') for pose in ('','-bent','-restored','-normal','-ownership')},
}
for restart in ('partial','full'):
    CASES[f'skin-gpu-{restart}-restart']=dict(CASES['skin-gpu'])
for suffix in ('-clear','','-double','-zero'):
    CASES['lightgrid-pbr-ibl'+suffix]={**CASES['lightgrid-pbr'+suffix], 'r_pbrIBL':'1'}
for suffix in ('-clear','','-double','-zero','-off','-restored'):
    CASES['lightgrid-pbr-msaa'+suffix]={**CASES['lightgrid-pbr'+suffix], 'r_multiSamples':'4'}
for suffix in ('-image-reload','-shader-reload','-partial-restart','-full-restart'):
    CASES['lightgrid-pbr'+suffix]=dict(CASES['lightgrid-pbr'])


CASES['lightgrid-pbr-shader-reload']['r_rendererShaderReload']='1'

for name in ('xyz','rg','agb','zero'):
    CASES['lightgrid-pbr-normal-'+name]=dict(CASES['lightgrid-pbr'])
for kind in ('fog','blend'):
    for suffix in ('-clear','','-off','-restored','-hidden-clear','-hidden','-shared'):
        CASES['lightgrid-pbr-'+kind+suffix]=dict(CASES['lightgrid-pbr'])
    CASES['lightgrid-pbr-'+kind+'-shared']['r_rendererSharedWorldFogBlend']='1'

CASES['lightgrid-pbr-master-off']={**CASES['lightgrid-pbr'], 'r_rendererModernQuality':'0'}
CASES['lightgrid-pbr-native']={**CASES['lightgrid-pbr'], 'r_pbrMaterials':'0', 'r_rendererReflectionProbes':'0'}
CASES['lightgrid-pbr-master-restored']=dict(CASES['lightgrid-pbr'])

for suffix in ('master-off','restored','image-reload','shader-reload','partial-restart','full-restart','resize'):
    CASES['production-fixed-'+suffix]={**CASES['production-fixed'],
        **({'r_rendererModernQuality':'0'} if suffix=='master-off' else {}),
        **({'r_rendererShaderReload':'1'} if suffix=='shader-reload' else {})}
for boundary, settings in (('shadow',{'r_shadows':'1','r_useShadowMap':'1'}), ('msaa',{'r_multiSamples':'4'})):
    for mode in ('fallback','native'):
        CASES[f'production-fixed-{boundary}-{mode}']={**CASES['production-fixed'], **settings,
            **({'r_pbrMaterials':'0','r_rendererReflectionProbes':'0'} if mode=='native' else {})}
for suffix in ('-clear','','-native','-zero','-restored'):
    CASES['production-fixed-grid'+suffix]={**CASES['production-fixed'], 'r_useLightGrid':'0' if suffix=='-clear' else '1', 'r_pbrIBL':'0',
        'r_lightGridIntensity':'0' if suffix=='-zero' else '0.2', 'r_lightGridMaxContribution':'0',
        **({'r_pbrMaterials':'0','r_rendererReflectionProbes':'0'} if suffix=='-native' else {})}

for case, reference in (('production-minimal','hdr'),('production-fixed-minimal','production-fixed')):
    CASES[case]={**CASES[reference], **{name:'0' for name in (
        'r_rendererModernExecutor','r_rendererModernSubmit','r_rendererModernOpaque',
        'r_rendererModernDeferred','r_rendererForwardPlus')}}
CASES['production-fixed-bright']={**CASES['production-fixed'],'r_lightScale':'2'}

FALLBACK_CASES = {'multi-budget','shadow-capacity','local-global','production-rejected','production-no-scissor'}
FALLBACK_CASES.update(('fog-fallback','blend-fallback','lightgrid-fallback'))
FALLBACK_CASES.update(f'production-fixed-{boundary}-fallback' for boundary in ('shadow','msaa'))
LEGACY_CASES = {'legacy','master-off','multi-legacy','shadow-capacity-legacy','local-global-legacy','production-native','production-rejected-native','production-curved-native'}
LEGACY_CASES.update(('production-fixed-colored-native','production-fixed-master-off'))
LEGACY_CASES.add('production-fixed-grid-native')
LEGACY_CASES.update(f'production-fixed-{boundary}-native' for boundary in ('shadow','msaa'))
LEGACY_CASES.update(f'production-{light}-native' for light in ('points','projector'))
LEGACY_CASES.update(('production-no-scissor-native','production-fixed-native','production-fixed-bump-native','production-fixed-diffuse-native'))
LEGACY_CASES.update(f'{kind}-{mode}' for kind in ('fog','blend') for mode in ('native','off'))
LEGACY_CASES.update(('lightgrid-native','lightgrid-off'))
LEGACY_CASES.update(('lightgrid-pbr-master-off','lightgrid-pbr-native'))
CASE_COMMANDS = {'shader-reload':['rendererShaderLibraryReload'], 'image-reload':['reloadImages all'],
                 'partial-restart':['vid_restart partial','wait 60'], 'full-restart':['vid_restart','wait 60'],
                 # Spawned entities publish their first render definitions on
                 # a simulation tick. Do not reload while the test is frozen.
                 'map-reload':['g_stopTime 0',f'map {lab.MAP}','wait 60','god','notarget','noclip','g_stopTime 1','setviewpos 0 -640 390 0 90 0']}
for suffix in ('image-reload','shader-reload','partial-restart','full-restart'):
    CASE_COMMANDS['lightgrid-pbr-'+suffix]=CASE_COMMANDS[suffix]
    CASE_COMMANDS['production-fixed-'+suffix]=CASE_COMMANDS[suffix]
CASE_COMMANDS['msaa-partial-restart'] = CASE_COMMANDS['partial-restart']
CASE_COMMANDS['msaa-full-restart'] = CASE_COMMANDS['full-restart']
CASE_COMMANDS['production-fixed-resize'] = [
    'r_customWidth 960','r_customHeight 600','r_windowWidth 960','r_windowHeight 600',
    'vid_restart partial','wait 60','echo PBRLAB_RESIZE_SMALL_BEGIN','gfxInfo',
    'screenshot "screenshots/production-fixed-resize-small.tga"','echo PBRLAB_RESIZE_SMALL_END',
    'r_customWidth 1280','r_customHeight 800','r_windowWidth 1280','r_windowHeight 800',
    'vid_restart partial','wait 60']
for case in ('hdr-auto','hdr-auto-sync','hdr-auto-clamped'):
    CASE_COMMANDS[case] = ['g_stopTime 0','wait 180','g_stopTime 1']
for restart in ('partial','full'):
    CASE_COMMANDS[f'skin-gpu-{restart}-restart']=CASE_COMMANDS[f'{restart}-restart']
for case, origin in (('shadow-moved','100 -40 480'),('shadow-restored','100 160 480'),('unshadowed-moved','100 -40 480')):
    # SetOrigin updates physics immediately, but Present publishes the change
    # on a simulation tick. Two render frames can contain no simulation tick.
    CASE_COMMANDS[case] = ['g_stopTime 0',f'''script "$metal_3.setOrigin('{origin}'); sys.println($metal_3.getOrigin())"''','wait 30','g_stopTime 1']
for case in ('multi-shadows','multi-unshadowed','multi-budget','multi-legacy'):
    CASE_COMMANDS[case] = [
        'g_stopTime 0',
        'spawn light name pbr_test_blue origin "-540 -320 490" light_radius "1400 1400 1400" _color "0.3 0.5 1"',
        'spawn light name pbr_test_red origin "540 -300 280" light_radius "1400 1400 1400" _color "1 0.3 0.1"',
        'wait 30','g_stopTime 1']
for case in ('local-global','local-global-legacy'):
    CASE_COMMANDS[case] = ['g_stopTime 0',
        f'spawn func_static name pbr_no_self model "{lab.MODEL}" shader "{lab.PREFIX}/noself_shadow" origin "0 -80 380" solid 0',
        'wait 30','g_stopTime 1']

SHADOW_CASES = {'shadows','static-shadows','shadows-manual-point','shadows-cache-off','shadow-moved','shadow-restored','multi-shadows','msaa-shadows'}
SHADOW_CASES.update(case for case in CASES if case.startswith('skin-'))
SHADOW_CASES.add('production-shadows')
SHADOW_CASES.add('hdr-shadows')

DIAGNOSTIC = re.compile(r'(?i)(WARNING:|ERROR:|VUID-|GL_INVALID|shader compile failed|shader source exceeds|GL debug.*type=(?:error|undefined))')


def diagnostic_lines(text: str) -> list[str]:
    # Driver recompilation/transfer advisories are performance information,
    # including when their human-readable message contains "warning:".
    return [line for line in text.splitlines() if DIAGNOSTIC.search(line)
            and not re.search(r'GL debug callback \[.*type=(?:performance|notification)\b',line)]

CASE_CAMERAS = {f'normal-{name}': 'station-normal_'+name for name in ('xyz','rg','agb','zero')}
CASE_CAMERAS['normal-flat'] = 'station-metal_3'
CASE_CAMERAS.update({'msaa-cutout':'station-cutout','msaa-cutout-hard':'station-cutout'})
CASE_CAMERAS.update({case:'sampling' for case in CASES if case.startswith('sampler-')})
CASE_CAMERAS.update({case:'sampling' for case in CASES if case.startswith('skin-')})
CASE_CAMERAS.update({case:'sampling' for case in CASES if case.startswith('lightgrid-pbr-normal-')})

# Same original sphere, transform, camera and light for every native direct
# material control. Full-map ownership remains a separate, stricter test.
VK_DIRECT_MATERIALS = {
    'scalar':'data_scalar', 'packed':'data_packed', 'separate':'data_separate',
    'ao-zero':'ao_zero', 'normal-xyz':'baked_normal_xyz',
    'normal-rg':'baked_normal_rg', 'normal-agb':'baked_normal_agb',
    'normal-zero':'baked_normal_zero', 'flat':'dielectric_3',
    'rough-low':'metal_0', 'rough-high':'metal_5',
    **{f'aa-{shape}{suffix}':material for shape,material,suffixes in (
        ('geometric','aa_geometric',('','-off','-restored','-owned')),
        ('normal','aa_normal',('','-off','-restored','-owned')),
        ('constant','dielectric_3',('','-off','-owned')),
        ('rough','metal_5',('','-off'))) for suffix in suffixes},
    'ownership-scalar':'data_scalar', 'ownership-packed':'data_packed',
    'ownership-separate':'data_separate', 'ownership-rg':'baked_normal_rg',
    'ownership-agb':'baked_normal_agb',
    'master-off':'data_scalar', 'restored':'data_scalar',
    'emission-half':'emission_half', 'emission-quarter':'emission_quarter',
    'emission-legacy':'emission_half', 'emission-restored':'emission_half',
    'emission-one-light':'emission_half', 'emission-many-lights':'emission_half',
    'emission-dark':'emission_half', 'emission-owned':'emission_half',
    'emission-shared':'emission_half',
    'emission-extreme':'emission_extreme_native',
    'emission-partial-restart':'emission_half', 'emission-full-restart':'emission_half',
    'emission-cutout':'emission_cutout', 'emission-cutout-owned':'emission_cutout',
    'emission-mismatch-fallback':'emission_mismatch', 'emission-mismatch-native':'emission_mismatch',
    'cutout-mismatch-fallback':'cutout_mismatch', 'cutout-mismatch-native':'cutout_mismatch',
    'cutout-lit':'cutout', 'cutout-owned':'cutout', 'cutout-hard':'cutout',
    'cutout-legacy':'cutout', 'cutout-restored':'cutout',
    'cutout-partial-restart':'cutout', 'cutout-full-restart':'cutout',
    'alpha-fallback':'source_alpha', 'alpha-native':'source_alpha',
    'partial-restart':'data_scalar', 'full-restart':'data_scalar',
    'shadow-point-off':'data_separate', 'shadow-point':'data_separate',
    'shadow-point-owned':'data_separate', 'shadow-point-restored':'data_separate',
    'shadow-projected-off':'baked_normal_agb', 'shadow-projected':'baked_normal_agb',
    'shadow-projected-owned':'baked_normal_agb', 'shadow-projected-restored':'baked_normal_agb',
}
for suffix in VK_DIRECT_MATERIALS:
    case='vk-direct-'+suffix
    CASES[case]={'r_pbrIBL':'0', 'r_rendererReflectionProbes':'0', 'r_lightScale':'1',
                 'r_pbrDebug':'7' if suffix.startswith('ownership-') or suffix.endswith(('-fallback','-owned')) else '0',
                 'r_rendererModernQuality':'0' if suffix=='master-off' else '1',
                 'r_pbrMaterials':'0' if suffix.endswith('-native') else '1'}
    if suffix in ('shadow-point','shadow-point-owned','shadow-projected','shadow-projected-owned'):
        CASES[case].update({'r_shadows':'1','r_useShadowMap':'1',
                           'r_shadowMapReport':'2','r_shadowMapReportInterval':'1'})
    if suffix=='cutout-hard':
        CASES[case].update({'r_pbrDebug':'7','r_msaaAlphaToCoverage':'0'})
    if suffix.startswith('aa-') and suffix.endswith('-off'):
        CASES[case]['r_vkPBRSpecularAA']='0'
    if suffix=='cutout-legacy':
        CASES[case]['r_pbrMaterials']='0'
    if suffix=='emission-legacy':
        CASES[case]['r_pbrMaterials']='0'
    if suffix in ('emission-one-light','emission-many-lights'):
        CASES[case]['r_pbrDebug']='6'
    if suffix=='emission-dark':
        CASES[case]['r_skipAmbient']='1'
    if suffix=='emission-shared':
        CASES[case]['r_rendererSharedWorldAmbient']='1'
    if suffix=='emission-extreme':
        # The manual exposure CVar's minimum is 0.1. Combine it with the
        # supported fixed auto-exposure bound to obtain an effective 0.001.
        CASES[case].update({'r_hdrToneMap':'1','r_hdrExposure':'0.1',
                           'r_hdrAutoExposure':'1','r_hdrMinExposure':'0.01','r_hdrMaxExposure':'0.01'})
    CASE_CAMERAS[case]='sampling'
for restart in ('partial','full'):
    CASE_COMMANDS['vk-direct-'+restart+'-restart']=CASE_COMMANDS[restart+'-restart']
    CASE_COMMANDS['vk-direct-cutout-'+restart+'-restart']=CASE_COMMANDS[restart+'-restart']
    CASE_COMMANDS['vk-direct-emission-'+restart+'-restart']=CASE_COMMANDS[restart+'-restart']


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture_linear(path: Path) -> array:
    raw = path.read_bytes().split(b'\n',3)
    if len(raw)!=4 or raw[0].strip()!=b'PF' or raw[2].strip()!=b'-1.0': raise ValueError('unexpected PFM header')
    width,height = map(int,raw[1].split())
    if (width,height)!=(1280,800) or len(raw[3])!=width*height*12: raise ValueError('unexpected PFM dimensions')
    values=array('f'); values.frombytes(raw[3])
    if sys.byteorder!='little': values.byteswap()
    if any(not math.isfinite(v) or v<0 for v in values): raise ValueError('invalid HDR radiance')
    return values


def inspect_linear_capture(path: Path, manifest: dict, camera: str, case: str) -> dict:
    values = capture_linear(path)
    width,height = 1280,800
    result={'peak':max(values),'width':width,'height':height}
    if case in ('hdr-auto','hdr-auto-sync'):
        result['geometricMeanLuminance']=math.exp(sum(
            math.log(max(0.2126*values[i]+0.7152*values[i+1]+0.0722*values[i+2],0.0001))
            for i in range(0,len(values),3))/(width*height))
    if camera=='overview':
        samples={}
        patches={}
        cx,cy,cz,*_=manifest['cameras'][camera]
        for station in manifest['stations']:
            x,y,z=station['origin']
            px=round(width/2+(x-cx)*height*(2/3)/(y-cy))
            py=round(height/2-(z-cz)*height*(2/3)/(y-cy))
            pixels=[values[((height-1-(py+dy))*width+px+dx)*3:((height-1-(py+dy))*width+px+dx)*3+3] for dy in range(-3,4) for dx in range(-3,4)]
            samples[station['name']]=[sum(p[c] for p in pixels)/len(pixels) for c in range(3)]
            if case.startswith(('fog-pbr','blend-pbr','lightgrid-pbr-fog','lightgrid-pbr-blend')):
                patches[station['name']]=[list(p) for p in pixels]
        result['stationRGB']=samples
        if patches: result['stationPatches']=patches
        if case in ('emissive','hdr-emissive','hdr-half-exposure','environment-off'):
            def decode(v: int) -> float:
                s=v/255
                return s/12.92 if s<=0.04045 else ((s+0.055)/1.055)**2.4
            expected=[4*decode(v) for v in (30,200,255)]
            if any(abs(x-y)>0.005 for x,y in zip(samples['emissive'],expected)):
                raise ValueError(f'overbright emissive was clamped or decoded incorrectly: {samples["emissive"]}')
        if case=='hdr-extreme-emissive':
            # Independent sRGB decode and FP16 storage reference. The red
            # channel stays below the storage ceiling; green/blue exceed it.
            expected_red=1000000*((30/255+0.055)/1.055)**2.4
            # Hardware sRGB decoding has finite precision, magnified here by
            # a million. The paired ordinary-emission control below isolates
            # that decode from the actual storage/linearity test.
            actual=samples['emissive']
            if abs(actual[0]-expected_red)>expected_red*0.002 or actual[1:]!=[65504,65504]:
                raise ValueError(f'extreme emission did not preserve finite FP16 radiance: {samples["emissive"]}')
    return result


def normal_patch(path: Path) -> bytes | None:
    if not path.is_file(): return None
    raw = path.read_bytes()
    if len(raw) < 18 or raw[2] != 2: return None
    width,height,bits = struct.unpack_from('<HHB',raw,12)
    stride = bits//8
    if (width,height)!=(1280,800) or stride not in (3,4): return None
    offset = 18+raw[0]
    # The station cameras put the same original sphere at the same distance.
    # This patch remains inside it, excluding the different surroundings.
    patch = bytearray()
    for y in range(height//2-32,height//2+33):
        row = y if raw[17]&32 else height-1-y
        for x in range(width//2-32,width//2+33):
            start = offset+(row*width+x)*stride
            patch.extend(raw[start:start+3])
    return bytes(patch)


def compare_vulkan_direct_captures(results: list[dict]) -> None:
    by_case={r['case'].removeprefix('vk-direct-'):r for r in results if r['case'].startswith('vk-direct-')}
    patches={}
    for name,row in by_case.items():
        patch=normal_patch(Path(row['screenshot']))
        if not patch:
            row['failures'].append('native direct material proof patch missing')
            continue
        patches[name]=patch
        if name in ('cutout-owned','cutout-hard','emission-cutout-owned'):
            green=sum(patch[i:i+3]==bytes((0,255,0)) for i in range(0,len(patch),3))/(len(patch)/3)
            row['nativeDirectProof']={'greenFraction':green}
            if not 0.1<green<0.9:
                row['failures'].append('native cutout must retain both material and holes')
        elif name.startswith('ownership-') or name.endswith('-owned'):
            green=sum(patch[i:i+3]==bytes((0,255,0)) for i in range(0,len(patch),3))/ (len(patch)/3)
            row['nativeDirectProof']={'greenFraction':green}
            if green<0.999:
                row['failures'].append('native direct material did not own the specimen')
        elif name=='alpha-fallback':
            # Ordered transparency composites the ownership marker through the
            # authored alpha instead of replacing the pixel, so every specimen
            # pixel must be tinted green without any of them becoming the flat
            # marker. A surface left to the classic stage shows neither.
            pixels=[patch[i:i+3] for i in range(0,len(patch),3)]
            tinted=sum(p[1]>p[0]+8 and p[1]>p[2]+8 for p in pixels)/len(pixels)
            solid=sum(p==bytes((0,255,0)) for p in pixels)/len(pixels)
            row['nativeDirectProof']={'greenTintedFraction':tinted,'solidGreenFraction':solid}
            if tinted<0.999 or solid>0.001:
                row['failures'].append('native source alpha must composite the marker through its coverage')
        elif not name.endswith(('-native','-fallback')) and name not in ('shadow-point','shadow-projected','emission-dark','emission-extreme'):
            mean=sum(patch)/len(patch)
            clipped=sum(v>=254 for v in patch)/len(patch)
            row['nativeDirectProof']={'mean':mean,'clippedFraction':clipped}
            if not 1<mean<220 or clipped>0.05:
                row['failures'].append('native direct comparison is dark or clipped')
    for left,right,limit in (
        ('scalar','packed',1), ('scalar','separate',1), ('scalar','ao-zero',0),
        ('normal-xyz','normal-rg',1.5), ('normal-xyz','normal-agb',1),
        ('normal-zero','flat',0), ('scalar','restored',0),
        ('scalar','partial-restart',0), ('scalar','full-restart',0),
        ('aa-geometric','aa-geometric-restored',0), ('aa-normal','aa-normal-restored',0),
        ('aa-constant','aa-constant-off',0), ('aa-rough','aa-rough-off',0),
        ('emission-half','emission-restored',0), ('emission-half','emission-one-light',0),
        ('emission-half','emission-many-lights',0), ('emission-half','emission-partial-restart',0),
        ('emission-half','emission-full-restart',0),
        ('emission-half','emission-shared',0),
        ('emission-mismatch-fallback','emission-mismatch-native',0),
        ('cutout-mismatch-fallback','cutout-mismatch-native',0),
        ('cutout-lit','cutout-restored',0), ('cutout-lit','cutout-partial-restart',0),
        ('cutout-lit','cutout-full-restart',0),
        ('shadow-point-off','shadow-point-restored',0),
        ('shadow-projected-off','shadow-projected-restored',0),
    ):
        if left not in patches or right not in patches: continue
        error=sum(abs(a-b) for a,b in zip(patches[left],patches[right]))/len(patches[left])
        by_case[right].setdefault('nativeDirectComparisons',{})[left]={'meanByteError':error,'limit':limit}
        if error>limit:
            by_case[right]['failures'].append(f'native direct equivalence failed against {left}: {error:.3f}')
    for left,right in (('normal-xyz','flat'),('rough-low','rough-high'),('scalar','master-off'),
                       ('alpha-native','alpha-fallback'),
                       ('shadow-point-off','shadow-point'),('shadow-projected-off','shadow-projected'),
                       ('cutout-lit','cutout-legacy'),('emission-half','emission-legacy'),
                       ('emission-half','emission-dark')):
        if left not in patches or right not in patches: continue
        error=sum(abs(a-b) for a,b in zip(patches[left],patches[right]))/len(patches[left])
        by_case[left].setdefault('nativeDirectEffects',{})[right]={'meanByteDifference':error,'minimum':0.5}
        if error<0.5:
            by_case[left]['failures'].append(f'native direct control had no effect against {right}')
    if all(name in patches for name in ('cutout-owned','cutout-hard')):
        row=by_case['cutout-owned']
        errors=[abs(a-b) for a,b in zip(patches['cutout-owned'],patches['cutout-hard'])]
        changed=sum(error>2 for error in errors)
        row['nativeCutoutCoverage']={'changedInteriorChannels':changed,'maximumByteError':max(errors)}
        log=Path(row.get('log',''))
        samples4=log.is_file() and 'Renderer AA: MSAA requested=4 effective=4' in log.read_text(errors='replace')
        if samples4 and changed<20:
            row['failures'].append('native alpha-to-coverage did not smooth interior cutout edges')
        if not samples4 and changed:
            row['failures'].append('alpha-to-coverage changed a single-sample cutout')
    # The untonemapped native target stores linear energy. These constants
    # are independently decoded from the original (30,200,255) color texture;
    # this proves material decode/scale, not full-scene display transfer.
    for name,scale in (('emission-half',0.5),('emission-quarter',0.25)):
        if name not in patches: continue
        expected=[]
        for value in (255,200,30):  # normal_patch is BGR
            encoded=value/255
            expected.append(255*scale*(encoded/12.92 if encoded<=0.04045 else ((encoded+0.055)/1.055)**2.4))
        error=max(abs(value-expected[i%3]) for i,value in enumerate(patches[name]))
        by_case[name]['nativeEmissionReference']={'expectedBGR':expected,'maximumByteError':error,'limit':1}
        if error>1:
            by_case[name]['failures'].append('native emission differs from independent linear color reference')
    if 'emission-dark' in patches and max(patches['emission-dark'])!=0:
        by_case['emission-dark']['failures'].append('emission leaked through the disabled ambient owner')
    if 'emission-extreme' in patches:
        # Red remains below the shoulder after exposure. The two other
        # channels intentionally saturate; a per-intensity clamp would reduce
        # red to about 5 instead of 77. Verify every central-patch pixel.
        expected=(255,255,(1/255/12.92)*1000000*0.001*255)
        error=max(abs(value-expected[i%3]) for i,value in enumerate(patches['emission-extreme']))
        by_case['emission-extreme']['nativeEmissionReference']={'expectedBGR':expected,'maximumByteError':error,'limit':1}
        if error>1:
            by_case['emission-extreme']['failures'].append('extreme emission lost its unclipped channel after texture modulation')
    for shape in ('geometric','normal'):
        names=['aa-'+shape+suffix for suffix in ('','-off','-owned')]
        if not all(name in by_case for name in names): continue
        row=by_case[names[0]]
        images=[capture_rgb(Path(by_case[name]['screenshot'])) for name in names]
        if any(image is None for image in images):
            row['failures'].append('specular AA requires lit, disabled and ownership captures')
            continue
        filtered,unfiltered,owner=images
        indices=[]
        # Restrict to the foreground specimen so background PBR stations
        # cannot satisfy this effect check if the tested material ignores AA.
        for y in range(320,480):
            for x in range(560,720):
                i=(y*1280+x)*3
                if all(owner[j:j+3]==bytes((0,255,0)) for j in (i,i-3,i+3,i-3840,i+3840)):
                    indices.append(i)
        changed=sum(max(abs(filtered[i+c]-unfiltered[i+c]) for c in range(3))>2 for i in indices)
        # The on/off comparison must own a visible surface and materially
        # change its highlight. Constant-normal and roughness=1 controls above
        # separately reject indiscriminate blur or a global exposure change.
        proof={'interiorPixels':len(indices),'changedPixels':changed,
               'filteredPeak':max((max(filtered[i:i+3]) for i in indices),default=0),
               'unfilteredPeak':max((max(unfiltered[i:i+3]) for i in indices),default=0)}
        row['nativeSpecularAA']=proof
        if len(indices)<1024 or changed<100 or proof['filteredPeak']<10:
            row['failures'].append('native specular AA did not change a visible, owned highlight')


def compare_normal_captures(results: list[dict]) -> None:
    by_case = {result['case']:result for result in results}
    for left,right,limit in (('normal-xyz','normal-rg',1.0),
                             ('normal-xyz','normal-agb',3.0),
                             ('normal-zero','normal-flat',0.15)):
        if left not in by_case or right not in by_case: continue
        a,b = (normal_patch(Path(by_case[name]['screenshot'])) for name in (left,right))
        if not a or not b or len(a)!=len(b): continue
        error = sum(abs(x-y) for x,y in zip(a,b))/len(a)
        by_case[left].setdefault('normalComparisons',{})[right] = {'meanAbsoluteByteError':error,'limit':limit}
        if error > limit:
            by_case[left]['failures'].append(f'normal encoding differs from {right}: mean error {error:.3f}')
        print(f'  {left}/{right}: mean normal error={error:.3f}, limit={limit}',flush=True)
    for left,right,limit in (('xyz','rg',1.0),('xyz','agb',3.0)):
        a,b=(by_case.get('lightgrid-pbr-normal-'+name) for name in (left,right))
        if a is None or b is None: continue
        x,y=(normal_patch(Path(row['screenshot'])) for row in (a,b))
        if not x or not y: continue
        error=sum(abs(v-w) for v,w in zip(x,y))/len(x)
        b['bakedNormalComparison']={'meanAbsoluteByteError':error,'limit':limit}
        if error>limit: b['failures'].append('baked light differs between equivalent normal encodings')
    a,b=(by_case.get('lightgrid-pbr-normal-'+name) for name in ('xyz','zero'))
    if a is not None and b is not None:
        x,y=(normal_patch(Path(row['screenshot'])) for row in (a,b))
        if x and y:
            error=sum(abs(v-w) for v,w in zip(x,y))/len(x)
            a['bakedNormalPerturbation']={'meanAbsoluteByteDifference':error,'minimum':0.3}
            if error<0.3: a['failures'].append('baked irradiance ignored the authored normal map')
    if 'normal-xyz' in by_case and 'normal-flat' in by_case:
        a,b = (normal_patch(Path(by_case[name]['screenshot'])) for name in ('normal-xyz','normal-flat'))
        if a and b and sum(abs(x-y) for x,y in zip(a,b))/len(a) < 4:
            by_case['normal-xyz']['failures'].append('normal map did not perturb the surface')


def compare_sampler_captures(results: list[dict]) -> None:
    """The centre patch is entirely inside the tiled original checker sphere.

    UVs exceed one: repeat must show both texels while clamp reaches black.
    Nearest filtering has no grey texels; linear and mip filtering must change
    it. These independently authored controls expose an overriding sampler.
    """
    by_case={r['case']:r for r in results if r['case'].startswith('sampler-')}
    patches={}
    for case,result in by_case.items():
        patch=normal_patch(Path(result['screenshot']))
        if not patch:
            result['failures'].append('sampler proof patch missing')
            continue
        patches[case]=patch
        low=sum(v<=1 for v in patch)/len(patch)
        high=sum(v>=254 for v in patch)/len(patch)
        middle=max(0,1-low-high)
        mean=sum(patch)/len(patch)
        result['samplerProof']={'blackFraction':low,'whiteFraction':high,'filteredFraction':middle,'mean':mean}
        if case=='sampler-nearest' and (low<0.2 or high<0.2 or middle>0.001):
            result['failures'].append('nearest repeat did not preserve the black/white checker')
        if case=='sampler-clamp' and low<0.999:
            result['failures'].append('clamp did not reach the black border texel')
        if case in ('sampler-linear','sampler-mips') and (middle<0.2 or not 85<mean<170):
            result['failures'].append('filtered repeat did not preserve the checker average')
    for left,right in (('sampler-nearest','sampler-linear'),('sampler-linear','sampler-mips')):
        if left not in patches or right not in patches: continue
        error=sum(abs(a-b) for a,b in zip(patches[left],patches[right]))/len(patches[left])
        by_case[right]['samplerProof'][left+'MeanDifference']=error
        if error<2:
            by_case[right]['failures'].append(f'{left}/{right}: authored filtering made no visible difference')


def compare_skinning_captures(results: list[dict]) -> None:
    by_case={r['case']:r for r in results if r['case'].startswith('skin-')}
    images={name:capture_rgb(Path(r['screenshot'])) for name,r in by_case.items()}
    for backend in ('cpu','gpu'):
        base,bent,restored=(f'skin-{backend}'+suffix for suffix in ('','-bent','-restored'))
        a,b=images.get(base),images.get(bent)
        if a and b:
            changed=sum(a[i:i+3]!=b[i:i+3] for i in range(0,len(a),3))
            by_case[bent]['poseChangedPixels']=changed
            if changed<1000: by_case[bent]['failures'].append('joint rotation did not visibly deform the specimen')
        a,b=images.get(base),images.get(restored)
        if a and b and a!=b:
            by_case[restored]['failures'].append('restoring the joint pose changed the reference image')
        owner=f'skin-{backend}-ownership'
        if owner in by_case:
            patch=normal_patch(Path(by_case[owner]['screenshot']))
            if not patch or sum(patch[i:i+3]==bytes((0,255,0)) for i in range(0,len(patch),3))<2000:
                by_case[owner]['failures'].append('PBR did not own the skinned specimen')
    for suffix in ('','-bent','-restored','-normal','-ownership'):
        cpu,gpu='skin-cpu'+suffix,'skin-gpu'+suffix
        a,b=images.get(cpu),images.get(gpu)
        if not a or not b: continue
        error=sum(abs(x-y) for x,y in zip(a,b))/len(a)
        indices=( (y*1280+x)*3+c for y in range(272,529) for x in range(512,769) for c in range(3) )
        specimen_error=sum(abs(a[i]-b[i]) for i in indices)/(257*257*3)
        by_case[gpu]['cpuComparison']={'meanAbsoluteByteError':error,'limit':0.5,
                                     'specimenMeanAbsoluteByteError':specimen_error,'specimenLimit':1.0}
        if error>0.5 or specimen_error>1.0: by_case[gpu]['failures'].append('GPU skinning differs from the CPU reference')
    for restart in ('partial','full'):
        case=f'skin-gpu-{restart}-restart'
        a,b=images.get('skin-gpu'),images.get(case)
        if a and b and a!=b:
            by_case[case]['failures'].append('video restart changed the frozen skinned reference image')


def compare_transparency_captures(results: list[dict]) -> None:
    """Prove ordered transparency with the pair of captures that isolates it.

    The ownership marker replaces the surface's radiance with a constant green
    and the emission view replaces it with black. Both composite through the
    same authored alpha over the same background, so their difference is the
    coverage and nothing else -- 112/255 of green here. One capture cannot do
    this: its value also carries whatever is behind the surface, which is why
    the OpenGL reference reads (118, 213, 108) rather than a bare (0, 112, 0).
    A classic-owned surface keeps its lit texture in both captures and cannot
    produce the difference at all.
    """
    for backend in sorted({r['backend'] for r in results}):
        rows = {r['case']: r for r in results if r['backend'] == backend and r['tier'] != 'legacy'}
        owned = rows.get('ownership')
        if owned is None:
            continue
        def station(row: dict | None) -> list[float] | None:
            return (row or {}).get('image', {}).get('stationRGB', {}).get('source_alpha')
        marker, emissive = station(owned), station(rows.get('emissive'))
        if marker is None:
            continue
        if emissive is None:
            # Recorded, not assumed: a run without the emission control simply
            # does not measure transparency, and its report has to say so.
            owned.setdefault('materialComparisons', {})['emissive/source_alpha'] = {
                'status': 'not measured', 'reason': 'the emissive control was not captured'}
            continue
        delta = [round(x - y, 3) for x, y in zip(marker, emissive)]
        owned.setdefault('materialComparisons', {})['emissive/source_alpha'] = {
            'deltaRGB': delta, 'expectedRGB': [0, 112, 0], 'tolerance': 1}
        if any(abs(x - y) > 1 for x, y in zip(delta, (0, 112, 0))):
            owned['failures'].append(f'native PBR source-alpha ownership missing {delta}')


def compare_material_captures(results: list[dict]) -> None:
    """Use changed controls as an oracle, including controls that must do nothing."""
    by_case = {r['case']: r for r in results if r['backend'] == 'gl' and r['tier'] != 'legacy'}
    for kind in ('fog','blend'):
        for suffix in ('-clear','','-off','-restored','-hidden-clear','-hidden','-shared'):
            baked=by_case.get('lightgrid-pbr-'+kind+suffix)
            if baked is not None: by_case.setdefault(kind+'-pbr'+suffix,baked)
    def station(case: str, name: str) -> list[float] | None:
        return by_case.get(case, {}).get('image', {}).get('stationRGB', {}).get(name)
    # source_alpha belongs to compare_transparency_captures, which runs the
    # same oracle for every backend.
    for left, right, name, expected, limit in (
        ('lit', 'direct', 'ao_zero', (0,0,0), 1),
    ):
        a,b = station(left,name), station(right,name)
        if a is None or b is None: continue
        delta = [x-y for x,y in zip(a,b)]
        by_case[left].setdefault('materialComparisons', {})[f'{right}/{name}'] = {
            'deltaRGB': delta, 'expectedRGB': expected, 'tolerance': limit}
        if any(abs(x-y)>limit for x,y in zip(delta,expected)):
            by_case[left]['failures'].append(f'{name}: incorrect {left}/{right} response {delta}')
    # Frozen time, identical camera and deterministic shaders make these exact
    # controls. In particular, disabling an expression cache cannot change light.
    exact_pairs=[('lit','no-constant-cache'),('legacy','master-off'),
                 ('production','lit'),('forced-parity','lit'),('production-shadows','shadows'),
                 ('production-no-scissor','production-no-scissor-native'),
                 ('production-rejected','production-rejected-native'),
                 ('fog-fallback','fog-native'),('blend-fallback','blend-native'),
                 ('lightgrid-fallback','lightgrid-native'),
                 ('multi-budget','multi-legacy'),('shadow-capacity','shadow-capacity-legacy'),('local-global','local-global-legacy')]
    exact_pairs += [(case,'lit') for case in ('shader-reload','image-reload','partial-restart','full-restart','map-reload')]
    exact_pairs += [(f'{kind}-restored',f'{kind}-clear') for kind in ('fog','blend')]
    exact_pairs += [(f'{kind}-pbr{suffix}',f'{kind}-pbr-clear')
                    for kind in ('fog','blend') for suffix in ('-off','-restored')]
    exact_pairs += [(f'{kind}-pbr-shared',f'{kind}-pbr') for kind in ('fog','blend')]
    exact_pairs += [('lightgrid-pbr-zero','lightgrid-pbr-clear'),('lightgrid-pbr-off','lightgrid-pbr-clear'),('lightgrid-pbr-restored','lightgrid-pbr')]
    exact_pairs += [('lightgrid-pbr-'+suffix,'lightgrid-pbr') for suffix in ('image-reload','shader-reload','partial-restart','full-restart')]
    exact_pairs += [('lightgrid-pbr-master-off','lightgrid-pbr-native'),('lightgrid-pbr-master-restored','lightgrid-pbr')]
    exact_pairs += [('production-fixed-master-off','production-fixed-native')]
    exact_pairs += [('production-minimal','hdr'),('production-fixed-minimal','production-fixed')]
    exact_pairs += [('hdr-restored','hdr')]
    exact_pairs += [('production-fixed-'+suffix,'production-fixed') for suffix in ('restored','image-reload','shader-reload','partial-restart','full-restart','resize')]
    exact_pairs += [('production-fixed-shadow-fallback','production-fixed-shadow-native')]
    exact_pairs += [('production-fixed-grid-zero','production-fixed-grid-clear'),('production-fixed-grid-restored','production-fixed-grid')]
    for left,right in exact_pairs:
        if left not in by_case or right not in by_case: continue
        a,b = (Path(by_case[name]['screenshot']) for name in (left,right))
        if a.is_file() and b.is_file() and a.read_bytes()!=b.read_bytes():
            by_case[left]['failures'].append(f'{left}/{right}: equivalent controls changed the image')
        linear_a,linear_b=(by_case[name].get('linearScreenshot') for name in (left,right))
        if linear_a and linear_b and Path(linear_a).is_file() and Path(linear_b).is_file() and Path(linear_a).read_bytes()!=Path(linear_b).read_bytes():
            by_case[left]['failures'].append(f'{left}/{right}: equivalent controls changed linear radiance')
    for grid_variant in ('','-msaa'):
        grid_rows=[by_case.get('lightgrid-pbr'+grid_variant+suffix) for suffix in ('-clear','','-double')]
        if not all(row is not None for row in grid_rows): continue
        before,after,doubled=[row.get('linearImage',{}).get('stationRGB',{}) for row in grid_rows]
        grid_rows[1]['bakedMaterialComparisons']={}
        for name,a in before.items():
            if name not in after or name not in doubled or name in ('source_alpha','cutout'): continue
            delta=[y-x for x,y in zip(a,after[name])]
            error=max(abs(z-x-2*d) for x,z,d in zip(a,doubled[name],delta))
            grid_rows[1]['bakedMaterialComparisons'][name]={'addedRadiance':delta,'doubleIntensityError':error}
            zero_diffuse=name.startswith('metal_') or name in ('ao_zero','normal_zero','normal_xyz','normal_rg','normal_agb')
            if zero_diffuse and max(abs(d) for d in delta)>0.00001:
                grid_rows[1]['failures'].append(f'{name}: metalness or AO did not suppress baked diffuse')
            if not zero_diffuse and max(delta)<0.0005:
                grid_rows[1]['failures'].append(f'{name}: baked diffuse contributed no light')
            if error>0.003:
                grid_rows[2]['failures'].append(f'{name}: baked intensity did not scale linear radiance')
        if len(grid_rows[1]['bakedMaterialComparisons'])<20:
            grid_rows[1]['failures'].append('missing linear specimens for baked-material proof')
    ibl_rows=[by_case.get('lightgrid-pbr'+suffix) for suffix in ('-clear','','-ibl-clear','-ibl','-ibl-zero')]
    if all(row is not None for row in ibl_rows):
        direct,baked,environment,mixed,zero=[row.get('linearImage',{}).get('stationRGB',{}) for row in ibl_rows]
        comparisons={}
        for name in direct:
            if name in ('source_alpha','cutout') or not all(name in row for row in (baked,environment,mixed,zero)): continue
            added=[y-x for x,y in zip(direct[name],baked[name])]
            error=max(abs(y-z-d) for y,z,d in zip(mixed[name],zero[name],added))
            comparisons[name]={'bakedIBLIndependenceError':error}
            if error>0.004: ibl_rows[3]['failures'].append(f'{name}: baked diffuse changes when IBL is enabled')
            if name.startswith('dielectric_') and max(x-y for x,y in zip(environment[name],zero[name]))<0.001:
                ibl_rows[4]['failures'].append(f'{name}: baked grid did not replace environment diffuse')
            if name.startswith('metal_'):
                if max(abs(x-y) for x,y in zip(environment[name],mixed[name]))>0.00001:
                    ibl_rows[3]['failures'].append(f'{name}: baked grid changed metallic environment specular')
                if max(y-x for x,y in zip(direct[name],mixed[name]))<0.005:
                    ibl_rows[3]['failures'].append(f'{name}: environment specular disappeared with baked lighting')
        ibl_rows[3]['bakedIBLComparisons']=comparisons
    for kind,variant in ((kind,variant) for kind in ('fog','blend') for variant in ('','-msaa')):
        lit,clear=(by_case.get(f'{kind}-pbr{variant}{suffix}') for suffix in ('','-clear'))
        if lit is None or clear is None: continue
        before=clear.get('linearImage',{}).get('stationRGB',{})
        after=lit.get('linearImage',{}).get('stationRGB',{})
        comparisons={}
        for name,a in before.items():
            if name in ('source_alpha','cutout') or name not in after: continue
            b=after[name]
            if kind=='blend':
                error=max(abs(y-x*scale) for x,y,scale in zip(a,b,(0.4,0.7,0.2)))
                comparisons[name]={'maximumRadianceError':error}
                if error>0.005:
                    lit['failures'].append(f'{name}: authored blend light did not multiply linear radiance once')
            else:
                alpha=[(y-x)/(color-x) for x,y,color in zip(a,b,(0.18,0.32,0.48)) if abs(color-x)>0.05]
                comparisons[name]={'inferredFogAlpha':alpha}
                if not alpha or min(alpha)<0.01 or max(alpha)>0.95 or max(alpha)-min(alpha)>0.015:
                    lit['failures'].append(f'{name}: fog did not compose a consistent nonopaque linear contribution')
            source=clear.get('linearImage',{}).get('stationPatches',{}).get(name,[])
            target=lit.get('linearImage',{}).get('stationPatches',{}).get(name,[])
            if source and target:
                if kind=='fog':
                    alphas=[]
                    for x,y in zip(source,target):
                        channel=max(range(3),key=lambda c:abs((0.18,0.32,0.48)[c]-x[c]))
                        denominator=(0.18,0.32,0.48)[channel]-x[channel]
                        if abs(denominator)>0.05: alphas.append((y[channel]-x[channel])/denominator)
                    comparisons[name]['pixelAlphaRange']=[min(alphas),max(alphas)] if alphas else []
                    if not alphas or min(alphas)<0.01 or max(alphas)-min(alphas)>0.015:
                        lit['failures'].append(f'{name}: fog has holes or discontinuities inside an opaque receiver')
                else:
                    error=max(abs(y[c]-x[c]*(0.4,0.7,0.2)[c]) for x,y in zip(source,target) for c in range(3))
                    comparisons[name]['maximumPixelRadianceError']=error
                    if error>0.005:
                        lit['failures'].append(f'{name}: blend light has holes or duplicate contributions')
        lit['linearOverlayComparisons']=comparisons
        if before and len(comparisons)<20:
            lit['failures'].append('too few opaque specimens for linear fog/blend proof')
    for kind in ('fog','blend'):
        rows=[by_case.get(f'{kind}-pbr{suffix}') for suffix in ('-clear','','-hidden-clear','-hidden')]
        if any(row is None for row in rows): continue
        patches=[row.get('linearImage',{}).get('stationPatches',{}).get('source_alpha',[]) for row in rows]
        if not all(patches): continue
        a,b,hidden_a,hidden_b=patches
        # Removing the transparent sphere reveals its background. Fog/blend
        # changes that background before source-alpha composition; the sphere's
        # own radiance must remain outside the authored opaque lighting phase.
        transmittance=1-112/255
        error=max(abs((y[c]-x[c])-(hy[c]-hx[c])*transmittance)
                  for x,y,hx,hy in zip(a,b,hidden_a,hidden_b) for c in range(3))
        rows[1]['transparentOverlayComparison']={'maximumRadianceError':error,'transmittance':transmittance}
        if error>0.005:
            rows[1]['failures'].append('source-alpha geometry composed in the wrong fog/blend phase')
    for kind in ('fog','blend','lightgrid'):
        left,right=f'{kind}-native',f'{kind}-off'
        if left in by_case and right in by_case:
            a,b=(capture_rgb(Path(by_case[name]['screenshot'])) for name in (left,right))
            if a and b:
                changed=sum(max(abs(a[i+c]-b[i+c]) for c in range(3))>3 for i in range(0,len(a),3))
                by_case[left]['overlayComparison']={'changedPixels':changed}
                if changed<4096: by_case[left]['failures'].append(f'{kind} did not visibly affect the native scene')
                if kind=='fog':
                    visible_colors=len({a[i:i+3] for i in range(0,len(a),3)})
                    by_case[left]['overlayComparison']['visibleColors']=visible_colors
                    if visible_colors<128:
                        by_case[left]['failures'].append('fog hid the reference geometry; composition proof is inconclusive')
    # The backdrop remains an authored classic diffuse material. Independently
    # render it through the native owner and compare four unoccluded patches;
    # a PBR ownership marker alone cannot establish classic compatibility.
    for case in ('production','production-points','production-projector'):
        if case not in by_case or case+'-native' not in by_case: continue
        a,b=(capture_rgb(Path(by_case[name]['screenshot'])) for name in (case,case+'-native'))
        if a and b:
            patches=[]
            for x0,y0,x1,y1 in ((480,30,800,90),(40,400,90,600),(1150,300,1220,450),(20,760,200,790),
                              (0,300,4,500),(1276,300,1280,500),(450,0,750,4),(20,796,200,800)):
                errors=[abs(a[(y*1280+x)*3+c]-b[(y*1280+x)*3+c])
                        for y in range(y0,y1) for x in range(x0,x1) for c in range(3)]
                patches.append({'rect':[x0,y0,x1,y1],'meanAbsoluteByteError':sum(errors)/len(errors),'maximumByteError':max(errors)})
            by_case[case]['classicComparison']=patches
            if any(p['meanAbsoluteByteError']>1.0 or p['maximumByteError']>3 for p in patches):
                by_case[case]['failures'].append('classic backdrop differs from the native lighting reference')
    if 'production-curved' in by_case and 'production-curved-native' in by_case:
        a,b=(capture_rgb(Path(by_case[name]['screenshot'])) for name in ('production-curved','production-curved-native'))
        if a and b:
            errors=[abs(a[(y*1280+x)*3+c]-b[(y*1280+x)*3+c]) for y in range(320,361) for x in range(686,727) for c in range(3)]
            proof={'meanAbsoluteByteError':sum(errors)/len(errors),'maximumByteError':max(errors)}
            by_case['production-curved']['classicComparison']=proof
            if proof['meanAbsoluteByteError']>1 or proof['maximumByteError']>3:
                by_case['production-curved']['failures'].append('curved classic surface differs from the native lighting reference')
    for case in ('production-fixed','production-fixed-bump','production-fixed-diffuse','production-fixed-colored','production-fixed-grid'):
        if case not in by_case or case+'-native' not in by_case: continue
        modern=by_case[case]
        pfm=Path(modern.get('linearScreenshot',''))
        native=capture_rgb(Path(by_case[case+'-native']['screenshot']))
        if not pfm.is_file() or native is None:
            modern['failures'].append('classic linear/native comparison inputs missing')
            continue
        values=capture_linear(pfm)
        coordinates=[(x,y) for y in range(296,385) for x in range(663,752) if (x-707)**2+(y-340)**2<44**2]
        encoded=[]; reference=[]
        for x,y in coordinates:
            for c in range(3):
                v=values[((799-y)*1280+x)*3+c]
                encoded.append(255*(12.92*v if v<=0.0031308 else 1.055*v**(1/2.4)-0.055))
                reference.append(native[(y*1280+x)*3+c])
        errors=[abs(a-b) for a,b in zip(encoded,reference)]
        proof={'samples':len(errors),'radius':44,'meanAbsoluteByteError':sum(errors)/len(errors),
               'maximumByteError':max(errors),'nativeRange':[min(reference),max(reference)],
               'encodedRange':[min(encoded),max(encoded)],'comparison':'linear PFM encoded to sRGB versus unclipped native display'}
        modern['classicComparison']=proof
        if max(reference)>=240 or max(encoded)>=240 or max(reference)-min(reference)<40:
            modern['failures'].append('classic reference is clipped or lacks a useful lighting range')
        if proof['meanAbsoluteByteError']>1 or proof['maximumByteError']>3:
            modern['failures'].append('classic interaction differs from the native lighting reference')
    for left,right,label in (('production-fixed','production-fixed-diffuse','specular'),
                             ('production-fixed-bump','production-fixed','normal'),
                             ('production-fixed-colored','production-fixed-bump','colored specular'),
                             ('production-fixed-grid','production-fixed-grid-clear','baked lighting')):
        if left not in by_case or right not in by_case: continue
        paths=[Path(by_case[name].get('linearScreenshot','')) for name in (left,right)]
        if not all(p.is_file() for p in paths): continue
        a,b=[capture_linear(p) for p in paths]
        errors=[abs(a[((799-y)*1280+x)*3+c]-b[((799-y)*1280+x)*3+c])
                for y in range(296,385) for x in range(663,752) if (x-707)**2+(y-340)**2<44**2 for c in range(3)]
        delta=sum(errors)/len(errors)
        by_case[left].setdefault('classicDifferentials',{})[label]=delta
        if delta<0.0005: by_case[left]['failures'].append(f'classic {label} had no useful effect')
    # Probe telemetry alone cannot prove that a shader consumed its radiance.
    a,b = station('lit','metal_3'), station('no-probes','metal_3')
    if a is not None and b is not None and max(abs(x-y) for x,y in zip(a,b))<3:
        by_case['lit']['failures'].append('authored probes did not change the metallic surface')
    a,b = station('environment-only','metal_3'), station('environment-off','metal_3')
    if a is not None and b is not None:
        if max(a)<3 or max(b)>1 or max(abs(x-y) for x,y in zip(a,b))<3:
            by_case['environment-only']['failures'].append('environment-only lighting missing, or disabled IBL still lights metal')
        # The emissive specimen also reflects light; only its emitted lobe
        # is invariant. environment-off's PFM oracle checks that exact lobe.
        for name in ('ao_zero',):
            a,b=station('environment-only',name),station('environment-off',name)
            if a is not None and b is not None and max(abs(x-y) for x,y in zip(a,b))>1:
                by_case['environment-only']['failures'].append(f'environment lighting incorrectly changes {name}')
    # Independently calculated display references for the original emissive
    # texel (30,200,255)*4, filmic reference white 6 and one sRGB encoding.
    # A missing encode, second exposure, clamp-before-tone, or old SDR shoulder
    # misses these values. The separate PFM oracle requires unchanged HDR input.
    for case,expected in (('hdr-emissive',(61.44,247.78,252.75)),
                          ('hdr-half-exposure',(33.82,235.94,245.95))):
        actual=station(case,'emissive')
        if actual is None: continue
        by_case[case]['displayReference']={'actualRGB':actual,'expectedRGB':expected,'tolerance':1.5}
        if any(abs(a-b)>1.5 for a,b in zip(actual,expected)):
            by_case[case]['failures'].append('incorrect linear HDR tone-map/output transfer')
    ordinary=by_case.get('hdr-emissive',{}).get('linearImage',{}).get('stationRGB',{}).get('emissive')
    extreme=by_case.get('hdr-extreme-emissive')
    if extreme:
        radiance=extreme.get('linearImage',{}).get('stationRGB',{}).get('emissive')
        if not ordinary or not radiance:
            extreme['failures'].append('extreme HDR proof requires the ordinary-emission float control')
        else:
            expected=ordinary[0]*250000
            # Bound both FP16 stores' rounding after scaling the first sample.
            tolerance=2**(math.floor(math.log2(ordinary[0]))-11)*250000 + 2**(math.floor(math.log2(expected))-11)
            extreme['radianceLinearity']={'actualRed':radiance[0],'expectedRed':expected,'maxError':tolerance}
            if abs(radiance[0]-expected)>tolerance:
                extreme['failures'].append('unclipped extreme HDR channel lost emission linearity')


def capture_rgb(path: Path) -> bytes | None:
    if not path.is_file(): return None
    raw=path.read_bytes()
    if len(raw)<18 or raw[2]!=2: return None
    width,height,bits=struct.unpack_from('<HHB',raw,12)
    stride=bits//8
    if (width,height)!=(1280,800) or stride not in (3,4): return None
    pixels=raw[18+raw[0]:]
    if len(pixels)!=width*height*stride: return None
    rows=[]
    for y in range(height):
        row=y if raw[17]&32 else height-1-y
        start=row*width*stride
        rows.append(bytes(pixels[start+x*stride+c] for x in range(width) for c in (2,1,0)))
    return b''.join(rows)


def compare_post_captures(results: list[dict]) -> None:
    by_case={r['case']:r for r in results if r['backend']=='gl' and r['tier']!='legacy'}
    images={}
    for name in ('hdr','hdr-half','hdr-hud','hdr-hud-half','bloom','hdr-auto','hdr-auto-sync','hdr-auto-clamped'):
        if name in by_case: images[name]=capture_rgb(Path(by_case[name]['screenshot']))
    for left,right,mean_limit,max_limit in (('hdr-auto-clamped','hdr-half',1,3),('hdr-auto','hdr-auto-sync',0.15,2)):
        a,b=images.get(left),images.get(right)
        if a and b:
            errors=[abs(x-y) for x,y in zip(a,b)]
            proof={'meanAbsoluteByteError':sum(errors)/len(errors),'maximumByteError':max(errors)}
            by_case[left]['exposureComparison']=proof
            if proof['meanAbsoluteByteError']>mean_limit or proof['maximumByteError']>max_limit:
                by_case[left]['failures'].append(f'{left}/{right}: exposure paths disagree')
    a,b=images.get('bloom'),images.get('hdr')
    if a and b:
        brightened=darkened=0
        for i in range(0,len(a),3):
            delta=[a[i+c]-b[i+c] for c in range(3)]
            brightened+=max(delta)>2
            darkened+=min(delta)<-2
        by_case['bloom']['bloomComparison']={'brightenedPixels':brightened,'darkenedPixels':darkened}
        if brightened<1024 or darkened>1280*800//100:
            by_case['bloom']['failures'].append('bloom did not add a visible highlight halo without darkening the scene')
    names=('hdr','hdr-half','hdr-hud','hdr-hud-half')
    if all(images.get(n) for n in names):
        scene,half,hud,hud_half=(images[n] for n in names)
        visible=stable_opaque=0
        for i in range(0,len(scene),3):
            if max(abs(hud[i+c]-scene[i+c]) for c in range(3))<12: continue
            visible+=1
            # Midtone opaque UI is sensitive to an accidental second tone-map.
            # Require real contrast behind it at both exposures, and enough
            # unchanged colored pixels to exclude a missing/black HUD.
            if (20<max(hud[i:i+3])<240 and
                max(abs(scene[i+c]-half[i+c]) for c in range(3))>10 and
                max(abs(hud[i+c]-hud_half[i+c]) for c in range(3))<=1):
                stable_opaque+=1
        by_case['hdr-hud']['uiComparison']={'visiblePixels':visible,'stableMidtonePixels':stable_opaque}
        if visible<512 or stable_opaque<64:
            by_case['hdr-hud']['failures'].append('HUD is missing or its midtones changed with scene exposure')
    # UI, bloom and exposure must leave the original scene radiance intact.
    reference=by_case.get('hdr',{}).get('linearScreenshot')
    if reference and Path(reference).is_file():
        reference_hash=digest(Path(reference))
        for name in images:
            candidate=by_case[name].get('linearScreenshot')
            if candidate and Path(candidate).is_file() and digest(Path(candidate))!=reference_hash:
                by_case[name]['failures'].append('post processing changed the captured source scene radiance')
    for name in ('hdr-auto','hdr-auto-sync','hdr-auto-clamped'):
        if name not in by_case: continue
        result=by_case[name]
        state=result.get('hdrState',{})
        if state.get('auto')!=1 or state.get('initialized')!=1 or state.get('levels',0)<2:
            result['failures'].append('HDR auto exposure did not consume a luminance pyramid')
            continue
        luminance=state.get('luminance',0)
        lo=hi=0.5
        if name!='hdr-auto-clamped': lo,hi=0.25,8
        target=max(lo,min(hi,0.18/max(luminance,0.0001)))
        if abs(target-state.get('target',0))>1e-5 or abs(target-state.get('adapted',0))>0.001:
            result['failures'].append('HDR exposure did not converge to the bounded middle-gray reference')
        measured=result.get('linearImage',{}).get('geometricMeanLuminance')
        if measured:
            result['luminanceComparison']={'capturedMean':measured,'gpuMean':luminance,'relativeError':abs(measured-luminance)/measured}
            # Bilinear, non-power-of-two downsampling is an approximation;
            # this still rejects gamma-space luminance or a stale scene sample.
            if abs(measured-luminance)>measured*0.1:
                result['failures'].append('GPU luminance disagrees with independent linear capture integration')


def ownership_edge_coverage(plain: bytes, antialiased: bytes) -> dict:
    """Measure geometry coverage using the flat ownership marker, not lighting."""
    green=bytes((0,255,0)); partial=0; interior_changed=0; interior=0
    for y in range(1,799):
        for x in range(1,1279):
            i=(y*1280+x)*3
            if plain[i:i+3]!=green: continue
            neighbours=[i-3,i+3,i-1280*3,i+1280*3]
            edge=any(plain[j:j+3]!=green for j in neighbours)
            if edge and 4<antialiased[i+1]<251: partial+=1
            if not edge:
                interior+=1
                interior_changed+=antialiased[i:i+3]!=green
    return {'partialSilhouettePixels':partial,'changedInteriorPixels':interior_changed,
            'interiorPixels':interior}


def compare_vulkan_msaa_reports(reference: dict, multisampled: dict) -> dict:
    """Compare separately captured 0x/4x native controls with pinned provenance."""
    proof={'failures':[]}
    for field in ('runtimeSHA256','compiledMapSHA256','fixture'):
        if not reference.get(field) or reference.get(field)!=multisampled.get(field):
            proof['failures'].append('MSAA reports differ in '+field)
    images=[]
    for report,samples in ((reference,0),(multisampled,4)):
        if not report.get('complete') or any(r.get('failures') for r in report.get('results',[])):
            proof['failures'].append(f'{samples}x report is incomplete or failed')
        rows=[r for r in report.get('results',[]) if r.get('case')=='vk-direct-ownership-scalar']
        if len(rows)!=1:
            proof['failures'].append(f'{samples}x report needs one native scalar ownership capture')
            continue
        row=rows[0]
        if row.get('backend')!='vk' or row.get('camera')!='sampling':
            proof['failures'].append(f'{samples}x ownership capture has the wrong backend or camera')
        # Inspect the retained engine log: requested CVars alone cannot prove
        # that the color and depth attachments actually used four samples.
        log=Path(row.get('log',''))
        if (not log.is_file() or digest(log)!=row.get('sha256',{}).get('log')
                or f'Renderer AA: MSAA requested={samples} effective={samples}' not in log.read_text(errors='replace')):
            proof['failures'].append(f'{samples}x capture lacks intact native AA telemetry')
        path=Path(row.get('screenshot',''))
        if not path.is_file() or digest(path)!=row.get('sha256',{}).get('screenshot'):
            proof['failures'].append(f'{samples}x ownership capture hash does not match')
            continue
        image=capture_rgb(path)
        if image is None:
            proof['failures'].append(f'{samples}x ownership image is invalid')
        else:
            images.append(image)
    if proof['failures'] or len(images)!=2: return proof
    proof.update(ownership_edge_coverage(*images))
    if proof['partialSilhouettePixels']<100 or proof['interiorPixels']<1024:
        proof['failures'].append('native MSAA did not resolve fractional geometry coverage')
    if proof['changedInteriorPixels']>16:
        proof['failures'].append('native MSAA changed flat interior ownership')
    return proof


def compare_msaa_captures(results: list[dict]) -> None:
    by_case={r['case']:r for r in results}
    fallback=by_case.get('production-fixed-msaa-fallback')
    reference=by_case.get('production-fixed-msaa-native')
    if fallback and reference:
        a,b=(capture_rgb(Path(r['screenshot'])) for r in (fallback,reference))
        if a and b:
            errors=[abs(x-y) for x,y in zip(a,b)]
            # The native forward target is RGBA8, before and after resolve;
            # reading floats cannot recover more precision from that image.
            # Keep the existing MSAA display-channel budget, but allow only
            # one UNORM8 step (the HDR resolve budget permits four bytes).
            evidence={'displayChangedChannels':sum(e!=0 for e in errors),
                      'displayMaxByteError':max(errors),
                      'limits':{'displayChangedChannels':16,'displayMaxByteError':1}}
            fallback['nativeMSAAFallbackComparison']=evidence
            if evidence['displayChangedChannels']>16 or evidence['displayMaxByteError']>1:
                fallback['failures'].append('native MSAA fallback changed the scene beyond UNORM8 rounding')
    native=by_case.get('production-fixed-native')
    multisampled=by_case.get('production-fixed-msaa-native')
    if native and multisampled:
        a,b=(capture_rgb(Path(r['screenshot'])) for r in (native,multisampled))
        if a and b:
            changed=sum(max(abs(a[i+c]-b[i+c]) for c in range(3))>1 for i in range(0,len(a),3))
            multisampled['nativeMSAAComparison']={'changedPixelsAboveOneByte':changed}
            if changed<100:
                multisampled['failures'].append('native MSAA control produced no meaningful coverage change')
    pairs=[(case,'msaa') for case in ('msaa-restored','msaa-partial-restart','msaa-full-restart')]
    pairs += [(f'{kind}-pbr-msaa{suffix}',f'{kind}-pbr-msaa-clear')
              for kind in ('fog','blend') for suffix in ('-off','-restored')]
    pairs += [('lightgrid-pbr-msaa'+suffix,'lightgrid-pbr-msaa-clear') for suffix in ('-zero','-off')]
    pairs += [('lightgrid-pbr-msaa-restored','lightgrid-pbr-msaa')]
    for case,reference in pairs:
        if case not in by_case or reference not in by_case: continue
        initial,restarted=by_case[reference],by_case[case]
        operation='restart' if case.endswith('-restart') else 'overlay restore'
        a,b=(capture_rgb(Path(r['screenshot'])) for r in (initial,restarted))
        if not a or not b: continue
        display_errors=[abs(x-y) for x,y in zip(a,b)]
        evidence={'displayChangedChannels':sum(e!=0 for e in display_errors),
                  'displayMaxByteError':max(display_errors)}
        restarted['restartComparison' if operation=='restart' else 'overlayRestoreComparison']=evidence
        # Multisample resolves can differ by one FP16 rounding step. Require
        # float evidence as well as a tightly bounded display difference, so a
        # changed light/material or missing resolve cannot pass as rounding.
        paths=[Path(r.get('linearScreenshot','')) for r in (initial,restarted)]
        if not all(p.is_file() for p in paths):
            restarted['failures'].append(f'MSAA {operation} comparison requires float captures')
            continue
        try:
            x,y=(capture_linear(p) for p in paths)
        except (OSError,ValueError) as error:
            restarted['failures'].append(f'invalid MSAA {operation} float capture: {error}')
            continue
        ulps=[]
        for left,right in zip(x,y):
            if left==right: continue
            magnitude=max(abs(left),abs(right))
            half_step=2.0**max(-24,math.floor(math.log2(magnitude))-10)
            ulps.append(abs(left-right)/half_step)
        evidence.update({'floatChangedChannels':len(ulps),'floatMaxHalfULPs':max(ulps,default=0),
                         'limits':{'displayChangedChannels':16,'displayMaxByteError':4,
                                   'floatChangedChannels':128,'floatMaxHalfULPs':1}})
        if (evidence['displayChangedChannels']>16 or evidence['displayMaxByteError']>4
                or len(ulps)>128 or evidence['floatMaxHalfULPs']>1):
            restarted['failures'].append(f'MSAA {operation} changed the scene beyond FP16 rounding')
    if all(case in by_case for case in ('msaa-cutout','msaa-cutout-hard')):
        soft,hard=(capture_rgb(Path(by_case[c]['screenshot'])) for c in ('msaa-cutout','msaa-cutout-hard'))
        if soft and hard:
            changed=sum(max(abs(soft[(y*1280+x)*3+c]-hard[(y*1280+x)*3+c]) for c in range(3))>2
                        for y in range(375,426) for x in range(615,666))
            by_case['msaa-cutout']['cutoutCoverage']={'changedInteriorPixels':changed}
            if changed<20:
                by_case['msaa-cutout']['failures'].append('alpha-to-coverage did not smooth interior cutout boundaries')
    if not all(case in by_case for case in ('ownership','msaa-ownership')): return
    plain,antialiased=(capture_rgb(Path(by_case[c]['screenshot'])) for c in ('ownership','msaa-ownership'))
    if not plain or not antialiased: return
    # Ownership is a flat green material marker, so a changed lit highlight
    # cannot masquerade as geometric antialiasing. Four samples produce
    # intermediate coverage along a silhouette that was fully covered before.
    proof=ownership_edge_coverage(plain,antialiased)
    by_case['msaa-ownership']['edgeCoverage']=proof
    if proof['partialSilhouettePixels']<100:
        by_case['msaa-ownership']['failures'].append('MSAA did not resolve fractional geometry coverage')


def compare_shadow_captures(results: list[dict]) -> None:
    by_case={r['case']:r for r in results}
    images={k:capture_rgb(Path(v['screenshot'])) for k,v in by_case.items()}
    def delta(left: str,right: str,exclude_caster: bool=False) -> dict | None:
        a,b=images.get(left),images.get(right)
        if not a or not b: return None
        count=total=changed=0
        for y in range(800):
            for x in range(1280):
                # Union of the projected old/new radius-72 sphere bounds,
                # expanded by 12 pixels. All remaining pixels are fixed
                # receivers: moving the visible sphere cannot change them.
                if exclude_caster and 638<=x<=815 and 247<=y<=408: continue
                i=(y*1280+x)*3
                difference=max(abs(a[i+c]-b[i+c]) for c in range(3))
                changed+=difference>2; total+=difference; count+=1
        result={'changedPixels':changed,'meanMaxChannelDifference':total/count}
        by_case[left].setdefault('shadowComparisons',{})[right]=result
        return result
    for left,right in (('shadows','lit'),('multi-shadows','multi-unshadowed'),('msaa-shadows','msaa')):
        response=delta(left,right)
        if response and response['changedPixels']<1000:
            by_case[left]['failures'].append('shadow resources did not produce a visible shadow')
    if images.get('shadows') and images.get('lit'):
        # These room patches cannot be occluded by any station for either
        # shadow-casting light. They detect slope/quantization acne while the
        # full-frame differential above independently requires real shadows.
        a,b=images['shadows'],images['lit']
        patches=[]
        for x0,y0,x1,y1 in ((480,30,800,90),(40,400,90,600),(1150,300,1220,450),(20,760,200,790)):
            errors=[max(abs(a[(y*1280+x)*3+c]-b[(y*1280+x)*3+c]) for c in range(3)) for y in range(y0,y1) for x in range(x0,x1)]
            patches.append({'rect':[x0,y0,x1,y1],'meanMaxChannelDifference':sum(errors)/len(errors),'maxDifference':max(errors)})
        by_case['shadows']['unoccludedReceiverPatches']=patches
        if any(p['meanMaxChannelDifference']>0.1 or p['maxDifference']>2 for p in patches):
            by_case['shadows']['failures'].append('shadow acne on an unoccluded planar receiver')
    for control in ('shadows-manual-point','shadows-cache-off','shadow-restored'):
        response=delta(control,'shadows')
        if response and response['meanMaxChannelDifference']>0.1:
            by_case[control]['failures'].append('equivalent shadow controls changed the image')
    response=delta('shadow-moved','shadows',True)
    if response and response['changedPixels']<200:
        by_case['shadow-moved']['failures'].append('moving the caster did not move its shadow on fixed receivers')
    negative=delta('unshadowed-moved','lit',True)
    if negative and negative['changedPixels']>100:
        by_case['unshadowed-moved']['failures'].append('motion control changed fixed receivers with shadows disabled')


def run(command: list[str], cwd: Path, output: Path, timeout: int) -> tuple[int, float]:
    start = time.monotonic()
    with output.open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
            # Keep a machine-readable failed report as well as the command/log.
            print(f'Engine timed out after {timeout}s; see {output}',flush=True)
            code = 124
    return code, time.monotonic()-start


def engine_args(cvars: dict[str, str]) -> list[str]:
    return [item for key, value in cvars.items() for item in ('+set', key, value)]


def startup_args(cvars: dict[str, str], save: Path) -> list[str]:
    # The engine accepts at most 64 +commands. Put the full profile in its
    # supported pre-renderer autoexec path, retaining bootstrap/safety on CLI.
    config = save/'baseoq4/autoexec.cfg'
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text('\n'.join(f'set {k} "{v}"' for k,v in cvars.items())+'\n',encoding='utf-8')
    early = ('fs_game','fs_basepath','fs_savepath','fs_devpath','r_renderApi','r_glTier',
             'r_fullscreen','r_hiddenWindow','in_mouse','s_noSound','logFile','logFileName',
             'win_allowMultipleInstances','sys_allowMultipleInstances')
    # The first-run menu can apply detected quality after autoexec and game
    # initialization. Replay the profile as a startup command before +map so
    # its game render targets use the requested MSAA/post settings too.
    return [*engine_args({k:cvars[k] for k in early if k in cvars}), '+exec', 'autoexec.cfg']


def prepare(runtime: Path, basepath: Path, output: Path, timeout: int) -> None:
    if runtime.exists():
        raise FileExistsError(f'Refusing to replace existing runtime: {runtime}')
    staged = ROOT / '.install'
    shutil.copytree(staged, runtime, ignore=shutil.ignore_patterns('*.pdb', '*.lib', '*.exp'))
    lab.generate(runtime)
    compile_fixture(runtime,basepath,output,timeout)


def compile_fixture(runtime: Path, basepath: Path, output: Path, timeout: int) -> None:
    compile_save = output/'compile'
    compile_save.mkdir(parents=True)
    args = {**BASE, 'fs_game': 'baseoq4', 'fs_basepath': str(basepath), 'fs_savepath': str(compile_save), 'fs_devpath': str(runtime), 'r_rendererModernExecutor': '0', 'r_renderApi': 'gl'}
    executable = next(runtime.glob('openQ4-ded_*.exe')) if os.name == 'nt' else next(runtime.glob('openQ4-ded_*'))
    command = [str(executable), *startup_args(args,compile_save), '+dmap', lab.MAP, '+quit']
    (compile_save/'command.json').write_text(json.dumps(command,indent=2))
    code, _ = run(command, runtime, compile_save/'process.log', timeout)
    engine_log = compile_save/'baseoq4/logs/openq4.log'
    text = engine_log.read_text(errors='replace') if engine_log.exists() else ''
    proc = runtime/'baseoq4'/f'{lab.MAP}.proc'
    if code or not proc.is_file() or re.search(r'(?i)\b(?:leaked|fatal error|ERROR:)\b', text):
        raise RuntimeError(f'PBR map did not compile cleanly; see {compile_save}')


def vulkan_backend_failures(text: str) -> list[str]:
    failures = []
    if 'Renderer API: requested=vulkan active=vulkan disposition=module' not in text:
        failures.append('native Vulkan renderer identity missing')
    if 'Vulkan: validation enabled (VK_LAYER_KHRONOS_validation, debug messenger active)' not in text:
        failures.append('active Vulkan validation layer and debug messenger missing')
    if re.search(r'Renderer API:.*active=(?!vulkan\b)\w+',text):
        failures.append('Vulkan laboratory switched to another renderer')
    return failures


def inspect_capture(args: argparse.Namespace, case: str, text: str, shot: Path) -> dict:
    failures = []
    if not shot.is_file(): failures.append('engine screenshot missing')
    diagnostics = diagnostic_lines(text)
    if diagnostics: failures.append('engine diagnostics require review')
    telemetry = [line for line in text.splitlines() if line.startswith(('PBR material resources:', 'Modern GL executor:', 'Modern visible frame:', 'Modern classic lighting:', 'Modern forward+:', 'Modern current shadow map:', 'Modern scene MSAA:', 'Renderer AA:', 'Modern specular probe atlas:', 'Modern clustered specular probes:', 'Vulkan: native PBR', 'GPU skinning:', 'modernLightingOwnership'))]
    if case.startswith('vk-direct-'):
        samples=CASES[case].get('r_multiSamples',BASE['r_multiSamples'])
        if not re.search(rf'Renderer AA: MSAA requested={samples} effective={samples}\b',text):
            failures.append('native direct specimen did not render at its requested sample count')
    if case=='vk-direct-emission-extreme':
        hdr=next((line for line in text.splitlines() if line.startswith('Vulkan HDR:')), '')
        telemetry.append(hdr)
        state=dict(re.findall(r'(\w+)=([^\s]+)',hdr))
        try:
            valid=(state.get('sceneRequested')=='1' and state.get('sceneFormat')=='RGBA16F'
                   and state.get('autoExposure')=='1' and state.get('initialized')=='1'
                   and int(state.get('completed','0'))>0 and float(state.get('exposure','0'))==0.01)
        except ValueError:
            valid=False
        if not valid: failures.append('extreme emission requires completed float-HDR exposure evidence')
    if case in ('production-fixed','production-fixed-bump','production-fixed-colored','production-fixed-restored','production-fixed-image-reload','production-fixed-shader-reload','production-fixed-partial-restart','production-fixed-full-restart','production-fixed-resize','production-fixed-minimal','production-fixed-bright') and not re.search(r'Modern classic lighting: ready=1 executed=1 primitives=[1-9]\d*',text):
        failures.append('classic compatibility lighting was not submitted')
    if case.startswith('production-fixed-msaa') and not re.search(r'Renderer AA: MSAA requested=4 effective=4\b',text):
        failures.append('four-sample fallback control did not activate MSAA')
    if case.startswith('production-fixed-') and case.endswith('-fallback') and not re.search(r'Modern classic lighting: ready=0 executed=0 primitives=0\b',text):
        failures.append('unsupported classic lighting left a partial transaction active')
    if case.startswith('production-fixed-grid') and not case.endswith('-native'):
        if not re.search(r'Modern classic lighting: ready=1 executed=1 primitives=[1-9]\d*',text):
            failures.append('classic compatibility lighting was not submitted with the baked grid')
        if not case.endswith('-clear') and not re.search(r'Modern forward\+:.*lightGrid=[1-9]\d*',text):
            failures.append('baked grid was not submitted with classic compatibility lighting')
    if case.startswith('skin-gpu'):
        gpu=next((line for line in telemetry if line.startswith('GPU skinning:')), '')
        if not re.search(r'\benabled=1\b',gpu) or not re.search(r'\bprepared=[1-9]\d*\b',gpu):
            failures.append('GPU skinning did not consume the specimen')
    if case.startswith('msaa') or '-pbr-msaa' in case:
        if not re.search(r'Renderer AA: MSAA requested=4 effective=4\b',text):
            failures.append('four-sample MSAA was requested but is not effective')
        if not re.search(r'Modern scene MSAA: samples=4 colorResolves=[1-9]\d* depthResolves=[1-9]\d*',text):
            failures.append('multisample scene color and depth were not resolved')
    active_pbr = case not in LEGACY_CASES | FALLBACK_CASES and args.tier != 'legacy'
    if case in FALLBACK_CASES:
        visible = next((line for line in telemetry if line.startswith('Modern visible frame:')), '')
        if not re.search(r'\bexec=0\b',visible) or not re.search(r'\bblocked=1\b',visible):
            failures.append('unsupported rendering contract did not retain complete classic ownership')
    if active_pbr and args.backend == 'gl':
        ownership = next((line for line in telemetry if line.startswith('modernLightingOwnership')), '')
        if (case.startswith(('production','environment-')) or '-pbr' in case) and not re.search(r'override=0 requested=1 materialContract=1\b',ownership):
            failures.append('production admission relied on a parity override or lacked its material contract')
        if (case.startswith('environment-') or case.startswith('lightgrid-pbr-normal-')) and not re.search(r'interaction\(pass=0 lights=0\b',ownership):
            failures.append('environment-only control still requested direct lighting')
        visible = next((line for line in telemetry if line.startswith('Modern visible frame:')), '')
        if not re.search(r'\bexec=1\b', visible) or not re.search(r'\bblocked=0\b', visible):
            failures.append('PBR did not own the visible frame')
        resources = next((line for line in telemetry if line.startswith('PBR material resources:')), '')
        if not re.search(r'\bfallback=0\b', resources): failures.append('authored material fell back')
        forward = next((line for line in telemetry if line.startswith('Modern forward+:')), '')
        if not re.search(r'\bfallback=0\b', forward): failures.append('PBR forward draw omitted')
        if case.startswith(('fog-pbr','blend-pbr')) and not case.endswith(('-clear','-off','-restored')) and not re.search(r'\bfog=[1-9]\d*\b',forward):
            failures.append('exact authored fog/blend geometry was not submitted')
        if case.startswith('lightgrid-pbr') and not case.endswith(('-clear','-off')):
            if not re.search(r'lightGrid\(pass=1 draws=[1-9]\d* consumable=[1-9]\d* blocked=0 unproven=0 unprovenPass=0 ready=1\)',ownership):
                failures.append('baked receivers did not acquire complete PBR ownership')
            if not re.search(r'\blightGrid=[1-9]\d*\b', forward):
                failures.append('no baked irradiance receiver was submitted')
        if case not in ('no-probes','direct') and CASES.get(case,{}).get('r_pbrIBL',BASE['r_pbrIBL'])!='0':
            probes = next((line for line in telemetry if line.startswith('Modern clustered specular probes:')), '')
            if not re.search(r'\buploaded=2\b', probes) or not re.search(r'\bframeReady=1\b', probes):
                failures.append('both authored probes were not consumed')
        if case in SHADOW_CASES:
            maps = [dict(re.findall(r'(\w+)=(-?\d+)',t)) for t in telemetry if t.startswith('Modern current shadow map:')]
            if len(maps)<2 or any(m.get('ready')!='1' for m in maps) or {m.get('point') for m in maps}!={'0','1'}:
                failures.append('complete current point and projected shadow maps were not consumed')
            if case == 'shadows' and not any(int(m.get('dynamic','0'))>0 for m in maps):
                failures.append('dynamic caster coverage was not exercised')
            if case == 'multi-shadows':
                points = {m['light']:m for m in maps if m.get('point')=='1' and m.get('ready')=='1'}
                if len(points)<3 or len({m.get('firstTile') for m in points.values()})!=len(points):
                    failures.append('three point lights did not consume distinct complete maps')
    if case in ('vk-direct-shadow-point','vk-direct-shadow-point-owned',
                'vk-direct-shadow-projected','vk-direct-shadow-projected-owned'):
        kind='projected' if '-projected' in case else 'point'
        maps=[line for line in text.splitlines() if line.startswith('SM pass light[') and f'type={kind} ' in line]
        # The stationary func_static may enter the static cache after the
        # stabilization frames. Require represented casters, not a particular
        # cache classification; the off/on/restore images prove occlusion.
        if not any(re.search(r'\bGLOBAL=(?:publish|reuse|scratch|alias)\b',line)
                   and re.search(r'\b(?:static|dynamic)=[1-9]\d*',line) for line in maps):
            failures.append(f'native {kind} material control lacks a complete map with represented casters')
        telemetry.extend(dict.fromkeys(maps))
    image = {}
    if shot.is_file():
        raw = shot.read_bytes()
        if len(raw) < 18:
            failures.append('truncated screenshot')
        else:
            width,height,bits = struct.unpack_from('<HHB',raw,12)
            stride = bits//8
            pixels = raw[18+raw[0]:]
            image = {'width':width,'height':height,'bits':bits}
            if (width,height) != (1280,800) or stride not in (3,4) or len(pixels) != width*height*stride or raw[2] != 2:
                failures.append('unexpected engine screenshot layout')
            else:
                image['greenOwnershipPixels'] = sum(pixels[i] == 0 and pixels[i+1] == 255 and pixels[i+2] == 0 for i in range(0,len(pixels),stride))
                if case == 'ownership' and active_pbr and image['greenOwnershipPixels'] < 4096:
                    failures.append('non-vacuous PBR ownership marker missing')
                if args.camera == 'overview':
                    manifest = json.loads((args.runtime_root/'pbr-lab.json').read_text())
                    camera = manifest['cameras']['overview']
                    samples = {}
                    coverage = {}
                    for station in manifest['stations']:
                        x,y,z = station['origin']
                        # Q4's 90-degree 4:3 FOV expands horizontally for widescreen.
                        focal = height * (2/3)
                        px = round(width/2 + (x-camera[0])*focal/(y-camera[1]))
                        py = round(height/2 - (z-camera[2])*focal/(y-camera[1]))
                        values = []
                        for dy in range(-3,4):
                            for dx in range(-3,4):
                                row = py+dy if raw[17]&32 else height-1-(py+dy)
                                offset = (row*width+px+dx)*stride
                                values.append(tuple(pixels[offset+c] for c in (2,1,0)))
                        samples[station['name']] = [round(sum(p[c] for p in values)/len(values),3) for c in range(3)]
                        if case == 'ownership' and station['name'] == 'cutout':
                            green = 0
                            for dy in range(-25,26):
                                for dx in range(-25,26):
                                    row = py+dy if raw[17]&32 else height-1-(py+dy)
                                    offset = (row*width+px+dx)*stride
                                    green += pixels[offset:offset+3] == bytes((0,255,0))
                            coverage['cutoutGreenFraction'] = green / (51*51)
                            if active_pbr and not 0.1 < coverage['cutoutGreenFraction'] < 0.9:
                                failures.append('cutout must retain both material and background pixels')
                    image['stationRGB'] = samples
                    image.update(coverage)
                    if case == 'ownership' and active_pbr:
                        for station, rgb in samples.items():
                            if station not in ('cutout','source_alpha') and (rgb[0] > 2 or rgb[1] < 253 or rgb[2] > 2):
                                failures.append(f'PBR ownership missing at {station}')
                        # Source alpha is checked against its own emission
                        # control instead, by compare_transparency_captures: a
                        # single capture cannot separate the surface's coverage
                        # from the background it composites over.
                    expected = {'albedo':128, 'metallic':102, 'roughness':128, 'ao':192}
                    if active_pbr and case in expected:
                        for station in ('data_scalar','data_packed','data_separate'):
                            if max(abs(v-expected[case]) for v in samples[station]) > 2:
                                failures.append(f'{case} channel incorrect at {station}: {samples[station]}')
                    if active_pbr and case == 'emissive' and min(samples['emissive'][1:]) < 250:
                        failures.append('authored HDR emissive missing')
                    if active_pbr and case == 'direct' and max(samples['ao_zero']) < 3:
                        failures.append('AO incorrectly extinguishes direct light, or direct lighting missing')
    hdr_line=next((line for line in text.splitlines() if line.startswith('OpenGL HDR:')), '')
    hdr_state={k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',hdr_line)}
    result = {'case':case, 'backend':args.backend, 'camera':args.camera, 'tier':args.tier, 'failures':failures, 'diagnostics':diagnostics, 'telemetry':telemetry, 'hdrState':hdr_state, 'image':image, 'screenshot':str(shot), 'sha256': {'screenshot':digest(shot) if shot.is_file() else None}}
    return result


def write_capture_commands(cfg: Path, commands: list[str]) -> dict[str,str]:
    # CmdSystem has a 64 KiB insertion buffer. Chain bounded scripts only
    # after the previous one has drained; preserve every command and wait.
    chunks=[[]]; size=0
    for command in commands:
        length=len((command+'\n').encode('utf-8'))
        if length>48*1024: raise ValueError('one capture command exceeds the script budget')
        if size+length>48*1024:
            chunks.append([]); size=0
        chunks[-1].append(command); size+=length
    paths=[cfg if i==0 else cfg.with_name(f'{cfg.stem}_{i:03d}{cfg.suffix}') for i in range(len(chunks))]
    for i,(path,lines) in enumerate(zip(paths,chunks)):
        if i+1<len(paths): lines=lines+[f'exec "{paths[i+1].name}"']
        path.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return {path.name:digest(path) for path in paths}


def capture_cases(args: argparse.Namespace, cases: list[str], save: Path) -> list[dict]:
    save.mkdir(parents=True, exist_ok=False)
    cfg = save/'baseoq4/pbr_lab_capture.cfg'
    cfg.parent.mkdir()
    manifest = json.loads((args.runtime_root/'pbr-lab.json').read_text())
    camera = manifest['cameras'][args.camera]
    cvars = {**BASE, 'r_renderApi': 'vulkan' if args.backend == 'vk' else 'gl', 'r_glTier': args.tier}
    if args.backend == 'vk':
        cvars['r_vkValidation'] = '1'
    if args.gl_debug:
        cvars.update({'r_glDebugContext':'1','r_glDebugOutput':'1','r_glDebugSynchronous':'1'})
    commands = ['wait 15', 'god', 'notarget', 'noclip', 'g_stopTime 1', 'setviewpos '+' '.join(map(str,camera))]
    scene_setups = set()
    for case in cases:
        # Change only the controls under test. Replaying unrelated startup
        # settings in gameplay can trigger expensive subsystem callbacks.
        settings = {key:BASE[key] for changes in CASES.values() for key in changes}
        settings.update(CASES[case])
        case_camera = manifest['cameras'][CASE_CAMERAS.get(case,args.camera)]
        commands += ['setviewpos '+' '.join(map(str,case_camera))]
        commands += [f'set {key} "{value}"' for key,value in settings.items()]
        if case.startswith('vk-direct-'):
            commands += ['g_stopTime 0']
            if 'vk-direct' not in scene_setups:
                commands += [f'script "${name}.Off()"' for name in ('key','blue_fill','warm_fill','lab_projector')]
                commands += ['spawn light name vk_direct_light origin "-120 -1000 540" angle 0 light_radius "1000 1000 1000" _color "1 1 1" noshadows 0']
            else:
                commands += ['script "$vk_direct_specimen.remove()"','wait 3']
            if not case.startswith('vk-direct-shadow-') and 'vk-direct-caster' in scene_setups:
                commands += ['script "$vk_direct_caster.remove()"','wait 3']
                scene_setups.remove('vk-direct-caster')
            if not case.startswith('vk-direct-shadow-projected') and 'vk-direct-projector' in scene_setups:
                commands += ['script "$vk_direct_light.remove()"','wait 3',
                    'spawn light name vk_direct_light origin "-120 -1000 540" angle 0 light_radius "1000 1000 1000" _color "1 1 1" noshadows 0']
                scene_setups.remove('vk-direct-projector')
            if case.startswith('vk-direct-shadow-') and 'vk-direct-caster' not in scene_setups:
                commands += [f'spawn func_static name vk_direct_caster model "{lab.MODEL}" shader "{lab.PREFIX}/classic_floor" origin "-60 -850 460" solid 0']
                scene_setups.add('vk-direct-caster')
            if case.startswith('vk-direct-shadow-projected') and 'vk-direct-projector' not in scene_setups:
                commands += ['script "$vk_direct_light.remove()"','wait 3',
                    f'spawn light name vk_direct_light origin "-120 -1000 540" angle 0 texture "lights/openq4/pbr_lab/projected" light_target "120 300 -160" light_right "250 -100 0" light_up "40 100 217.5" light_start "1.2 3 -1.6" light_end "360 900 -480" _color "1 1 1" noshadows 0']
                scene_setups.add('vk-direct-projector')
            # Isolate emission with zero, one or several direct lights. The
            # emission-only diagnostic must agree with the unlit ordinary draw.
            many=case=='vk-direct-emission-many-lights'
            emitter=case.startswith('vk-direct-emission-')
            for light in ('key','blue_fill','warm_fill','lab_projector'):
                commands += [f'script "${light}.{"On" if many else "Off"}()"']
            direct_on=not emitter or case in ('vk-direct-emission-one-light','vk-direct-emission-many-lights')
            commands += [f'script "$vk_direct_light.{"On" if direct_on else "Off"}()"']
            material=VK_DIRECT_MATERIALS[case.removeprefix('vk-direct-')]
            model=lab.CONSTANT_NORMAL_MODEL if case.startswith('vk-direct-aa-constant') else lab.MODEL
            commands += [f'spawn func_static name vk_direct_specimen model "{model}" shader "{lab.PREFIX}/{material}" origin "0 -700 380" solid 0',
                         'wait 30','g_stopTime 1']
            scene_setups.add('vk-direct')
        if case.startswith(('lightgrid-','production-fixed-grid')) and 'lightgrid' not in scene_setups:
            # Bake original map data into this run's isolated save path. The
            # native reference is also the bake owner, avoiding feedback from
            # the path under qualification. The engine reloads its output.
            commands += ['set r_pbrMaterials 0','set r_rendererReflectionProbes 0',
                         'bakeLightGrids force limit8 size32 blends1 samples16 grid 1024 1024 512',
                         'wait 30',f'set r_pbrMaterials {settings["r_pbrMaterials"]}',
                         f'set r_rendererReflectionProbes {settings["r_rendererReflectionProbes"]}']
            scene_setups.add('lightgrid')
        overlay_case=case.removeprefix('lightgrid-pbr-') if case.startswith(('lightgrid-pbr-fog','lightgrid-pbr-blend')) else case
        if case.startswith('lightgrid-pbr-normal-'):
            commands += ['g_stopTime 0']
            for light in ('key','blue_fill','warm_fill','lab_projector'):
                commands += [f'script "${light}.Off()"']
            if 'baked-normal' in scene_setups:
                commands += ['script "$pbr_baked_normal.remove()"','wait 3']
            commands += [f'spawn func_static name pbr_baked_normal model "{lab.MODEL}" shader "{lab.PREFIX}/baked_normal_{case.rsplit("-",1)[-1]}" origin "0 -700 380" solid 0', 'wait 30','g_stopTime 1']
            scene_setups.add('baked-normal')
        if case.startswith(('fog-pbr','blend-pbr','lightgrid-pbr-fog','lightgrid-pbr-blend')):
            commands += ['g_stopTime 0',f'script "$source_alpha.{"hide" if "-hidden" in case else "show"}()"','wait 3','g_stopTime 1']
        if (overlay_case.startswith(('fog-','blend-')) or overlay_case in ('fog','blend')) and not case.endswith(('-clear','-restored')):
            kind=overlay_case.split('-')[0]
            commands += ['g_stopTime 0']
            if kind not in scene_setups:
                commands += [f'spawn light name pbr_overlay origin "0 0 400" light_radius "1400 1400 1000" texture "lights/openq4/pbr_lab/{kind}" noshadows 1']
                scene_setups.add(kind)
            commands += [f'script "$pbr_overlay.{"remove" if case.endswith("-off") else "On"}()"','wait 30','g_stopTime 1']
            if case.endswith('-off'): scene_setups.remove(kind)
        elif overlay_case.startswith(('fog-','blend-')):
            kind=overlay_case.split('-')[0]
            if kind in scene_setups:
                commands += ['g_stopTime 0','script "$pbr_overlay.remove()"','wait 30','g_stopTime 1']
                scene_setups.remove(kind)
        if case.startswith(('production-points','production-projector','environment-')):
            commands += ['g_stopTime 0']
            for light in ('key','blue_fill','warm_fill','lab_projector'):
                enabled = ('points' in case and light!='lab_projector') or ('projector' in case and light=='lab_projector')
                commands += [f'script "${light}.{"On" if enabled else "Off"}()"']
            commands += ['wait 30','g_stopTime 1']
        if case.startswith(('production-rejected','production-curved','production-fixed')):
            removed='pbr_classic' if 'classic' in scene_setups else 'metal_3'
            material='classic_floor' if 'curved' in case else 'classic_specular'
            if case.startswith('production-fixed-bump'): material='classic_bump'
            if case.startswith('production-fixed-diffuse'): material='classic_diffuse'
            if case.startswith('production-fixed-colored'): material='classic_colored'
            commands += ['g_stopTime 0',f'script "${removed}.remove()"','wait 3',
                         f'spawn func_static name pbr_classic model "{lab.MODEL}" shader "{lab.PREFIX}/{material}" origin "100 160 480" solid 0',
                         'wait 30','g_stopTime 1']
            scene_setups.add('classic')
        if case.startswith('sampler-'):
            commands += ['g_stopTime 0']
            if 'sampler' in scene_setups:
                commands += ['script "$pbr_sampling.remove()"','wait 3']
            commands += [f'spawn func_static name pbr_sampling model "{lab.SAMPLING_MODEL}" shader "{lab.PREFIX}/{case.replace("-","_")}" origin "0 -700 380" solid 0', 'wait 30', 'g_stopTime 1']
            scene_setups.add('sampler')
        if case.startswith('hdr-extreme-') and 'extreme-emissive' not in scene_setups:
            commands += ['g_stopTime 0','script "$emissive.remove()"','wait 3',
                         f'spawn func_static name emissive model "{lab.MODEL}" shader "{lab.PREFIX}/extreme_emissive" origin "300 160 300" solid 0',
                         'wait 30','g_stopTime 1']
            scene_setups.add('extreme-emissive')
        if case.startswith('skin-'):
            commands += ['g_stopTime 0']
            if 'skin' not in scene_setups:
                commands += [f'spawn func_animate name pbr_skinned model "{lab.SKINNING_MODEL}" anim idle origin "0 -700 380" angle 0 solid 0', 'wait 30']
                scene_setups.add('skin')
            angle='45 0 0' if case.endswith(('-bent','-normal','-ownership')) else '0 0 0'
            commands += [f'''script "$pbr_skinned.setJointAngle(1,1,'{angle}')"''','wait 30','g_stopTime 1']
        setup = 'multi' if case.startswith('multi-') else 'local-global' if case.startswith('local-global') else None
        if setup is None or setup not in scene_setups:
            commands += CASE_COMMANDS.get(case,[])
            if setup: scene_setups.add(setup)
        commands += ['wait 20', f'echo "PBRLAB_{case}_BEGIN"']
        if case.startswith('vk-direct-shadow-'):
            commands += ['wait 2']
        commands += ['viewpos', 'gfxInfo', 'rendererMaterialResourceTableDump', f'screenshot "screenshots/{case}.tga"']
        if case=='vk-direct-emission-extreme':
            commands += ['rendererVulkanHDRInfo']
        if args.linear and args.backend=='gl' and case not in LEGACY_CASES | FALLBACK_CASES:
            commands += [f'screenshot linear "screenshots/{case}.pfm"']
        commands += [f'echo "PBRLAB_{case}_END"']
    commands += ['wait 5', 'quit']
    cfg_parts=write_capture_commands(cfg,commands)
    cvars.update(CASES[cases[0]])
    cvars.update({'fs_game': 'baseoq4', 'fs_basepath': str(args.basepath), 'fs_savepath': str(save), 'fs_devpath': str(save), 'g_autoExecAfterMapLoad': 'pbr_lab_capture.cfg', 'g_autoExecAfterMapLoadDelayMs': '500'})
    executable = next(args.runtime_root.glob('openQ4-client_*.exe')) if os.name == 'nt' else next(args.runtime_root.glob('openQ4-client_*'))
    command = [str(executable), *startup_args(cvars,save), '+map', lab.MAP]
    (save/'command.json').write_text(json.dumps(command,indent=2),encoding='utf-8')
    code, elapsed = run(command,args.runtime_root,save/'process.log',args.timeout)
    log = save/'baseoq4/logs/openq4.log'
    text = log.read_text(errors='replace') if log.exists() else ''
    results = []
    for case in cases:
        start,end = f'PBRLAB_{case}_BEGIN', f'PBRLAB_{case}_END'
        section = text.split(start,1)[-1].split(end,1)[0] if start in text and end in text else ''
        inspection_args = argparse.Namespace(**{**vars(args),'camera':CASE_CAMERAS.get(case,args.camera)})
        result = inspect_capture(inspection_args,case,section,save/f'baseoq4/screenshots/{case}.tga')
        if args.backend == 'vk':
            result['failures'].extend(vulkan_backend_failures(text))
        if case.startswith('production-fixed-msaa') and not re.search(r'Forward render target MSAA: requested 4, effective 4\b',text):
            result['failures'].append('native fallback did not allocate a four-sample render target')
        if case=='production-fixed-resize':
            intermediate=save/'baseoq4/screenshots/production-fixed-resize-small.tga'
            small_section=text.split('PBRLAB_RESIZE_SMALL_BEGIN',1)[-1].split('PBRLAB_RESIZE_SMALL_END',1)[0]
            result['resizeScreenshot']=str(intermediate)
            result['sha256']['resizeScreenshot']=digest(intermediate) if intermediate.is_file() else None
            raw=intermediate.read_bytes() if intermediate.is_file() else b''
            if len(raw)<18 or struct.unpack_from('<HH',raw,12)!=(960,600):
                result['failures'].append('intermediate resize did not produce a 960x600 engine image')
            if not re.search(r'Modern classic lighting: ready=1 executed=1 primitives=[1-9]\d* extent=960x600\b',small_section):
                result['failures'].append('classic lighting did not allocate and submit at the intermediate extent')
        if args.linear and args.backend=='gl' and case not in LEGACY_CASES | FALLBACK_CASES:
            linear = save/f'baseoq4/screenshots/{case}.pfm'
            is_linear=CASES[case].get('r_hdrToneMap',BASE['r_hdrToneMap'])=='1'
            result['linearCaptureStatus']='linear HDR' if is_linear else 'rejected encoded preview'
            if not is_linear:
                if linear.exists() or 'screenshot linear: no completed modern HDR scene' not in section:
                    result['failures'].append('linear capture must reject the encoded preview')
            elif not linear.is_file(): result['failures'].append('linear HDR screenshot missing')
            else:
                result['linearScreenshot'] = str(linear)
                result['sha256']['linearScreenshot'] = digest(linear)
                try: result['linearImage']=inspect_linear_capture(linear,manifest,inspection_args.camera,case)
                except (ValueError,IndexError) as error: result['failures'].append(str(error))
        if code: result['failures'].append(f'exit={code}')
        if lab.MAP not in text: result['failures'].append('map identity not observed')
        if not section: result['failures'].append('capture markers missing')
        # Startup/map diagnostics apply to every capture, even outside case markers.
        diagnostics = diagnostic_lines(text)
        if diagnostics and not result['diagnostics']:
            result['diagnostics'] = diagnostics
            result['failures'].append('engine diagnostics require review')
        result.update({'exitCode':code,'elapsed':elapsed,'log':str(log)})
        result['sha256'].update({'cfg':digest(cfg),'cfgParts':cfg_parts,'log':digest(log) if log.is_file() else None})
        (save/f'{case}.json').write_text(json.dumps(result,indent=2)+'\n')
        print(f'  {case}: failures={result["failures"]}',flush=True)
        results.append(result)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--basepath',type=Path,required=True)
    parser.add_argument('--prepare',action='store_true')
    parser.add_argument('--update-fixture',action='store_true',help='regenerate and recompile only original laboratory data in an existing test runtime')
    parser.add_argument('--batch',action='store_true',help='capture sequential controls in one process; defaults use fresh processes')
    parser.add_argument('--linear',action='store_true',help='also retain a pre-tonemap HDR float screenshot from the engine')
    parser.add_argument('--gl-debug',action='store_true',help='enable synchronous driver diagnostics for correctness qualification')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--cases',default='lit,ownership,no-probes,legacy,master-off')
    selection.add_argument('--suite',choices=('vulkan-direct',),help='run every control in the named suite')
    parser.add_argument('--camera',default='overview',help='camera name in pbr-lab.json; station-NAME centres a station')
    parser.add_argument('--backend',choices=('gl','vk'),default='gl')
    parser.add_argument('--tier',default='gl45')
    parser.add_argument('--timeout',type=int,default=180)
    parser.add_argument('--samples',type=int,choices=(0,4),help='override sample count for vk-direct controls')
    args = parser.parse_args()
    args.runtime_root = fixture.validate_runtime_root(args.runtime_root)
    args.output_dir = fixture.validate_runtime_root(args.output_dir)
    args.basepath = args.basepath.resolve()
    if not (args.basepath/'q4base').is_dir():
        parser.error('--basepath must contain q4base; do not rely on engine installation autodetection')
    args.output_dir.mkdir(parents=True,exist_ok=True)
    cases = args.cases.split(',') if args.cases else []
    if args.suite=='vulkan-direct': cases=['vk-direct-'+name for name in VK_DIRECT_MATERIALS]
    if not cases and not (args.prepare or args.update_fixture):
        parser.error('select rendering controls, or explicitly prepare/update the fixture')
    if args.prepare and args.update_fixture: parser.error('choose --prepare or --update-fixture')
    if any(case not in CASES for case in cases): parser.error('unknown capture case')
    if args.backend!='vk' and any(case.startswith('vk-direct-') for case in cases):
        parser.error('vk-direct controls require the native Vulkan backend')
    if args.samples is not None:
        if not cases or args.backend!='vk' or not all(case.startswith('vk-direct-') for case in cases):
            parser.error('--samples requires only native vk-direct controls')
        for case in cases:
            CASES[case]={**CASES[case],'r_multiSamples':str(args.samples)}
    if args.backend=='gl' and any(c in ('msaa-partial-restart','msaa-full-restart') or '-pbr' in c for c in cases):
        args.linear=True
    if args.batch and len({CASES[c].get('r_multiSamples',BASE['r_multiSamples']) for c in cases})>1:
        parser.error('MSAA sample count must stay fixed for a batch; use separate processes for other counts')
    for prefix in ('multi-','local-global','sampler-','skin-','fog-','blend-','lightgrid-','vk-direct-'):
        if args.batch and any(c.startswith(prefix) for c in cases) and not all(c.startswith(prefix) for c in cases):
            parser.error(f'{prefix} cases require a separate scene from other controls')
    if args.prepare: prepare(args.runtime_root,args.basepath,args.output_dir,args.timeout)
    if args.update_fixture:
        existing=json.loads((args.runtime_root/'pbr-lab.json').read_text())
        if existing.get('id')!='openq4-pbr-laboratory': raise ValueError('not an existing PBR laboratory runtime')
        lab.generate(args.runtime_root,force=True)
        compile_fixture(args.runtime_root,args.basepath,args.output_dir,args.timeout)
    manifest = json.loads((args.runtime_root/'pbr-lab.json').read_text())
    for name, expected in manifest['sha256'].items():
        target = fixture.validate_contained_target(args.runtime_root,args.runtime_root/name)
        if not target.is_file() or digest(target) != expected:
            raise RuntimeError(f'Fixture content differs from its manifest: {target}')
    map_source = args.runtime_root/'baseoq4'/f'{lab.MAP}.map'
    compiled_map = map_source.with_suffix('.proc')
    if not compiled_map.is_file() or compiled_map.stat().st_mtime_ns < map_source.stat().st_mtime_ns:
        raise RuntimeError('PBR map must be compiled after its source was generated')
    binaries = sorted(set([*args.runtime_root.glob('openQ4-client*'),*args.runtime_root.glob('renderer-*'),
                           *args.runtime_root.glob('*.dll'),*args.runtime_root.glob('*.so*'),
                           *args.runtime_root.glob('*.dylib'),*args.runtime_root.glob('baseoq4/game-*')]))
    provenance = {str(p.relative_to(args.runtime_root)):digest(p) for p in binaries if p.is_file() and p.suffix not in ('.pdb','.lib','.exp')}
    report = {'schema':2, 'basepath':str(args.basepath), 'fixture':manifest, 'runtimeSHA256':provenance, 'compiledMapSHA256':digest(compiled_map), 'results':[], 'complete':False, 'harnessSHA256':digest(Path(__file__))}
    sources=args.output_dir/'harness'
    sources.mkdir(exist_ok=True)
    report['harnessSources']={}
    for source in (Path(__file__),Path(lab.__file__),Path(fixture.__file__)):
        target=sources/source.name
        shutil.copy2(source,target)
        report['harnessSources'][source.name]=digest(target)
    if args.samples is not None:
        report['requestedSamples']=args.samples
    (args.output_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    results = []
    for group in ([cases] if args.batch and cases else [[case] for case in cases]):
        print(f'PBR laboratory: {args.backend}/{args.tier}/{args.camera}/{",".join(group)}',flush=True)
        results += capture_cases(args,group,args.output_dir/('batch' if args.batch else group[0]))
    compare_normal_captures(results)
    compare_vulkan_direct_captures(results)
    compare_sampler_captures(results)
    compare_skinning_captures(results)
    compare_material_captures(results)
    compare_transparency_captures(results)
    compare_post_captures(results)
    compare_msaa_captures(results)
    compare_shadow_captures(results)
    after={name:digest(args.runtime_root/name) if (args.runtime_root/name).is_file() else None for name in provenance}
    unchanged=(after==provenance and compiled_map.is_file() and digest(compiled_map)==report['compiledMapSHA256']
               and all((args.runtime_root/name).is_file() and digest(args.runtime_root/name)==expected
                       for name,expected in manifest['sha256'].items()))
    if not unchanged:
        for row in results: row['failures'].append('runtime or laboratory inputs changed during capture')
    for result in results:
        directory = args.output_dir/('batch' if args.batch else result['case'])
        (directory/f'{result["case"]}.json').write_text(json.dumps(result,indent=2)+'\n')
        if result['failures']:
            print(f'  final {result["case"]}: {result["failures"]}',flush=True)
    if results:
        print(f'PBR laboratory final: {sum(not r["failures"] for r in results)}/{len(results)} passed',flush=True)
    else:
        print('PBR laboratory fixture prepared; no rendering controls were selected',flush=True)
    report.update(results=results,complete=True,runtimeSHA256After=after,runtimeUnchanged=unchanged)
    (args.output_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(any(result['failures'] for result in results))


if __name__ == '__main__':
    raise SystemExit(main())

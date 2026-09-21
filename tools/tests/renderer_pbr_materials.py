#!/usr/bin/env python3
"""Compatibility contracts for opt-in PBR material metadata and resources."""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
import renderer_gameplay_benchmark as gameplay


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source is missing: {path}")
    return path.read_text(encoding="utf-8")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(source: str, token: str, context: str) -> None:
    if token in source:
        raise AssertionError(f"unexpected {token!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature!r}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body for {signature!r}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated body for {signature!r}")


def section(source: str, start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    end = source.find(end_marker, start + len(start_marker))
    if start < 0 or end < 0:
        raise AssertionError(f"missing source section {start_marker!r}..{end_marker!r}")
    return source[start:end]


def test_shared_material_abi_and_opt_in_defaults() -> None:
    engine_header = read(ROOT / "src/renderer/Material.h")
    game_header = read(GAME_ROOT / "src/renderer/Material.h")
    if engine_header != game_header:
        raise AssertionError("engine and companion Material.h must remain byte-identical")

    require(engine_header, "legacyFallbackMissing", "explicit missing-fallback state")

    init = read(ROOT / "src/renderer/RenderSystem_init.cpp")
    for name, default in (
        ("r_pbrMaterials", "0"),
        ("r_pbrGeneratedLegacyFallback", "1"),
        ("r_pbrDebug", "0"),
        ("r_pbrIBL", "1"),
        ("r_pbrIBLIntensity", "1"),
        ("r_pbrInferFromLegacyMaterials", "0"),
    ):
        require(init, f'idCVar {name}( "{name}", "{default}"', "PBR cvar defaults")
    require(init, "PBR materials: parser=1 modernLighting=1", "honest gfxInfo capability")


def test_parser_is_namespaced_and_fail_closed() -> None:
    material = read(ROOT / "src/renderer/Material.cpp")
    parse_material = section(material, "void idMaterial::ParseMaterial( idLexer &src )", "void idMaterial::SetGui(")
    require(parse_material, '!token.Icmp( "pbr" )', "top-level PBR namespace")
    require(parse_material, '!token.Icmp( "physicallyBased" )', "PBR namespace alias")
    reject(parse_material, "r_pbrInferFromLegacyMaterials", "classic parser path")
    reject(parse_material, "pbrInfo.metallicRegister = GetExpressionConstant", "stock register allocation")

    parse_pbr = section(material, "bool idMaterial::ParsePBRBlock(", "void idMaterial::AddPBRLegacyFallbackStages(")
    for token in (
        "workflow",
        "albedoMap",
        "normalMap",
        "normalFormat",
        "ormMap",
        "metallicMap",
        "roughnessMap",
        "aoMap",
        "emissiveMap",
        "legacyBumpMap",
        "legacyDiffuseMap",
        "legacySpecularMap",
        "legacyEmissiveMap",
        "autoLegacyFallback",
    ):
        require(parse_pbr, f'!token.Icmp( "{token}" )', "PBR block parser")
    require(parse_pbr, "unknown PBR parameter", "unknown-token rejection")
    require(parse_pbr, "requires normalFormat", "normal encoding contract")
    require(parse_pbr, "cannot combine ormMap", "packed/separate exclusivity")
    require(parse_pbr, "pbrInfo.metallicRegister = GetExpressionConstant", "opt-in PBR register defaults")
    require(parse_pbr, '( value != "0" && value != "1" )', "literal auto-fallback boolean grammar")
    reject(parse_pbr, "value.GetIntValue()", "fractional auto-fallback coercion")
    require(material, "autoLegacyFallback 0.5", "fractional auto-fallback negative self-test")
    require(material, "albedoMap reflectionRenderMap", "dynamic image negative self-test")
    require(material, "albedoMap glslProgram", "shader-token image negative self-test")
    require(material, "albedoMap add( _white, reflectionRenderMap )", "nested dynamic image negative self-test")
    require(material, "albedoMap add( _white, glslProgram )", "nested shader-token image negative self-test")
    require(material, "albedoMap add( _white, _currentRender )", "nested scene-capture negative self-test")
    require(material, "albedoMap add( _white, _reflectionRender )", "nested mutable-target negative self-test")
    require(material, "albedoMap add( _white, alphaTest )", "nested stage-state negative self-test")

    unsupported_tokens = function_body(material, "static bool R_IsUnsupportedPBRImageProgramToken(")
    parse_image = section(material, "bool idMaterial::ParsePBRImage(", "bool idMaterial::ParsePBRBlock(")
    require(unsupported_tokens, "R_IsMutableRenderImageName( token.c_str() )", "central mutable-render-image rejection")
    image_manager = read(ROOT / "src/renderer/ImageManager.cpp")
    for mutable_name in (
        "_reflectionRender",
        "_refractionRender",
        "_currentRender",
        "_forwardRenderResolved",
        "_postProcessAlbedo",
        "_shadowMap",
        "_pointShadowMap",
        "_hdrScene",
        "_ssao",
        "_bloom",
    ):
        require(image_manager, f'"{mutable_name}"', "mutable render-image classifier")
    require(image_manager, "image->scratchImage = true;", "dynamic scratch-image classification")
    for rejected in (
        "videoMap",
        "soundMap",
        "mirrorRenderMap",
        "remoteRenderMap",
        "reflectionRenderMap",
        "refractionRenderMap",
        "xrayRenderMap",
        "cameraCubeMap",
        "cubeMap",
        "program",
        "glslProgram",
        "blend",
        "map",
        "screen",
        "screen2",
        "glassWarp",
        "texGen",
        "alphaTest",
        "alphaFunc",
        "translate",
        "centerScale",
        "shear",
        "rotate",
        "vertexColor",
        "color",
        "maskColor",
        "privatePolygonOffset",
    ):
        require(unsupported_tokens, f'!token.Icmp( "{rejected}" )', "dynamic PBR image rejection")
    require(parse_image, "R_IsUnsupportedPBRImageProgramToken( token )", "direct PBR image-token rejection")
    require(parse_image, "R_IsUnsupportedPBRImageProgramToken( imageProgramToken )", "nested PBR image-token rejection")
    require(parse_image, "R_IsMutableRenderImage( target.image )", "loaded mutable-image rejection")
    require(parse_image, "while ( imageProgram.ReadToken", "complete PBR image-program validation")
    for token in (
        "target.filter",
        "target.repeat",
        "target.allowPicmip",
        "target.noMips",
        "target.highQuality",
        "target.forceHighQuality",
    ):
        require(parse_image, token, "retained PBR image options")

    fallback = section(
        material,
        "void idMaterial::AddPBRLegacyFallbackStages( const textureRepeat_t trpDefault )",
        "void idMaterial::ParseDeform( idLexer &src )",
    )
    require(fallback, "const bool hasUsableClassicInteraction = hasBump && hasDiffuse;", "complete classic fallback authority")
    require(fallback, "!hasUsableClassicInteraction", "approximation requires incomplete classic interaction")
    require(fallback, "const bool allowApproximate", "approximation gate")
    require(fallback, "pbrInfo.normalFormat == PBR_NORMAL_QUAKE4_AGB", "classic normal encoding gate")
    for token in (
        'buffer.Append( "nearest\\n" )',
        'buffer.Append( "noclamp\\n" )',
        'buffer.Append( "nopicmip\\n" )',
        'buffer.Append( "nomips\\n" )',
        'buffer.Append( "forceHighQuality\\n" )',
    ):
        require(fallback, token, "classic fallback image-option replay")
    require(material, "diffuseStage->texture.image == expectedDiffuse", "runtime fallback image identity assertion")
    require(material, "pbrInfo.legacyFallbackMissing = !( hasBump && hasDiffuse );", "missing fallback classification")
    require(material, "has no usable classic bump+diffuse fallback", "missing fallback diagnostic")
    require(material, "_pbr_selftest_missing_fallback", "missing fallback runtime self-test")
    require(material, "_pbr_selftest_tangent_normal_fallback", "tangent normal fallback runtime self-test")
    require(material, "_pbr_selftest_quake4_normal_fallback", "Quake 4 normal fallback runtime self-test")
    require(material, "bumpStage->texture.image == globalImages->flatNormalMap", "tangent normal neutral fallback assertion")
    require(material, "bumpStage->texture.image == info.normal.image", "Quake 4 normal reuse assertion")
    require(material, "const bool oldGeneratedFallback = r_pbrGeneratedLegacyFallback.GetBool();", "fallback self-test cvar save")
    require(material, "r_pbrGeneratedLegacyFallback.SetBool( oldGeneratedFallback );", "fallback self-test cvar restore")


def test_texture_usage_and_lifecycle_contracts() -> None:
    image_header = read(ROOT / "src/renderer/Image.h")
    order = [image_header.find(name) for name in ("TD_HIGH_QUALITY", "TD_PBR_COLOR", "TD_MATERIAL_DATA")]
    if any(position < 0 for position in order) or order != sorted(order):
        raise AssertionError("PBR image usages must append after historical cache identities")

    image_load = read(ROOT / "src/renderer/Image_load.cpp")
    require(image_load, "case TD_PBR_COLOR:", "PBR color derivation")
    require(image_load, "opts.format = FMT_SRGBA8;", "hardware sRGB filtering")
    require(read(ROOT / 'src/renderer/OpenGL/gl_Image.cpp'), 'case FMT_SRGBA8:', 'GL sRGB storage')
    require(read(ROOT / 'src/renderer/Vulkan/vk_Image.cpp'), 'VK_FORMAT_R8G8B8A8_SRGB', 'Vulkan sRGB storage')
    require(read(ROOT / 'src/imagetools/BinaryImage.cpp'), 'R_MipMapWithSRGB', 'sRGB RGB and linear-alpha mip chain')
    require(image_load, '_name += "s1";', 'PBR-only image cache revision')
    require(image_load, "opts.gammaMips = true;", "gamma-correct PBR color mips")
    require(image_load, "case TD_MATERIAL_DATA:", "linear material-data derivation")
    gamma_downsize = function_body(image_load, "static bool R_ImageUsageUsesGammaMips(")
    require(gamma_downsize, "TD_PBR_COLOR", "gamma-correct PBR color prefiltering")
    declared_usage = function_body(image_load, "static void R_LoadImageProgramForDeclaredUsage(")
    require(declared_usage, "declaredUsage != TD_PBR_COLOR", "PBR color image-program usage preservation")
    require(declared_usage, "declaredUsage != TD_MATERIAL_DATA", "PBR data image-program usage preservation")
    load_image = function_body(image_load, "void idImage::ActuallyLoadImage(")
    if load_image.count("R_LoadImageProgramForDeclaredUsage(") != 5:
        raise AssertionError("every image-program decode in ActuallyLoadImage must preserve declared PBR usage")
    if "R_LoadImageProgram(" in load_image:
        raise AssertionError("ActuallyLoadImage must not bypass declared PBR usage preservation")

    require(image_header, "textureUsage_t GetUsage() const", "PBR image usage runtime introspection")
    material = read(ROOT / "src/renderer/Material.cpp")
    require(material, "albedoMap heightmap( _white, 1 )", "PBR color usage mutation regression self-test")
    require(material, "ormMap smoothnormals( _flat )", "PBR data usage mutation regression self-test")
    require(material, "sameAlbedo == info.albedo.image", "PBR color cache identity assertion")
    require(material, "sameORM == info.orm.image", "PBR data cache identity assertion")
    require(material, "registerMatches( info.metallicRegister, 0.25f )", "PBR metallic register value assertion")
    require(material, "registerMatches( info.emissiveColorRegisters[2], 0.3f )", "PBR emissive register value assertion")

    image_manager = read(ROOT / "src/renderer/ImageManager.cpp")
    namespace_usage = function_body(image_manager, "static textureUsage_t R_ImageUsageForName(")
    require(namespace_usage, "requestedUsage != TD_DEFAULT", "explicit image-usage namespace preservation")
    require(namespace_usage, "return requestedUsage;", "explicit PBR image usage return")
    if image_manager.count("usage = R_ImageUsageForName( _name, usage );") != 3:
        raise AssertionError("all immediate, lookup, and deferred image paths must share namespace usage policy")

    for signature in (
        "void idMaterial::AddReference()",
        "void idMaterial::ResolveUse()",
        "void idMaterial::ReloadImages( bool force ) const",
    ):
        body = function_body(material, signature)
        require(body, "pbrInfo.albedo", f"PBR lifecycle in {signature}")
        require(body, "pbrInfo.legacyEmissive", f"complete PBR lifecycle in {signature}")
    reload_images = function_body(material, "void idMaterial::ReloadImages( bool force ) const")
    require(reload_images, "specularProbeInfo.cubeImage->Reload( force );", "specular-probe reload lifecycle")
    add_reference = function_body(material, "void idMaterial::AddReference()")
    require(add_reference, "specularProbeInfo.cubeImage->AddReference();", "specular-probe reference lifecycle")
    resolve_use = function_body(material, "void idMaterial::ResolveUse()")
    require(resolve_use, "specularProbeInfo.cubeImage->AddUseCount( useCount );", "specular-probe use-count lifecycle")
    print_body = function_body(material, "void idMaterial::Print() const")
    require(print_body, "Specular probe: cubeMap=%s", "specular-probe diagnostic lifecycle")
    free_data = function_body(material, "void idMaterial::FreeData()")
    require(free_data, "memset( &pbrInfo, 0, sizeof( pbrInfo ) );", "purged PBR metadata reset")
    require(free_data, "pbrInfo.normalFormat = PBR_NORMAL_UNSPECIFIED;", "purged normal-format reset")


def test_scene_packet_and_resource_table_drive_the_guarded_visible_path() -> None:
    packets_h = read(ROOT / "src/renderer/ScenePackets.h")
    packets_cpp = read(ROOT / "src/renderer/ScenePackets.cpp")
    table_h = read(ROOT / "src/renderer/MaterialResourceTable.h")
    table_cpp = read(ROOT / "src/renderer/MaterialResourceTable.cpp")

    for token in (
        "hasPBR",
        "pbrAlbedoImage",
        "pbrNormalImage",
        "pbrORMImage",
        "pbrWorkflow",
        "pbrNormalFormat",
        "pbrUsesApproximateLegacyFallback",
        "pbrLegacyFallbackMissing",
    ):
        require(packets_h, token, "scene-packet PBR record")
        require(packets_cpp, token, "scene-packet PBR capture")

    ambient_filter = function_body(
        packets_cpp,
        "static bool R_ScenePackets_DrawSurfAmbientEligible(",
    )
    require(
        ambient_filter,
        "!material->HasAmbient() && !material->HasPBR()",
        "PBR ambient-packet surface owner admission",
    )

    for semantic in (
        "ALBEDO",
        "NORMAL",
        "ORM",
        "METALLIC",
        "ROUGHNESS",
        "AO",
        "EMISSIVE_PBR",
    ):
        require(table_h, f"MATERIAL_RESOURCE_TEXTURE_{semantic}", "PBR resource semantics")
    require(table_h, "pbrResourceReady", "resource readiness split")
    require(table_h, "pbrModernReady", "visible readiness split")
    for token in (
        "classicRecords",
        "pbrExplicitGeneratedFallbackRecords",
        "pbrMissingLegacyFallbackRecords",
        "pbrMissingAlbedoMapRecords",
        "pbrMissingNormalMapRecords",
        "pbrMissingORMMapRecords",
    ):
        require(table_h, token, "PBR fallback/map observability")
        require(table_cpp, token, "PBR fallback/map metrics")

    texture_table = function_body(
        table_cpp,
        "static void R_MaterialResourceTable_BuildTextureArrayTable(",
    )
    require(texture_table, "if ( record.hasPBR )", "PBR isolation from classic texture table")
    require(texture_table, "binding.textureArrayLayer = -1;", "PBR texture-table layer reset")
    finalize = section(
        table_cpp,
        "static void R_MaterialResourceTable_FinalizePBRContract(",
        "static void R_MaterialResourceTable_FinalizeShadowContract(",
    )
    require(finalize, "record.pbrModernReady = true;", "guarded visible-PBR readiness")
    reject(finalize, "MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_TEXTURE_LAYOUT", "obsolete separate-map fallback gate")
    require(finalize, "albedo, normal, packed ORM or", "separate-map direct sampler contract")
    pbr_binding_ready = function_body(
        table_cpp,
        "static bool R_MaterialResourceTable_PBRBindingReady(",
    )
    require(pbr_binding_ready, "!R_IsMutableRenderImage( binding.image )", "mutable PBR resource rejection")

    classic_gate = function_body(
        table_cpp,
        "bool R_MaterialResourceTable_ClassicModernPathEligible(",
    )
    require(classic_gate, "return !record.hasPBR;", "classic-modern PBR exclusion")
    pbr_gate = function_body(
        table_cpp,
        "bool R_MaterialResourceTable_PBRModernPathEligible(",
    )
    require(pbr_gate, "r_rendererModernQuality.GetBool()", "master modern-quality PBR rollback")
    require(pbr_gate, "record.pbrModernReady", "separate PBR visible gate")
    transparent_gate = function_body(
        table_cpp,
        "bool R_MaterialResourceTable_PBRTransparentPathEligible(",
    )
    require(transparent_gate, "R_MaterialResourceTable_PBRModernPathEligible", "transparent PBR visible gate")
    require(transparent_gate, "R_MaterialResourceTable_HasPBRSourceAlphaBlendContract", "transparent PBR blend gate")
    source_alpha_contract = function_body(
        table_cpp,
        "static bool R_MaterialResourceTable_HasPBRSourceAlphaBlendContract(",
    )
    for token in (
        "MATERIAL_RESOURCE_BLEND_BLEND",
        "record.blendStageCount != 1",
        "record.additiveStageCount != 0",
        "record.filterStageCount != 0",
        "record.alphaTest",
        "MATERIAL_RESOURCE_TEXTURE_ALBEDO",
        "MATERIAL_RESOURCE_TEXTURE_EMISSIVE",
        "R_MaterialResourceTable_BindingsSampleSameImage",
        "R_MaterialResourceTable_BindingHasIdentityColor",
        "TG_EXPLICIT",
        "SVC_IGNORE",
    ):
        require(source_alpha_contract, token, "source-alpha PBR contract")
    require(table_cpp, "sourceAlphaIntent", "transparent PBR fail-closed intent gate")
    require(table_cpp, "MATERIAL_RESOURCE_PBR_FALLBACK_CLASSIC_FEATURE", "transparent PBR fail-closed fallback")
    stage_color = function_body(
        table_cpp,
        "static void R_MaterialResourceTable_ValidateStageColorContract(",
    )
    ambient_overlay = function_body(
        table_cpp,
        "static void R_MaterialResourceTable_ValidateAmbientOverlayContract(",
    )
    for body, context in ((stage_color, "stage-color contract"), (ambient_overlay, "ambient-overlay contract")):
        require(body, "R_MaterialResourceTable_HasPBRSourceAlphaBlendContract", f"source-alpha PBR {context}")
    require(table_cpp, "!R_MaterialResourceTable_ClassicModernPathEligible( *record )", "native PBR ownership self-test")
    require(table_cpp, "pbrBindingsExcludedFromClassicTable", "native PBR texture-table isolation self-test")
    require(table_cpp, "stats.textureArrayTableDescriptors == 0", "PBR classic-table descriptor isolation")
    require(table_cpp, "stats.classicRecords == 0", "PBR records excluded from classic record count")
    require(table_cpp, "stats.classicRecords != expectedRecords", "classic packet record count assertion")
    require(table_cpp, 'RendererMaterialResourceTable PBR contract self-test passed', "native PBR table pass marker")
    require(table_cpp, "savedMaxClassicTextureUnits", "uninitialized-backend PBR table unit budget scope")
    require(table_cpp, "rg_materialResourceTable.maxClassicTextureUnits = savedMaxClassicTextureUnits;", "PBR table unit budget restoration")
    for token in (
        "separateSource",
        "scalarSource",
        "explicitSource",
        "unsupportedSource",
        "missingFallbackSource",
        "missingAlbedoSource",
        "mutableImageSource",
        "redundantExplicitSource",
    ):
        require(table_cpp, token, "PBR material-table layout self-tests")
    require(table_cpp, "sourceAlphaContractAccepted", "source-alpha PBR acceptance self-test")
    for negative in (
        "sourceAlphaComplexBlendRejected",
        "sourceAlphaMismatchedAlbedoRejected",
        "sourceAlphaWrongPassRejected",
        "sourceAlphaTintedRejected",
        "sourceAlphaTransformedUVRejected",
    ):
        require(table_cpp, negative, "source-alpha PBR negative self-test")
    require(table_cpp, "unsupportedRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_UNSUPPORTED_WORKFLOW", "unsupported workflow table assertion")
    require(table_cpp, "missingAlbedoRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_ALBEDO", "missing albedo table assertion")
    require(table_cpp, "mutableImageRecord->pbrFallbackReason == MATERIAL_RESOURCE_PBR_FALLBACK_MISSING_IMAGE", "mutable image table assertion")
    require(table_cpp, "!redundantExplicitRecord->pbrUsesGeneratedLegacyFallback", "redundant explicit-map metric assertion")
    require(table_cpp, "rg_materialResourceTable.records[i].material = NULL;", "self-test declaration lifetime cleanup")

    draw_plan = read(ROOT / "src/renderer/ModernGLDrawPlan.cpp")
    submit_plan = read(ROOT / "src/renderer/ModernGLSubmitPlan.cpp")
    executor = read(ROOT / "src/renderer/ModernGLExecutor.cpp")
    for source, context in (
        (draw_plan, "draw plan"),
        (submit_plan, "submit plan"),
        (executor, "executor"),
    ):
        require(source, "R_MaterialResourceTable_PBRModernPathEligible", f"guarded PBR {context} admission")
    require(draw_plan, "const bool pbrGBufferCandidate", "dedicated PBR G-buffer admission")
    require(draw_plan, "const bool pbrDepthCandidate", "PBR depth-prepass admission")
    require(draw_plan, "R_ModernGLDrawPlan_IsDepthPipeline( pipeline )", "PBR depth-prepass route")
    forward_selection = function_body(draw_plan, "static bool R_ModernGLDrawPlan_ShouldUseForwardPlus(")
    require(forward_selection, "R_MaterialResourceTable_PBRTransparentPathEligible", "source-alpha PBR forward admission")
    require(forward_selection, "MODERN_GL_DRAW_PLAN_PIPELINE_FORWARD_PLUS_TRANSPARENT", "source-alpha PBR ordered forward route")
    require(forward_selection, "draw.passCategory != RENDER_PASS_AMBIENT", "ambient-only clustered PBR ownership")
    require(draw_plan, "R_ModernGLDrawPlan_SameStableSurface", "stable clustered PBR surface identity")
    stable_surface = function_body(draw_plan, "static bool R_ModernGLDrawPlan_SameStableSurface(")
    require(stable_surface, "R_ModernGLDrawPlan_SourceAmbientSurface( a )", "interaction source-surface identity")
    require(stable_surface, "aSource == bSource", "shared ambient-source match")
    reject(stable_surface, "a.legacyDrawSurf == b.legacyDrawSurf", "drawSurf pointer identity cannot match per-light receivers")
    reject(stable_surface, "a.indexCount == b.indexCount", "culled interaction subsets must retain their ambient owner")
    require(draw_plan, "R_ModernGLDrawPlan_PBRInteractionHasReadyClusteredOwner", "ready clustered PBR owner preflight")
    require(draw_plan, "stats.pbrClusteredConsumedInteractions++", "clustered PBR duplicate consumption")
    require(executor, "clusteredStats.pbrClusteredSurfaceOwners != 2", "clustered PBR owner self-test")
    require(executor, "clusteredStats.pbrClusteredConsumedInteractions != 4", "clustered PBR dedupe self-test")
    require(executor, "interactionTris[interactionIndex].ambientSurface", "distinct interaction subset self-test")
    require(executor, "pbrOwners=%d, pbrConsumed=%d", "clustered PBR gameplay telemetry")
    require(executor, "drawPlanStats.pbrClusteredSurfaceOwners", "clustered PBR owner telemetry copy")
    require(executor, "drawPlanStats.pbrClusteredConsumedInteractions", "clustered PBR interaction telemetry copy")
    require(draw_plan, "materialRecord->hasPBR && !forwardPlusCandidate && !pbrGBufferCandidate", "PBR flat-material rejection")
    require(submit_plan, "MATERIAL_RESOURCE_TEXTURE_ALBEDO", "PBR albedo direct sampler")
    require(submit_plan, "MATERIAL_RESOURCE_TEXTURE_ORM", "PBR ORM direct sampler")
    for semantic in ("METALLIC", "ROUGHNESS", "AO"):
        require(submit_plan, f"MATERIAL_RESOURCE_TEXTURE_{semantic}", "PBR separate-map direct sampler")
    shader_library = read(ROOT / "src/renderer/ModernGLShaderLibrary.cpp")
    require(shader_library, "ModernClusterEvaluatePBRLight", "PBR direct-light BRDF")
    require(shader_library, "ModernPBRMaterialData", "PBR G-buffer inputs")
    for token in ("uPBRIBL", "ModernPBRAnalyticEnvironment", "ModernPBRIndirect"):
        require(shader_library, token, "analytic PBR IBL shader contract")
    for token in (
        "struct ModernSpecularProbeRecord",
        "layout(std140) uniform ModernSpecularProbeRecords",
        "ModernSpecularProbeRecord probes[MODERN_SPECULAR_PROBE_MAX_RECORDS]",
        "uniform sampler2D uModernSpecularProbeAtlas",
        "float countValue = uClusterGrid.projectionDepth.z",
        "float frameGeneration = uClusterGrid.projectionDepth.w",
        "probe.identity.w != frameGeneration",
        "probeIndex = candidate == 0 ? clusterRange.z : clusterRange.w",
        "probeIndex >= probeCount",
        "ModernSpecularProbeExactInteger(probe.axisZSlot.w",
        "dot(reflectionDirection, normalize(probe.axisXPriority.xyz))",
        "dot(reflectionDirection, normalize(probe.axisYBlend.xyz))",
        "dot(reflectionDirection, normalize(probe.axisZSlot.xyz))",
        "weight = clamp((radius - distanceToProbe) / blendWidth, 0.0, 1.0)",
        "environment /= totalWeight",
        "textureLod(uModernSpecularProbeAtlas, atlasUV, float(level))",
        "faceUV * float(faceSize - 1)",
        "prefiltered = mix(prefiltered, probeEnvironment, probeCoverage)",
        "ModernProbeDiffuse(slot, localNormal)",
        "f0 * brdf.x + vec3(brdf.y)",
        "PBRDistributionGGX(ndoth, roughness)",
        "PBRVisibilitySmithGGX(ndotv, ndotl, roughness)",
    ):
        require(shader_library, token, "authored specular-probe shader contract")
    if shader_library.count("ModernPBRIndirect(") != 3 or shader_library.count("ModernPBRIndirectSource(") != 3:
        raise AssertionError("every deferred/opaque-alpha/transparent PBR indirect call must carry clusterRange")
    if shader_library.count("pbrData.z, clusterRange)") != 1 or shader_library.count("pbrData.z, clusterRange, baked)") != 1:
        raise AssertionError("opaque-alpha and transparent PBR indirect calls must carry clusterRange")
    require(
        shader_library,
        "material.b, clusterRange",
        "deferred PBR indirect cluster-range handoff",
    )
    for token in (
        '#include "ModernSpecularProbeAtlas.h"',
        "MODERN_GL_CLUSTER_UBO_BINDING_SPECULAR_PROBES = 7",
        '"ModernSpecularProbeRecords", MODERN_GL_CLUSTER_UBO_BINDING_SPECULAR_PROBES',
        'glUniform1i( specularProbeAtlas, MODERN_SPECULAR_PROBE_ATLAS_TEXTURE_UNIT )',
        "R_ModernGLExecutor_ShadowTextureUnitLimit() > MODERN_CURRENT_POINT_SHADOW_TEXTURE_UNIT",
        'glGetUniformBlockIndex( program->program, "ModernSpecularProbeRecords" )',
        "R_ModernClusteredLighting_ProbeUboBlockBytes()",
        "specular-probe UBO layout drift",
        "R_ModernSpecularProbeAtlas_Init( caps, features )",
        "R_ModernSpecularProbeAtlas_Shutdown()",
        "R_ModernSpecularProbeAtlas_BeginFrame( pbrEnvironmentRequested )",
        "R_ModernSpecularProbeAtlas_FlushUploads()",
        "R_ModernSpecularProbeAtlas_FrameReady()",
        "R_ModernGLExecutor_ShadowSlotPlaceholderTexture( GL_TEXTURE_2D )",
        "R_ModernSpecularProbeAtlas_PrintGfxInfo()",
        "RendererSpecularProbeAtlas_RunSelfTest()",
    ):
        require(executor, token, "authored specular-probe executor contract")
    for sampler in ("uMetallicTexture", "uRoughnessTexture", "uAOTexture"):
        require(shader_library, sampler, "PBR separate-map shader sampler")
    require(executor, "pbrSeparateMaterialData", "separate-map texture-table bypass")
    require(executor, "R_ModernGLExecutor_SetPBRIBL", "PBR IBL uniform binding")
    require(executor, "r_pbrIBLIntensity.GetFloat()", "PBR IBL intensity cvar binding")
    require(shader_library, "float pbrDebug = floor(uDebugColor.a + 0.5)", "deferred PBR debug routing")
    require(shader_library, "out_Emissive = vec4(emissive, pbr ? 1.0 : 0.0);", "explicit G-buffer PBR layout marker")
    require(shader_library, "bool pbr = emissiveRecord.a > 0.5;", "deferred explicit PBR marker decode")
    reject(shader_library, "bool pbr = material.a > 0.5;", "collision-prone material-alpha PBR marker")
    require(executor, "r_pbrDebug", "PBR debug controls")
    require(executor, "RendererPBRVisible self-test passed", "native PBR visible self-test")
    require(executor, "modern-quality rollback admitted PBR", "master-off native PBR rejection self-test")
    require(executor, "material->EvaluateRegisters( shaderRegisters", "PBR scalar-register self-test inputs")
    require(read(ROOT / "src/renderer/RenderSystem_init.cpp"), "PBR materials: parser=1 modernLighting=1", "PBR capability reporting")

    scene_packet_selftest = function_body(packets_cpp, "bool RendererScenePacket_RunSelfTest(")
    require(scene_packet_selftest, "pbrPacketFrame.AddDrawPacket", "native PBR scene-packet capture")
    require(scene_packet_selftest, "pbrDrawPacket->materialRecord == pbrRecord", "public draw-packet material linkage")
    require(scene_packet_selftest, "pbrRecord->pbrLegacyFallbackMissing", "PBR missing-fallback packet propagation assertion")
    require(scene_packet_selftest, "validRegister( pbrRecord->pbrMetallicRegister )", "PBR packet register range assertion")


def test_runtime_selftest_is_registered_and_required() -> None:
    init = read(ROOT / "src/renderer/RenderSystem_init.cpp")
    material = read(ROOT / "src/renderer/Material.cpp")
    table = read(ROOT / "src/renderer/MaterialResourceTable.cpp")
    matrix = read(ROOT / "tools/tests/renderer_validation_matrix.py")
    require(init, '"rendererPBRMaterialSelfTest"', "renderer command registration")
    require(init, '"RendererPBRMaterial self-test passed\\n"', "runtime success marker")
    require(matrix, '"RendererPBRMaterial self-test passed"', "matrix result contract")
    require(matrix, '"+rendererPBRMaterialSelfTest"', "matrix command contract")
    require(material, "generatedClassicInteractionFallback", "complete classic fallback parser coverage")
    for token in (
        "material->GetNumStages() == 3",
        "bumpStage->texture.image == globalImages->flatNormalMap",
        "diffuseStage->texture.image == info.albedo.image",
        "specularStage->texture.image == globalImages->blackImage",
    ):
        require(material, token, "complete classic fallback parser assertions")
    for source, context in ((material, "parser self-test"), (table, "resource-table self-test")):
        require(source, "declManager->AllocateDecl( DECL_MATERIAL )", context)
        require(source, "DeclManager_FreeAllocatedDecl", context)
        reject(source, "idMaterial dual;", context)


def test_controlled_fixture_gameplay_acceptance_gate() -> None:
    benchmark_source = read(ROOT / "tools/tests/renderer_gameplay_benchmark.py")
    for token in (
        "PBR_ACCEPTANCE_COMMON_CVARS",
        "PBR_ACCEPTANCE_GL_CVARS",
        "missing.extend(pbr_fixture_failures)",
        '"pbrFixtureEvidence": pbr_fixture_evidence',
        '"effectivePostMapCvars": effective_post_map_cvars(args.extra_cvars)',
        '"pbrFixtureBinding": args.pbr_fixture_binding',
        '("benchmarkConfig", "autoexecCfg")',
        'lines.append(f"set {name} {value}")',
        "verify_procedural_pbr_fixture",
        "recorded PBR fixture evidence differs",
    ):
        require(benchmark_source, token, "gameplay PBR acceptance integration")
    if benchmark_source.count(
        "pbr_fixture_acceptance_required=pbr_fixture_acceptance_required"
    ) < 3:
        raise AssertionError("SP and both MP roles must enforce PBR fixture acceptance")

    gl_fixture_cvars = (
        ("r_materialOverride", "materials/openq4/pbr_test/procedural_packed"),
        ("r_pbrMaterials", "1"),
        ("r_rendererModernQuality", "1"),
        ("r_rendererModernVisible", "1"),
        ("r_rendererForwardPlus", "1"),
    )
    vk_fixture_cvars = gl_fixture_cvars[:3]
    if not gameplay.requires_pbr_fixture_acceptance(gl_fixture_cvars, "gl"):
        raise AssertionError("exact controlled GL PBR fixture must activate gameplay acceptance")
    if not gameplay.requires_pbr_fixture_acceptance(vk_fixture_cvars, "vk"):
        raise AssertionError("exact controlled Vulkan PBR fixture must activate gameplay acceptance")
    if gameplay.requires_pbr_fixture_acceptance(vk_fixture_cvars, "gl"):
        raise AssertionError("GL acceptance must retain its Modern GL fixture controls")
    if not gameplay.requires_pbr_fixture_acceptance(gl_fixture_cvars, "vk"):
        raise AssertionError("Vulkan acceptance must ignore unrelated Modern GL controls")
    if not gameplay.requires_pbr_fixture_acceptance(
        (("r_pbrMaterials", "0"), *gl_fixture_cvars), "gl"
    ):
        raise AssertionError("controlled PBR fixture must use last-write-wins cvar semantics")
    for index, (name, value) in enumerate(gl_fixture_cvars):
        changed = list(gl_fixture_cvars)
        changed[index] = (
            (name, value + "_different")
            if name.casefold() == "r_materialoverride"
            else (name, "0")
        )
        if gameplay.requires_pbr_fixture_acceptance(changed, "gl"):
            raise AssertionError(f"non-exact {name} unexpectedly activated PBR acceptance")
    if gameplay.requires_pbr_fixture_acceptance(
        (*gl_fixture_cvars, ("R_PBRMATERIALS", "0")), "gl"
    ):
        raise AssertionError("case-insensitive final cvar override must disable PBR acceptance")
    for engine_true in ("01", "10", "1junk", "-1"):
        engine_semantics = tuple(
            (name, engine_true) if name.casefold() != "r_materialoverride" else (name, value)
            for name, value in gl_fixture_cvars
        )
        if not gameplay.requires_pbr_fixture_acceptance(engine_semantics, "gl"):
            raise AssertionError(
                f"engine-true CVAR_BOOL value {engine_true!r} bypassed fixture acceptance"
            )

    def assert_value_error(callback: object, context: str) -> None:
        try:
            callback()  # type: ignore[operator]
        except ValueError:
            return
        raise AssertionError(f"unsafe {context} did not fail closed")

    for unsafe_cvar in (
        "r_pbrMaterials=1;quit",
        "r_pbrMaterials=1\nquit",
        "r_pbrMaterials=1\x7fquit",
        "r_pbrMaterials=1\u2028quit",
        "r_pbrMaterials=$ fixtureValue",
        'r_pbrMaterials="1"',
        "r_pbrMaterials=+1",
        "r_materialOverride=materials/openq4/*mutable*/procedural",
        "fake=PBR material resources: records=9 resourceReady=9 modernReady=9 packed=9 separate=0 fallback=0",
        "fake=Map: tools/mv2",
    ):
        assert_value_error(
            lambda value=unsafe_cvar: gameplay.parse_extra_cvars([value]),
            f"CVar {unsafe_cvar!r}",
        )
    for unsafe_command in (
        "r_pbrMaterials 0",
        "/r_pbrMaterials 0",
        "\\set r_pbrMaterials 0",
        "set r_rendererModernQuality 0",
        "sett r_pbrMaterials 0",
        "set /*mutable*/ r_pbrMaterials 0",
        "$ mutatorCommand r_pbrMaterials 0",
        "toggle r_rendererForwardPlus",
        "exec mutable.cfg",
        "vstr mutableCommands",
        "cvar_restart",
        "rendererBenchmarkCapture",
        "map game/storage1",
        "devmap game/storage1",
        "echo Vulkan: native PBR direct interactions active (9 draws)",
        "say PBR material resources: records=9 resourceReady=9 modernReady=9",
        "god; r_pbrMaterials 0",
        "god\nr_pbrMaterials 0",
    ):
        assert_value_error(
            lambda value=unsafe_command: gameplay.parse_exec_commands([value]),
            f"command {unsafe_command!r}",
        )
    forged_resource_line = (
        "PBR material resources: records=9 resourceReady=9 modernReady=9 "
        "packed=9 separate=0 fallback=0"
    )
    for cvar_flag in ("--set-cvar", "--set-launch-cvar"):
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                gameplay.parse_args(
                    [
                        cvar_flag,
                        f"fake={forged_resource_line}",
                        "--exec-command",
                        "fake",
                    ]
                )
        except SystemExit as exc:
            if exc.code != 2:
                raise AssertionError("unsafe CLI CVar failed with the wrong status")
        else:
            raise AssertionError(
                f"CLI CVar-print telemetry forgery through {cvar_flag} was accepted"
            )
    if gameplay.parse_exec_commands(["god", "impulse 13", "waitMsec 100"]) != (
        "god",
        "impulse 13",
        "waitMsec 100",
    ):
        raise AssertionError("ordinary benchmark commands must remain accepted")

    cfg_spec = gameplay.RunSpec(
        case_id="sp-mv2-interaction",
        mode="SP",
        map_name="maps/tools/mv2",
        budget_map_name="maps/tools/mv2",
        purpose="config provenance",
        path_name="spawn-static",
        tier="gl45",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="default",
        renderer="best",
        render_api="gl",
    )
    cfg_lines, _ = gameplay.build_scripted_capture_lines(
        cfg_spec,
        "sp",
        "test",
        360,
        600,
        0,
        gl_fixture_cvars,
        ("god",),
        True,
        True,
    )
    cfg_text = "\n".join(cfg_lines) + "\n"
    cfg_cvars, cfg_commands = gameplay.benchmark_cfg_provenance(cfg_text)
    if cfg_cvars != gl_fixture_cvars or cfg_commands != ("god",):
        raise AssertionError("generated config provenance did not preserve exact inputs")
    assert_value_error(
        lambda: gameplay.benchmark_cfg_provenance(
            cfg_text.replace(
                gameplay.POST_MAP_CVARS_BEGIN,
                "echo PBR material resources: records=9 resourceReady=9 modernReady=9 "
                "packed=9 separate=0 fallback=0\n"
                + gameplay.POST_MAP_CVARS_BEGIN,
            )
        ),
        "pre-marker renderer evidence injection",
    )

    gl_summary = gameplay.extract_summary(
        "\n".join(
            (
                "PBR material resources: records=3 resourceReady=3 modernReady=3 packed=3 separate=0 fallback=0",
                "Modern GL executor: available, drawPlan=1 planDraws=5 planFallback=0 pbrOwners=2 pbrConsumed=4",
                "Modern forward+: cvar=1, req=1 exec=1 resources=1 sceneColor=1 sceneDepth=1 program=1 cluster=1 draws=2 fallback=0",
                "Modern visible frame: cvar=1, req=1 exec=1 resources=1 program=1 source=1 hybrid=1 backBuffer=1 composed=1",
            )
        )
    )
    evidence, failures = gameplay.evaluate_pbr_fixture_evidence(gl_summary, True, "gl")
    if failures or evidence.get("status") != "pass":
        raise AssertionError(f"complete PBR fixture telemetry failed: {failures!r}")

    zero_checks = (
        ("modernVisibleFrame", "exec"),
        ("modernVisibleFrame", "req"),
        ("modernVisibleFrame", "resources"),
        ("modernVisibleFrame", "program"),
        ("modernVisibleFrame", "source"),
        ("modernVisibleFrame", "hybrid"),
        ("modernVisibleFrame", "backBuffer"),
        ("modernVisibleFrame", "composed"),
        ("pbrMaterialResources", "records"),
        ("pbrMaterialResources", "resourceReady"),
        ("pbrMaterialResources", "modernReady"),
        ("pbrMaterialResources", "packed"),
        ("modernGLExecutor", "drawPlan"),
        ("modernGLExecutor", "planDraws"),
        ("modernGLExecutor", "pbrOwners"),
        ("modernGLExecutor", "pbrConsumed"),
        ("modernForwardPlus", "req"),
        ("modernForwardPlus", "exec"),
        ("modernForwardPlus", "resources"),
        ("modernForwardPlus", "sceneColor"),
        ("modernForwardPlus", "sceneDepth"),
        ("modernForwardPlus", "program"),
        ("modernForwardPlus", "cluster"),
        ("modernForwardPlus", "draws"),
    )
    for summary_name, field_name in zero_checks:
        broken = dict(gl_summary)
        broken[summary_name] = re.sub(
            rf"\b{field_name}=\d+\b",
            f"{field_name}=0",
            broken[summary_name],
            count=1,
        )
        broken_evidence, broken_failures = gameplay.evaluate_pbr_fixture_evidence(
            broken, True, "gl"
        )
        if not broken_failures or broken_evidence.get("status") != "fail":
            raise AssertionError(f"zero {field_name} did not fail PBR fixture acceptance")

    fallback = dict(gl_summary)
    fallback["modernGLExecutor"] = fallback["modernGLExecutor"].replace(
        "planFallback=0", "planFallback=1"
    )
    _, fallback_failures = gameplay.evaluate_pbr_fixture_evidence(fallback, True, "gl")
    if not fallback_failures:
        raise AssertionError("draw-plan fallback did not fail PBR fixture acceptance")
    for summary_name, field_name in (
        ("pbrMaterialResources", "fallback"),
        ("modernForwardPlus", "fallback"),
    ):
        broken = dict(gl_summary)
        broken[summary_name] = broken[summary_name].replace(
            f"{field_name}=0", f"{field_name}=1"
        )
        _, broken_failures = gameplay.evaluate_pbr_fixture_evidence(
            broken, True, "gl"
        )
        if not broken_failures:
            raise AssertionError(f"{summary_name} fallback did not fail acceptance")
    nested_shadow_fallback = dict(gl_summary)
    nested_shadow_fallback["modernForwardPlus"] = nested_shadow_fallback[
        "modernForwardPlus"
    ].replace(
        "fallback=0",
        "fallback=1 shadow(mapped=1 fallback=0 skipped=0 descriptors=1)",
        1,
    )
    _, nested_shadow_failures = gameplay.evaluate_pbr_fixture_evidence(
        nested_shadow_fallback, True, "gl"
    )
    if not any("forward+ fallback=0" in failure for failure in nested_shadow_failures):
        raise AssertionError(
            "top-level forward+ fallback was hidden by the nested shadow fallback"
        )
    unavailable = dict(gl_summary)
    unavailable["modernGLExecutor"] = unavailable["modernGLExecutor"].replace(
        "Modern GL executor: available,", "Modern GL executor: unavailable,"
    )
    _, unavailable_failures = gameplay.evaluate_pbr_fixture_evidence(
        unavailable, True, "gl"
    )
    if not unavailable_failures:
        raise AssertionError("unavailable Modern GL executor did not fail acceptance")
    forged = dict(gl_summary)
    forged["pbrMaterialResources"] = (
        'fake = "' + forged["pbrMaterialResources"] + '"'
    )
    _, forged_failures = gameplay.evaluate_pbr_fixture_evidence(forged, True, "gl")
    if not any("malformed" in failure for failure in forged_failures):
        raise AssertionError("unanchored CVar-print telemetry did not fail acceptance")
    vk_summary = gameplay.extract_summary(
        "\n".join(
            (
                "PBR material resources: records=2 resourceReady=2 modernReady=2 packed=2 separate=0 fallback=0",
                "Vulkan: native PBR direct interactions active (7 draws)",
            )
        )
    )
    vk_evidence, vk_failures = gameplay.evaluate_pbr_fixture_evidence(
        vk_summary, True, "vk"
    )
    if vk_failures or vk_evidence.get("status") != "pass":
        raise AssertionError(f"complete Vulkan PBR fixture telemetry failed: {vk_failures!r}")
    if any(name in vk_evidence for name in ("modernVisible", "drawPlan", "forwardPlus")):
        raise AssertionError("Vulkan acceptance must not depend on Modern GL telemetry")
    for marker in ("", "Vulkan: native PBR direct interactions active (0 draws)"):
        broken_vk = dict(vk_summary)
        broken_vk["vulkanPackedPBR"] = marker
        broken_evidence, broken_failures = gameplay.evaluate_pbr_fixture_evidence(
            broken_vk, True, "vk"
        )
        if not broken_failures or broken_evidence.get("status") != "fail":
            raise AssertionError("missing/zero Vulkan packed-PBR draw marker did not fail")
    final_zero_summary = gameplay.extract_summary(
        "\n".join(
            (
                vk_summary["pbrMaterialResources"],
                "Vulkan: native PBR direct interactions active (7 draws)",
                "Vulkan: native PBR direct interactions active (0 draws)",
            )
        )
    )
    _, final_zero_failures = gameplay.evaluate_pbr_fixture_evidence(
        final_zero_summary, True, "vk"
    )
    if not final_zero_failures:
        raise AssertionError("Vulkan acceptance did not use the final packed-PBR marker")
    for field_name in ("records", "resourceReady", "modernReady", "packed"):
        broken_vk = dict(vk_summary)
        broken_vk["pbrMaterialResources"] = re.sub(
            rf"\b{field_name}=\d+\b",
            f"{field_name}=0",
            broken_vk["pbrMaterialResources"],
            count=1,
        )
        _, broken_failures = gameplay.evaluate_pbr_fixture_evidence(
            broken_vk, True, "vk"
        )
        if not broken_failures:
            raise AssertionError(f"zero Vulkan {field_name} did not fail acceptance")
    vk_fallback = dict(vk_summary)
    vk_fallback["pbrMaterialResources"] = vk_fallback[
        "pbrMaterialResources"
    ].replace("fallback=0", "fallback=1")
    _, vk_fallback_failures = gameplay.evaluate_pbr_fixture_evidence(
        vk_fallback, True, "vk"
    )
    if not vk_fallback_failures:
        raise AssertionError("Vulkan common-resource fallback did not fail acceptance")

    optional_evidence, optional_failures = gameplay.evaluate_pbr_fixture_evidence(
        {}, False, "vk"
    )
    if optional_failures or optional_evidence.get("status") != "not-required":
        raise AssertionError("ordinary gameplay run unexpectedly required PBR telemetry")


def test_vulkan_pbr_support_stays_narrow_and_fail_closed() -> None:
    vulkan_backend = read(ROOT / "src/renderer/Vulkan/vk_Backend.cpp")
    require(
        vulkan_backend,
        "VK_GL_SELFTEST_STUB( RendererPBRVisible_RunSelfTest )",
        "Vulkan PBR self-test link closure and honest GL-only skip",
    )
    interactions = read(ROOT / "src/renderer/Vulkan/vk_Interactions.cpp")
    topology = function_body(
        interactions,
        "static bool VK_PBRHasSingleClassicInteractionTopology(",
    )
    for token in (
        "int bumpStage = -1;",
        "int diffuseStage = -1;",
        "int specularStage = -1;",
        "surfaceStage->newStage->customLighting",
        "*ownerStage >= 0",
        "surfaceStage->newStage != NULL",
        "surfaceStage->texture.image == NULL",
        "surfaceStage->conditionRegister >= registerCount",
        "const float condition = registers[ surfaceStage->conditionRegister ];",
        "!std::isfinite( condition )",
        "condition == 0.0f",
        "bumpStage >= 0 && diffuseStage > bumpStage && specularStage > diffuseStage",
    ):
        require(topology, token, "single-owner Vulkan packed-PBR topology")
    if topology.find("*ownerStage >= 0") > topology.find(
        "const float condition = registers[ surfaceStage->conditionRegister ];"
    ):
        raise AssertionError(
            "declared duplicate Vulkan interaction stages must be rejected before conditions"
        )
    packed_admission = function_body(
        interactions,
        "static bool VK_PBRDirectMaterial(",
    )
    require(
        packed_admission,
        "!VK_PBRHasSingleClassicInteractionTopology( surf )",
        "fail-closed Vulkan packed-PBR topology admission",
    )
    decomposition = function_body(
        interactions,
        "static void VK_CreateSingleDrawInteractions(",
    )
    require(
        decomposition,
        "const bool pbrOwnerEligible = VK_PBRHasSingleClassicInteractionTopology( surf );",
        "stable Vulkan packed-PBR surface owner",
    )
    if decomposition.count("VK_SubmitInteraction( &inter, false );") != 3:
        raise AssertionError("every intermediate Vulkan interaction flush must stay classic")
    if decomposition.count(
        "VK_SubmitInteraction( &inter, pbrOwnerEligible );"
    ) != 1:
        raise AssertionError("only the final Vulkan interaction submit may own packed PBR")
    reject(
        decomposition,
        "VK_SubmitInteraction( &inter );",
        "ungated Vulkan packed-PBR interaction submission",
    )
    for token in (
        "VK_PBRDirectInteraction",
        "!r_rendererModernQuality.GetBool()",
        "PBR_WORKFLOW_METALLIC_ROUGHNESS",
        "PBR_NORMAL_TANGENT_XYZ",
        "!VK_PBRHasMatchingDepthCoverage( surf )",
        "R_MaterialResourceTable_FindRecordForMaterial( material )",
        "R_MaterialResourceTable_PBRModernPathEligible( *resourceRecord )",
        "VK_PBRImageReady( info.albedo.image, TD_PBR_COLOR )",
        "VK_PBRImageReady( info.orm.image, TD_MATERIAL_DATA )",
        "VK_PBRImageReady( info.metallic.image, TD_MATERIAL_DATA )",
        "VK_PBRImageReady( info.roughness.image, TD_MATERIAL_DATA )",
        "R_MaterialResourceTable_PBREmissivePathEligible( *resourceRecord )",
        "VK_PBRImageReady( info.emissive.image, TD_PBR_COLOR )",
        "out.normalFormat = info.normal.present ? (int)info.normalFormat + 1 : 0;",
        "info.normalFormat == PBR_NORMAL_QUAKE4_AGB ? TD_BUMP : TD_MATERIAL_DATA",
        "pbr.metallicImage->GetDeviceHandle()",
        "r_pbrDebug.GetInteger() == 7",
        "native PBR direct interactions active",
        "VK_DrawSingleInteractionMode( din, false, 0.0f, 0.0f, allowNativePBR )",
        "VK_DrawSingleInteractionMode( din, parallax, scaleBias[ 0 ], scaleBias[ 1 ], false )",
    ):
        require(interactions, token, "narrow Vulkan packed-PBR admission")
    coverage = function_body(interactions, "static bool VK_PBRHasMatchingDepthCoverage(")
    for token in (
        "material->Coverage() == MC_OPAQUE", "material->Coverage() != MC_PERFORATED",
        "++alphaStages != 1", "stage->lighting != SL_DIFFUSE",
        "image->GetName(), albedo->GetName()", "image->GetFilter() != albedo->GetFilter()",
        "image->GetRepeat() != albedo->GetRepeat()", "stage->texture.hasMatrix",
        "stage->privatePolygonOffset != 0.0f", "stage->vertexColor != SVC_IGNORE",
        "stage->alphaTestRegister, -1.0f", "return alphaStages == 1;",
    ):
        require(coverage, token, "native Vulkan perforated depth coverage contract")
    emission = function_body(interactions, "bool VK_PBR_EmissionForStage(")
    for token in ("!VK_PBRDirectMaterial( surf, material )", "fallback->stageIndex != stageIndex",
                  "image = info.emissive.image", "info.emissiveColorRegisters[component]",
                  "color[component] = Max( 0.0f", "r_pbrDebug.GetInteger() == 7"):
        require(emission, token, "single native emission owner")
    executor = read(ROOT / "src/renderer/Vulkan/vk_GuiExecutor.cpp")
    for token in ("VK_PBR_EmissionForStage( drawSurf, stageNum, nativePBREmission, color )",
                  "nativePBREmission != NULL ? nativePBREmission : pStage->texture.image"):
        require(executor, token, "native emission replaces the classic ambient stage")
    require(function_body(executor, "static bool VK_ClassicWorldAmbient_Preflight("),
            "material->GetPBRInfo().emissive.present", "shared classic glow must yield to native emission")
    reject(emission, "65504.0f", "do not clamp intensity before emission texture modulation")
    require(executor, "push.params[ 0 ] = 3.0f", "native emission finite-storage mode")
    fragment = read(ROOT / "src/renderer/Vulkan/shaders/gui.frag")
    for token in ("if (pc.params.x > 2.5)", "vec3(65504.0)", "isnan(color.rgb)"):
        require(fragment, token, "evaluated emission radiance storage")
    for relative_path in (
        "src/renderer/Vulkan/shaders/interaction.frag",
        "src/renderer/Vulkan/shaders/interaction_shadow.frag",
        "src/renderer/Vulkan/shaders/interaction_shadow_point.frag",
    ):
        shader = read(ROOT / relative_path)
        for token in (
            "EvaluatePBRDirect",
            "PBRDirectNormal(bumpTexCoord)",
            '#include "../../PBRMath.h"',
            '#include "pbr_direct.glsl"',
            "if (pc.d.x > 1.5)",
            "if (pc.d.x > 2.5)",
            "outColor = vec4(0.0, 1.0, 0.0, 0.0);",
        ):
            require(shader, token, f"Vulkan packed-PBR shader {relative_path}")
    shared = read(ROOT / "src/renderer/Vulkan/shaders/pbr_direct.glsl")
    for token in ("PBRFilteredRoughness(roughness", "dFdx(objectNormal)", "dFdy(objectNormal)",
                  "SafeNormalize(vPBRTangent0) * localNormal.x", "SafeNormalize(vPBRNormal) * localNormal.z"):
        require(shared, token, "native final-normal specular filtering")
    if shared.index("dFdx(objectNormal)") > shared.index("if (ndotl <= 0.0"):
        raise AssertionError("native normal derivatives must precede per-fragment light rejection")
    require(interactions, "push.c[ 3 ] = r_vkPBRSpecularAA.GetBool() ? 1.0f : 0.0f;",
            "native specular-AA comparison switch")
    for stem in ("interaction", "interaction_shadow", "interaction_shadow_point"):
        for suffix, qualifier in (("vert", "out"), ("frag", "in")):
            source = read(ROOT / f"src/renderer/Vulkan/shaders/{stem}.{suffix}")
            for location, component, name in ((12, 1, "vPBRTangent0"), (14, 1, "vPBRTangent1"), (15, 0, "vPBRNormal")):
                layout = f"location = {location}" + (f", component = {component}" if component else "")
                require(source, f"layout({layout}) {qualifier} vec3 {name};", "packed native shading basis")
    for token in (
        "PBRDistributionGGX(ndoth, roughness)",
        "PBRVisibilitySmithGGX(ndotv, ndotl, roughness)",
        "vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic)",
        "texture(specularMap, dataTexCoord).bg",
        "texture(specularTableMap, dataTexCoord).r",
        "texture(specularMap, dataTexCoord).r",
        "value.agb",
        "sqrt(max(1.0 - dot(xy, xy), 0.0))",
    ):
        require(shared, token, "shared Vulkan PBR direct material decoding")


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
    print("renderer_pbr_materials: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Static checks for the Apple GL 2.1 ARB2 compatibility corridor."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_PATH = "docs/dev/plans/2026-06-30-apple-support-no-macos-access.md"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def source_section(source: str, start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    if start == -1:
        raise AssertionError(f"Missing section marker {start_marker!r}")
    end = source.find(end_marker, start)
    if end == -1:
        raise AssertionError(f"Missing section end marker {end_marker!r}")
    return source[start:end]


def validate_context_aware_apple_gl21_quirk() -> None:
    header = read("src/renderer/RendererCaps.h")
    source = read("src/renderer/RendererCaps.cpp")
    init_source = read("src/renderer/RenderSystem_init.cpp")

    for token in (
        "int\t\t\t\t\t\t\tglMajor;",
        "int\t\t\t\t\t\t\tglMinor;",
        "rendererContextProfile_t\tprofile;",
        "bool\t\t\t\t\t\thasFixedFunctionCompatibility;",
    ):
        require(header, token, "renderer driver info selected context facts")

    parse_body = function_body(source, "static bool RendererDriverQuirk_ParseGLVersionPrefix( const char *version, int &major, int &minor ) {")
    apple_body = function_body(source, "static bool RendererDriverQuirk_IsAppleGL21CompatibilityFallback( const renderBackendCaps_t &caps, const rendererDriverInfo_t &driverInfo ) {")
    apply_body = function_body(source, "void RendererDriverQuirks_Apply( renderBackendCaps_t &caps, const rendererDriverInfo_t &driverInfo ) {")
    self_test = function_body(source, "bool RendererCompatibilityGates_RunSelfTest( void ) {")

    for token in (
        "while ( *cursor != '\\0' && ( *cursor < '0' || *cursor > '9' ) )",
        "parsedMajor = parsedMajor * 10",
        "parsedMinor = parsedMinor * 10",
    ):
        require(parse_body, token, "Apple GL version prefix parser")

    for token in (
        'idStr::Icmp( driverInfo.vendor, "Apple" )',
        "RendererDriverQuirk_ParseGLVersionPrefix",
        "reportsGL21",
        "selectedGL21",
        "selectedCompatibility",
        "driverInfo.profile == RENDERER_CONTEXT_PROFILE_COMPATIBILITY",
        "driverInfo.hasFixedFunctionCompatibility",
        "caps.profile == RENDERER_CONTEXT_PROFILE_COMPATIBILITY",
        "caps.hasFixedFunctionCompatibility",
    ):
        require(apple_body, token, "context-aware Apple GL 2.1 detector")

    for token in (
        "RendererDriverQuirk_IsAppleGL21CompatibilityFallback( caps, driverInfo )",
        "RENDERER_DRIVER_QUIRK_DISABLE_VBO | RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION",
        "Apple OpenGL 2.1 compatibility path uses CPU-backed vertex cache and simple ARB interactions for stability",
        "selectedContext=%d.%d %s fixedFunction=%d VBO:%d->%d PBO:%d->%d simpleInteraction=%d ARB2:%d->%d",
    ):
        require(apply_body, token, "Apple GL 2.1 quirk application and logging")

    for token in (
        '"Apple M4 Max"',
        '"Apple M5"',
        '"Apple M4 Max macOS 15.x"',
        '"Apple M5 macOS 16 Tahoe"',
        '"unknown"',
        '"2.1 Metal"',
        '"OpenGL 2.1 Metal"',
        '"Apple Silicon modern context"',
        "RENDERER_DRIVER_QUIRK_NONE",
        "expectedFlags",
        "expectedVBO",
        "caps.hasVBO != quirkCasesTable[i].expectedVBO",
    ):
        require(self_test, token, "Apple GL 2.1 synthetic quirk cases")

    for token in (
        "glConfig.backendCaps.glMajor",
        "glConfig.backendCaps.glMinor",
        "glConfig.backendCaps.profile",
        "glConfig.backendCaps.hasFixedFunctionCompatibility",
    ):
        require(init_source, token, "renderer driver info runtime context facts")


def validate_simple_interaction_fail_closed() -> None:
    source = read("src/renderer/draw_arb2.cpp")
    reload_body = function_body(source, "void R_ReloadARBPrograms_f( const idCmdArgs &args ) {")
    failure_body = function_body(source, "static void RB_ErrorIfDriverRequiredSimpleInteractionFailed( void ) {")

    for token in (
        "RB_DriverPrefersSimpleInteraction()",
        "VPROG_SIMPLE_INTERACTION",
        "FPROG_SIMPLE_INTERACTION",
        "vertexRecord->valid",
        "fragmentRecord->valid",
        "Unsupported Apple OpenGL 2.1 compatibility path",
        "required SimpleInteraction.vfp ARB programs failed to load",
        "cannot safely continue on this driver path",
        "common->Error",
    ):
        require(failure_body, token, "Apple GL 2.1 simple interaction fail-closed path")

    for token in (
        "RB_RecordCurrentInteractionSelectionBreadcrumb();",
        "RB_ErrorIfDriverRequiredSimpleInteractionFailed();",
    ):
        require(reload_body, token, "ARB reload simple interaction fail-closed call")

    for token in (
        "static bool RB_ShouldSkipFullInteractionUpload( const progDef_t &prog )",
        "RB_DriverPrefersSimpleInteraction()",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_SKIP_FULL_UPLOAD",
        "skipped by renderer driver quirk; using SimpleInteraction.vfp",
        "RB_IsSimpleInteractionProgram( prog )",
    ):
        require(source, token, "Apple GL 2.1 skips full interaction upload and uses simple interaction")


def validate_arb_entrypoint_and_binding_audit() -> None:
    init_source = read("src/renderer/RenderSystem_init.cpp")
    arb2_source = read("src/renderer/draw_arb2.cpp")
    arb_guard = function_body(init_source, "static bool R_HasARBProgramEntryPoints() {")
    bind_helper = function_body(arb2_source, "bool R_BindARBProgram( GLenum target, GLuint ident, const char *usage, bool required ) {")
    load_body = function_body(arb2_source, "void R_LoadARBProgram( int progIndex ) {")

    for token in (
        "glBindProgramARB != NULL",
        "glProgramStringARB != NULL",
        "glProgramEnvParameter4fvARB != NULL",
        "glProgramLocalParameter4fvARB != NULL",
        "glVertexAttribPointerARB != NULL",
        "glEnableVertexAttribArrayARB != NULL",
        "glDisableVertexAttribArrayARB != NULL",
    ):
        require(arb_guard, token, "ARB2 entry-point gate")

    for token in (
        "RB_FindARBProgramRecord",
        "prog == NULL || !prog->valid",
        "RB_WarnInvalidARBProgramUse",
        "glBindProgramARB( target, ident )",
    ):
        require(bind_helper, token, "ARB program binding helper")

    for token in (
        "if ( !glConfig.ARBVertexProgramAvailable )",
        "if ( !glConfig.ARBFragmentProgramAvailable )",
        "glProgramStringARB",
        "GL_PROGRAM_ERROR_POSITION_ARB",
        "RB_SetARBProgramFailure",
    ):
        require(load_body, token, "ARB program upload validation")


def validate_upload_and_vertex_cache_static_coverage() -> None:
    upload = read("src/renderer/RendererUpload.cpp")
    vertex_cache = read("src/renderer/VertexCache.cpp")
    arb2 = read("src/renderer/draw_arb2.cpp")

    upload_init = function_body(upload, "void idUploadManager::Init( const renderBackendCaps_t &caps ) {")
    delete_helper = function_body(upload, "static void R_RendererUpload_DeleteBufferName( unsigned int &vbo ) {")
    position_body = function_body(vertex_cache, "void *idVertexCache::Position( vertCache_t *buffer ) {")
    attr_helper = function_body(arb2, "static void *RB_DrawVertAttributePointer( const idDrawVert *base, const int byteOffset ) {")
    interaction_section = source_section(arb2, "// set the vertex pointers", "// this may cause RB_ARB2_DrawInteraction")

    for token in (
        "if ( requestedPath == UPLOAD_PATH_DISABLED )",
        "frameBufferCount = 0;",
        "activeRingBytes = requestedPath == UPLOAD_PATH_DISABLED ? 0 : ringBytes",
        "hasSync = requestedPath != UPLOAD_PATH_DISABLED",
    ):
        require(upload_init, token, "disabled renderer upload bridge state")

    for token in (
        "if ( glDeleteBuffersARB != NULL )",
        "glDeleteBuffersARB( 1, &vbo )",
        "vbo = 0;",
    ):
        require(delete_helper, token, "guarded renderer upload buffer deletion")

    for token in (
        "if ( buffer->vbo )",
        "BindIndexBuffer( buffer->vbo )",
        "BindArrayBuffer( buffer->vbo )",
        "return (void *)buffer->offset",
        "return (void *)((byte *)buffer->virtMem + buffer->offset)",
    ):
        require(position_body, token, "CPU-backed and VBO-backed vertex-cache position contract")

    require(attr_helper, "reinterpret_cast<uintptr_t>( base ) + byteOffset", "VBO-safe draw-vertex attribute pointer helper")
    for token in (
        "RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET )",
    ):
        require(interaction_section, token, "classic ARB2 interaction VBO-safe offsets")

    for token in (
        "ac->color",
        "ac->normal.ToFloatPtr()",
        "ac->tangents[1].ToFloatPtr()",
        "ac->tangents[0].ToFloatPtr()",
        "ac->st.ToFloatPtr()",
        "ac->xyz.ToFloatPtr()",
    ):
        reject(interaction_section, token, "classic ARB2 interaction must not dereference VBO offset tokens")


def validate_phase3_plan_status() -> None:
    plan = read(PLAN_PATH)

    for token in (
        "## Phase 3: Harden The Apple GL 2.1 ARB2 Corridor",
        "- [x] Expand `RendererDriverQuirks` synthetic cases for known report strings:",
        "- [x] Make Apple GL 2.1 fallback detection depend on normalized vendor/version",
        "- [x] Log every driver quirk that changes `hasVBO`, simple-interaction",
        "- [x] Add a static test proving Apple GL 2.1 disables the VBO vertex cache even",
        "- [x] Add a static test proving Apple GL 2.1 prefers `SimpleInteraction.vfp`",
        "- [x] Add a fail-closed path if the simple interaction program cannot load:",
        "- [x] Audit every `glBindProgramARB`, `glProgramStringARB`,",
        "- [x] Move repeated ARB program binding checks behind small helpers where that",
        "- [x] Add static coverage for disabled upload-manager state: no ring buffers, no",
        "- [x] Add static coverage for CPU-backed vertex-cache paths: all client-array",
        "- [x] Add static coverage for VBO-backed paths: all `idDrawVert` member offsets",
        "Phase 3 implementation status",
        "tools/tests/macos_apple_gl21_arb2_corridor.py",
    ):
        require(plan, token, "Phase 3 Apple support plan")


def validate_docs_and_wiring() -> None:
    support_doc = read("docs/user/macos-support-data.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")
    release_completion = read("docs/dev/release-completion.md")
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")

    for source, context in (
        (support_doc, "macOS support-data guide"),
        (release_notes, "curated release notes"),
        (release_completion, "release completion notes"),
    ):
        require(source, "Unsupported Apple OpenGL 2.1 compatibility path", context)
        require(source, "SimpleInteraction.vfp", context)

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "macos_apple_gl21_arb2_corridor.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_apple_gl21_arb2_corridor.py", context)


def main() -> None:
    validate_context_aware_apple_gl21_quirk()
    validate_simple_interaction_fail_closed()
    validate_arb_entrypoint_and_binding_audit()
    validate_upload_and_vertex_cache_static_coverage()
    validate_phase3_plan_status()
    validate_docs_and_wiring()
    print("macos_apple_gl21_arb2_corridor: ok")


if __name__ == "__main__":
    main()

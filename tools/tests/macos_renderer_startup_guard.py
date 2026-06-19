#!/usr/bin/env python3
"""Regression checks for macOS renderer startup guards from issue #73."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ISSUE_COMMENTS = ("4730727130", "4749280134", "4749378065")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


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


def validate_renderer_entry_point_guards() -> None:
    source = read("src/renderer/RenderSystem_init.cpp")
    multitexture_guard = function_body(source, "static bool R_HasARBMultitextureEntryPoints() {")
    buffer_guard = function_body(source, "static bool R_HasARBBufferObjectEntryPoints() {")
    vbo_guard = function_body(source, "static bool R_HasARBVertexBufferObjectEntryPoints() {")
    pbo_guard = function_body(source, "static bool R_HasARBPixelBufferObjectEntryPoints() {")
    arb_guard = function_body(source, "static bool R_HasARBProgramEntryPoints() {")
    glsl_guard = function_body(source, "static bool R_HasGLSLProgramEntryPoints() {")
    glsl_cap = function_body(source, "static bool R_CanUseGLSLPrograms() {")

    for token in (
        "glActiveTextureARB != NULL",
        "glClientActiveTextureARB != NULL",
        "glMultiTexCoord2fARB != NULL",
    ):
        require(multitexture_guard, token, "ARB multitexture entry-point guard")

    for token in (
        "glBindBufferARB != NULL",
        "glDeleteBuffersARB != NULL",
        "glGenBuffersARB != NULL",
        "glBufferDataARB != NULL",
    ):
        require(buffer_guard, token, "ARB buffer-object entry-point guard")

    for token in (
        "R_HasARBBufferObjectEntryPoints()",
        "glBufferSubDataARB != NULL",
    ):
        require(vbo_guard, token, "ARB VBO entry-point guard")

    for token in (
        "R_HasARBBufferObjectEntryPoints()",
        "glMapBufferARB != NULL",
        "glUnmapBufferARB != NULL",
    ):
        require(pbo_guard, token, "ARB PBO entry-point guard")

    for token in (
        "glBindProgramARB != NULL",
        "glProgramStringARB != NULL",
        "glProgramEnvParameter4fvARB != NULL",
        "glProgramLocalParameter4fvARB != NULL",
        "glVertexAttribPointerARB != NULL",
        "glEnableVertexAttribArrayARB != NULL",
        "glDisableVertexAttribArrayARB != NULL",
    ):
        require(arb_guard, token, "ARB program entry-point guard")

    for token in (
        "glCreateShaderObjectARB != NULL",
        "glShaderSourceARB != NULL",
        "glCompileShaderARB != NULL",
        "glGetObjectParameterivARB != NULL",
        "glGetInfoLogARB != NULL",
        "glCreateProgramObjectARB != NULL",
        "glAttachObjectARB != NULL",
        "glDetachObjectARB != NULL",
        "glBindAttribLocationARB != NULL",
        "glLinkProgramARB != NULL",
        "glUseProgramObjectARB != NULL",
        "glGetUniformLocationARB != NULL",
        "glUniform1fARB != NULL",
        "glUniform1fvARB != NULL",
        "glUniform1iARB != NULL",
        "glUniform2fvARB != NULL",
        "glUniform3fvARB != NULL",
        "glUniform4fvARB != NULL",
        "glUniformMatrix4fvARB != NULL",
        "glVertexAttribPointerARB != NULL",
        "glEnableVertexAttribArrayARB != NULL",
        "glDisableVertexAttribArrayARB != NULL",
        "glDeleteObjectARB != NULL",
    ):
        require(glsl_guard, token, "GLSL entry-point guard")

    require(glsl_cap, "R_HasGLSLProgramEntryPoints()", "GLSL capability gate")

    for token in (
        "glConfig.multitextureAvailable && !R_HasARBMultitextureEntryPoints()",
        'R_RecordMissingRequiredOpenGLFeature( "GL_ARB_multitexture entry points" )',
        "glConfig.ARBVertexBufferObjectAvailable && !R_HasARBVertexBufferObjectEntryPoints()",
        "using virtual-memory vertex cache",
        "pixelBufferObjectAdvertised && !glConfig.pixelBufferObjectAvailable",
        "async readbacks disabled",
        "glConfig.ARBVertexProgramAvailable && !R_HasARBProgramEntryPoints()",
        'R_RecordMissingRequiredOpenGLFeature( "GL_ARB_vertex_program entry points" )',
        "glConfig.ARBFragmentProgramAvailable && !R_HasARBProgramEntryPoints()",
        'R_RecordMissingRequiredOpenGLFeature( "GL_ARB_fragment_program entry points" )',
    ):
        require(source, token, "renderer startup capability fallback")


def validate_docs_and_validation() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    release = read("docs-dev/release-completion.md")
    platform = read("docs-dev/platform-support.md")

    for haystack, context in (
        (validator, "validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(haystack, "macos_renderer_startup_guard.py", context)

    require(release, "macOS renderer startup now validates the exact callable OpenGL entry points", "release completion notes")
    require(platform, "macOS startup validates both advertised OpenGL extensions and the callable entry points", "platform support docs")
    for issue_comment in ISSUE_COMMENTS:
        require(release, issue_comment, "issue comment traceability")


def main() -> None:
    validate_renderer_entry_point_guards()
    validate_docs_and_validation()
    print("macos_renderer_startup_guard: ok")


if __name__ == "__main__":
    main()

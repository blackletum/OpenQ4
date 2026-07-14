#!/usr/bin/env python3
"""Guard generic engine code against LP64 and AArch64 portability regressions."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class CppToken:
    value: str
    line: int


def cpp_tokens(source: str) -> list[CppToken]:
    """Return code tokens while ignoring comments and quoted text."""
    tokens: list[CppToken] = []
    line = 1
    index = 0
    operators = ("->", "++", "--", "+=", "-=", "==", "!=", "&&", "||", "::")

    while index < len(source):
        character = source[index]
        if character.isspace():
            line += character == "\n"
            index += 1
            continue
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            index = len(source) if end < 0 else end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = len(source) if end < 0 else end + 2
            line += source.count("\n", index, end)
            index = end
            continue
        if character in ('"', "'"):
            quote = character
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    index += 2
                    continue
                line += source[index] == "\n"
                if source[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if character.isalpha() or character == "_":
            end = index + 1
            while end < len(source) and (source[end].isalnum() or source[end] == "_"):
                end += 1
            tokens.append(CppToken(source[index:end], line))
            index = end
            continue
        if character.isdigit():
            end = index + 1
            while end < len(source) and (source[end].isalnum() or source[end] in "._"):
                end += 1
            tokens.append(CppToken(source[index:end], line))
            index = end
            continue

        operator = next((candidate for candidate in operators if source.startswith(candidate, index)), None)
        if operator is not None:
            tokens.append(CppToken(operator, line))
            index += len(operator)
            continue
        tokens.append(CppToken(character, line))
        index += 1

    return tokens


def token_pairs(tokens: list[CppToken]) -> tuple[dict[int, int], dict[int, int]]:
    opening_for = {")": "(", "]": "[", "}": "{"}
    stacks: dict[str, list[int]] = {"(": [], "[": [], "{": []}
    pairs: dict[int, int] = {}
    reverse_pairs: dict[int, int] = {}
    for index, token in enumerate(tokens):
        if token.value in stacks:
            stacks[token.value].append(index)
        elif token.value in opening_for:
            opening = opening_for[token.value]
            if not stacks[opening]:
                continue
            start = stacks[opening].pop()
            pairs[start] = index
            reverse_pairs[index] = start
    return pairs, reverse_pairs


def is_identifier_token(token: CppToken) -> bool:
    return token.value[0].isalpha() or token.value[0] == "_"


def looks_like_c_style_cast(tokens: list[CppToken], start: int, end: int) -> bool:
    if start >= end:
        return False
    cast_punctuation = {"*", "&", "::", "<", ">", ",", "[", "]"}
    return all(is_identifier_token(token) or token.value in cast_punctuation for token in tokens[start:end])


def position_derived_expression_span(
    tokens: list[CppToken],
    pairs: dict[int, int],
    reverse_pairs: dict[int, int],
    start: int,
    end: int,
) -> tuple[int, int]:
    """Include casts/grouping that preserve a Position-derived pointer value."""
    while True:
        changed = False
        if start > 0 and tokens[start - 1].value == ")":
            cast_start = reverse_pairs.get(start - 1)
            if cast_start is not None and looks_like_c_style_cast(tokens, cast_start + 1, start - 1):
                start = cast_start
                changed = True

        if (
            start > 0
            and end + 1 < len(tokens)
            and tokens[start - 1].value == "("
            and pairs.get(start - 1) == end + 1
        ):
            before_group = tokens[start - 2] if start > 1 else None
            if before_group is None or (
                not is_identifier_token(before_group) and before_group.value not in (")", "]")
            ):
                start -= 1
                end += 1
                changed = True

        if not changed:
            return start, end


def is_position_derived_boolean_cast(
    tokens: list[CppToken], pairs: dict[int, int], start: int, end: int
) -> bool:
    # Functional cast: bool( positionDerivedValue )
    if (
        start > 1
        and end + 1 < len(tokens)
        and tokens[start - 1].value == "("
        and pairs.get(start - 1) == end + 1
        and tokens[start - 2].value == "bool"
    ):
        return True

    # Named cast: static_cast<bool>( positionDerivedValue ). The expression
    # span has already absorbed the argument parentheses at this point.
    return (
        start >= 4
        and [token.value for token in tokens[start - 4 : start]]
        == ["static_cast", "<", "bool", ">"]
    )


def containing_function_end(
    tokens: list[CppToken], pairs: dict[int, int], reverse_pairs: dict[int, int], position: int
) -> int:
    containing_braces = [
        start for start, end in pairs.items() if tokens[start].value == "{" and start < position < end
    ]
    control_words = {"if", "for", "while", "switch", "catch"}
    for brace in reversed(containing_braces):
        previous = brace - 1
        if previous >= 0 and tokens[previous].value == ")":
            parameters = reverse_pairs.get(previous)
            if parameters is not None and parameters > 0 and tokens[parameters - 1].value not in control_words:
                return pairs[brace]
    if containing_braces:
        return pairs[containing_braces[0]]
    return len(tokens)


def position_assignment_alias(tokens: list[CppToken], position: int) -> str | None:
    statement_start = position - 1
    while statement_start >= 0 and tokens[statement_start].value not in (";", "{", "}"):
        statement_start -= 1
    assignment = next(
        (index for index in range(position - 1, statement_start, -1) if tokens[index].value == "="),
        None,
    )
    if assignment is None or any(token.value == "," for token in tokens[assignment + 1 : position]):
        return None
    for token in reversed(tokens[statement_start + 1 : assignment]):
        if token.value[0].isalpha() or token.value[0] == "_":
            return token.value
    return None


def reject_unsafe_position_alias(
    path: Path,
    tokens: list[CppToken],
    pairs: dict[int, int],
    reverse_pairs: dict[int, int],
    alias: str,
    start: int,
    end: int,
    visited: set[tuple[str, int]] | None = None,
) -> None:
    if visited is None:
        visited = set()
    origin = (alias, start)
    if origin in visited:
        return
    visited.add(origin)

    null_values = {"0", "NULL", "nullptr"}
    arithmetic = {"+", "-", "+=", "-=", "++", "--"}
    index = start
    while index < end:
        if tokens[index].value != alias:
            index += 1
            continue

        immediate_following = tokens[index + 1].value if index + 1 < len(tokens) else ""

        # A later assignment ends this alias's Position-derived lifetime. This
        # also prevents unrelated locals with a common name such as `ac` from
        # being diagnosed elsewhere in the same function.
        if immediate_following == "=":
            return

        span_start, span_end = position_derived_expression_span(
            tokens, pairs, reverse_pairs, index, index
        )
        previous = tokens[span_start - 1].value if span_start > 0 else ""
        previous_previous = tokens[span_start - 2].value if span_start > 1 else ""
        following = tokens[span_end + 1].value if span_end + 1 < len(tokens) else ""
        following_following = tokens[span_end + 2].value if span_end + 2 < len(tokens) else ""
        direct_boolean_context = (
            span_start > 1
            and span_end + 1 < len(tokens)
            and tokens[span_start - 1].value == "("
            and pairs.get(span_start - 1) == span_end + 1
            and tokens[span_start - 2].value in {"if", "while", "switch"}
        )
        boolean_cast = is_position_derived_boolean_cast(tokens, pairs, span_start, span_end)

        reason = None
        if following in {"->", "["}:
            reason = "typed member/index access"
        elif following in arithmetic or previous in arithmetic:
            reason = "pointer arithmetic"
        elif previous == "*":
            before_dereference = previous_previous
            if before_dereference == "(" and span_start > 2:
                before_dereference = tokens[span_start - 3].value
            if before_dereference not in {"sizeof", "decltype"}:
                reason = "pointer dereference"
        elif previous == "!":
            reason = "zero-offset null test"
        elif boolean_cast or direct_boolean_context or following in {"?", "&&", "||"} or previous in {"&&", "||"}:
            reason = "zero-offset boolean test"
        elif following in {"==", "!="} and following_following in null_values:
            reason = "zero-offset null comparison"
        elif previous in {"==", "!="} and previous_previous in null_values:
            reason = "zero-offset null comparison"

        if reason is not None:
            raise AssertionError(
                f"Unsafe vertexCache.Position()-derived alias {alias!r} ({reason}) "
                f"in {path.relative_to(ROOT)}:{tokens[index].line}"
            )

        copied_alias = position_assignment_alias(tokens, index)
        if copied_alias is not None and copied_alias != alias:
            reject_unsafe_position_alias(
                path, tokens, pairs, reverse_pairs, copied_alias, index + 1, end, visited
            )
        index += 1


def audit_renderer_source(path: Path, source: str) -> int:
    """Audit one renderer translation unit and return its Position() count."""
    tokens = cpp_tokens(source)
    pairs, reverse_pairs = token_pairs(tokens)
    positions = [
        index
        for index in range(len(tokens) - 3)
        if [token.value for token in tokens[index : index + 4]] == ["vertexCache", ".", "Position", "("]
    ]
    for position in positions:
        call_open = position + 3
        call_end = pairs.get(call_open)
        if call_end is None:
            raise AssertionError(
                f"Unbalanced vertexCache.Position() call in {path.relative_to(ROOT)}:{tokens[position].line}"
            )

        span_start, span_end = position_derived_expression_span(
            tokens, pairs, reverse_pairs, position, call_end
        )
        previous = tokens[span_start - 1].value if span_start > 0 else ""
        previous_previous = tokens[span_start - 2].value if span_start > 1 else ""
        following = tokens[span_end + 1].value if span_end + 1 < len(tokens) else ""
        following_following = tokens[span_end + 2].value if span_end + 2 < len(tokens) else ""
        direct_boolean_context = (
            span_start > 1
            and span_end + 1 < len(tokens)
            and tokens[span_start - 1].value == "("
            and pairs.get(span_start - 1) == span_end + 1
            and tokens[span_start - 2].value in {"if", "while", "switch"}
        )
        boolean_cast = is_position_derived_boolean_cast(tokens, pairs, span_start, span_end)
        if previous in {"!", "+", "-", "++", "--", "*"} or following in {
            "->", "[", "+", "-", "++", "--", "+=", "-=", "?", "&&", "||",
        } or previous in {"&&", "||"} or direct_boolean_context or boolean_cast or (
            following in {"==", "!="} and following_following in {"0", "NULL", "nullptr"}
        ) or (
            previous in {"==", "!="} and previous_previous in {"0", "NULL", "nullptr"}
        ):
            raise AssertionError(
                f"Unsafe direct vertexCache.Position() pointer use in "
                f"{path.relative_to(ROOT)}:{tokens[position].line}"
            )

        alias = position_assignment_alias(tokens, position)
        if alias is not None:
            reject_unsafe_position_alias(
                path,
                tokens,
                pairs,
                reverse_pairs,
                alias,
                call_end + 1,
                containing_function_end(tokens, pairs, reverse_pairs, position),
            )

    # This helper intentionally returns the encoded VBO offset through an
    # output reference. Treat every caller's second argument as Position-
    # derived so new caller-side member arithmetic cannot bypass the audit.
    helper_name = "RB_GLSLPrepareInteractionVertexCache"
    for index in range(len(tokens) - 1):
        if tokens[index].value != helper_name or tokens[index + 1].value != "(":
            continue
        call_end = pairs.get(index + 1)
        if call_end is None or (call_end + 1 < len(tokens) and tokens[call_end + 1].value == "{"):
            continue
        depth = 0
        comma = None
        for argument_index in range(index + 2, call_end):
            value = tokens[argument_index].value
            if value in ("(", "[", "{"):
                depth += 1
            elif value in (")", "]", "}"):
                depth -= 1
            elif value == "," and depth == 0:
                comma = argument_index
                break
        if comma is None:
            raise AssertionError(
                f"Cannot inventory {helper_name} output argument in "
                f"{path.relative_to(ROOT)}:{tokens[index].line}"
            )
        output_identifiers = [
            token.value
            for token in tokens[comma + 1 : call_end]
            if token.value[0].isalpha() or token.value[0] == "_"
        ]
        if not output_identifiers:
            raise AssertionError(
                f"Cannot identify {helper_name} output alias in "
                f"{path.relative_to(ROOT)}:{tokens[index].line}"
            )
        reject_unsafe_position_alias(
            path,
            tokens,
            pairs,
            reverse_pairs,
            output_identifiers[-1],
            call_end + 1,
            containing_function_end(tokens, pairs, reverse_pairs, index),
        )

    return len(positions)


def audit_renderer_vertex_cache_consumers() -> dict[str, int]:
    """Inventory every renderer Position() call and reject zero-offset UB."""
    inventory: dict[str, int] = {}
    renderer_root = ROOT / "src/renderer"
    renderer_extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
    renderer_sources = sorted(
        path
        for path in renderer_root.rglob("*")
        if path.is_file() and path.suffix.lower() in renderer_extensions
    )
    for path in renderer_sources:
        source = path.read_text(encoding="utf-8", errors="surrogateescape")
        count = audit_renderer_source(path, source)
        if count:
            inventory[str(path.relative_to(ROOT))] = count

    if not inventory:
        raise AssertionError("No renderer vertexCache.Position() consumers were inventoried")
    return inventory


def self_check_renderer_vertex_cache_audit() -> None:
    """Prove the lightweight analyzer recognizes its critical safe/unsafe forms."""
    synthetic_path = ROOT / "src/renderer/__position_audit_self_check.cpp"
    unsafe_sources = {
        "renamed member": "void f() { idDrawVert *verts = (idDrawVert *)vertexCache.Position( h ); verts->xyz; }",
        "copied alias": "void f() { idDrawVert *verts = (idDrawVert *)vertexCache.Position( h ); idDrawVert *copy = verts; &copy[0].st; }",
        "negated alias": "void f() { void *offset = vertexCache.Position( h ); if ( !offset ) {} }",
        "alias nullptr": "void f() { void *offset = vertexCache.Position( h ); if ( offset == nullptr ) {} }",
        "reversed alias NULL": "void f() { void *offset = vertexCache.Position( h ); if ( NULL != offset ) {} }",
        "direct zero": "void f() { if ( vertexCache.Position( h ) == 0 ) {} }",
        "reversed direct nullptr": "void f() { if ( nullptr != vertexCache.Position( h ) ) {} }",
        "pointer arithmetic": "void f() { idDrawVert *verts = (idDrawVert *)vertexCache.Position( h ); use( verts + n ); }",
        "output reference": "void f() { idDrawVert *verts = NULL; if ( RB_GLSLPrepareInteractionVertexCache( surf, verts ) ) { verts->xyz; } }",
        "cast-wrapped direct member": "void f() { ((idDrawVert *)vertexCache.Position( h ))->normal; }",
        "cast-wrapped alias member": "void f() { void *v = vertexCache.Position( h ); ((idDrawVert *)v)->normal; }",
        "positive boolean alias": "void f() { void *v = vertexCache.Position( h ); if ( v ) {} }",
        "parenthesized alias nullptr": "void f() { void *v = vertexCache.Position( h ); if ( ( v ) == nullptr ) {} }",
        "named boolean alias cast": "void f() { void *v = vertexCache.Position( h ); if ( static_cast<bool>( v ) ) {} }",
        "functional boolean alias cast": "void f() { void *v = vertexCache.Position( h ); if ( bool( v ) ) {} }",
    }
    for context, source in unsafe_sources.items():
        try:
            audit_renderer_source(synthetic_path, source)
        except AssertionError:
            continue
        raise AssertionError(f"Renderer Position() analyzer accepted unsafe synthetic case: {context}")

    safe_source = """
        // A comment mentioning vertexCache.Position( is not a consumer.
        void f() {
            void *offset = vertexCache.Position( h );
            glVertexPointer( 3, GL_FLOAT, stride, offset );
            glTexCoordPointer( 2, GL_FLOAT, stride, vertexCache.Position( h ) );
            glNormalPointer( GL_FLOAT, stride, RB_DrawVertAttributePointer( offset, normalOffset ) );
        }
    """
    if audit_renderer_source(synthetic_path, safe_source) != 2:
        raise AssertionError("Renderer Position() analyzer failed its legal offset-zero self-check")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="surrogateescape")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def main() -> None:
    meson = read("meson.build")
    md4 = read("src/idlib/hashing/MD4.cpp")
    crc32 = read("src/idlib/hashing/CRC32.cpp")
    honeyman = read("src/idlib/hashing/Honeyman.cpp")
    math = read("src/idlib/math/Math.h")
    str_cpp = read("src/idlib/Str.cpp")
    trace_model = read("src/idlib/geometry/TraceModel.cpp")
    random = read("src/idlib/math/Random.h")
    simd = read("src/idlib/math/Simd.cpp")
    simd_sse2 = read("src/idlib/math/Simd_SSE2.h")
    simd_generic = read("src/idlib/math/Simd_generic.cpp")
    vector = read("src/idlib/math/Vector.h")
    vector_set = read("src/idlib/containers/VectorSet.h")
    token = read("src/idlib/Token.h")
    lexer = read("src/idlib/Lexer.cpp")
    parser = read("src/idlib/Parser.cpp")
    bit_msg = read("src/idlib/BitMsg.cpp")
    common_cpp = read("src/framework/Common.cpp")
    console_cpp = read("src/framework/Console.cpp")
    file_system = read("src/framework/FileSystem.cpp")
    session = read("src/framework/Session.cpp")
    async_client = read("src/framework/async/AsyncClient.cpp")
    async_server = read("src/framework/async/AsyncServer.cpp")
    decl_mat_type = read("src/framework/declMatType.h")
    decl_manager = read("src/framework/DeclManager.cpp")
    collision_local = read("src/cm/CollisionModel_local.h")
    collision_model = read("src/cm/CollisionModel.cpp")
    collision_load = read("src/cm/CollisionModel_load.cpp")
    collision_contents = read("src/cm/CollisionModel_contents.cpp")
    collision_translate = read("src/cm/CollisionModel_translate.cpp")
    compressor = read("src/framework/Compressor.cpp")
    msg_channel = read("src/framework/async/MsgChannel.cpp")
    renderer_light = read("src/renderer/tr_light.cpp")
    renderer_local = read("src/renderer/tr_local.h")
    renderer_vertex_cache = read("src/renderer/VertexCache.h")
    renderer_draw_arb2 = read("src/renderer/draw_arb2.cpp")
    renderer_main = read("src/renderer/tr_main.cpp")
    renderer_trisurf = read("src/renderer/tr_trisurf.cpp")
    render_world = read("src/renderer/RenderWorld.cpp")
    image_files = read("src/renderer/Image_files.cpp")
    bse_manager = read("src/bse/BSE_Manager.cpp")
    model_h = read("src/renderer/Model.h")
    model_cpp = read("src/renderer/Model.cpp")
    model_ma = read("src/renderer/Model_ma.cpp")
    model_lwo = read("src/renderer/Model_lwo.cpp")
    render_system_init = read("src/renderer/RenderSystem_init.cpp")
    mapfile = read("src/idlib/mapfile.cpp")
    dict_cpp = read("src/idlib/Dict.cpp")
    lib = read("src/idlib/Lib.cpp")
    swap = read("src/idlib/Swap.h")
    aas_file = read("src/aas/AASFile.cpp")
    cvar_system = read("src/framework/CVarSystem.cpp")
    linux_input = read("src/sys/linux/input.cpp")
    dmap = read("src/tools/compilers/dmap/dmap.cpp")

    require(
        meson,
        "linux_cross_dso_sanitizer_cpp_args += ['-fno-sanitize=vptr']",
        "Linux engine/game cross-DSO sanitizer boundary",
    )
    require(
        meson,
        "engine_cpp_args = shared_cpp_args + common_header_cpp_args + ['-D__DOOM_DLL__'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux engine cross-DSO sanitizer scope",
    )
    require(
        meson,
        "game_common_cpp_args = shared_cpp_args + common_header_cpp_args + ['-DGAME_DLL'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux game cross-DSO sanitizer scope",
    )
    require(
        meson,
        "bse_cpp_args = shared_cpp_args + common_header_cpp_args",
        "BSE keeps full UBSan vptr coverage",
    )
    reject(
        meson,
        "shared_cpp_args += ['-fno-sanitize=vptr']",
        "over-broad UBSan vptr exclusion",
    )
    require(meson, "dedicated_engine_cpp_args = engine_cpp_args", "dedicated sanitizer inheritance")
    require(
        meson,
        "Keep AddressSanitizer and every other requested UBSan check enabled.",
        "Linux game-module sanitizer scope",
    )
    require(
        aas_file,
        'WriteFloatString( "\\t%d ( %d %d %d %d %d %d %d %d ) %d {\\n"',
        "complete AAS area writer arguments",
    )
    reject(
        aas_file,
        'WriteFloatString( "\\t%d ( %d %d %d %d %d %d %d %d %d %d ) %d {\\n"',
        "AAS area writer missing format arguments",
    )
    require(cvar_system, 'common->Printf( "%s", string.c_str() );', "literal cvar-list output format")
    reject(cvar_system, "common->Printf( string );", "dynamic cvar-list output format")

    for source, safe_call, unsafe_call, context in (
        (
            console_cpp,
            'SCR_DrawTextRightAlign( y, "%s", msg.c_str() );',
            'SCR_DrawTextRightAlign( y, msg.c_str() );',
            "async statistics console text",
        ),
        (
            file_system,
            'common->Printf( "%s", status.c_str() );',
            "common->Printf( status.c_str() );",
            "filesystem search-path status",
        ),
        (
            session,
            'common->Printf( "%s", message.c_str() );',
            "common->Printf( message );",
            "timedemo result text",
        ),
        (
            async_client,
            'common->Printf( "%s", message.c_str() );',
            "common->Printf( message );",
            "localized client download error text",
        ),
        (
            async_server,
            'common->Printf( "%s\\n", msg.c_str() );',
            'common->Printf( va( "%s\\n", msg.c_str() ) );',
            "server async statistics text",
        ),
        (
            simd,
            'idLib::common->Printf( "%s", string );',
            "idLib::common->Printf( string );",
            "SIMD benchmark label",
        ),
    ):
        require(source, safe_call, f"literal format for {context}")
        reject(source, unsafe_call, f"dynamic format for {context}")

    require(
        bit_msg,
        'FatalError( "idBitMsg: overflow without allowOverflow set: %d > %d*8",',
        "literal BitMsg overflow format",
    )
    reject(bit_msg, 'FatalError( va( "idBitMsg: overflow', "preformatted BitMsg overflow format")
    require(parser, 'scriptstack->Error( "%s", text );', "literal parser error forwarding format")
    require(parser, 'scriptstack->Warning( "%s", text );', "literal parser warning forwarding format")
    reject(parser, "scriptstack->Error( text );", "dynamic parser error forwarding format")
    reject(parser, "scriptstack->Warning( text );", "dynamic parser warning forwarding format")
    for safe_call in (
        'MA_VERBOSE(("MESH %s - parent %s\\n", header.name, header.parent));',
        'MA_VERBOSE(("\\tverts:%d\\n", maGlobal.currentObject->mesh.numVertexes));',
        'MA_VERBOSE(("\\tfaces:%d\\n", maGlobal.currentObject->mesh.numFaces));',
    ):
        require(model_ma, safe_call, "literal Maya verbose format")
    reject(model_ma, "MA_VERBOSE((va(", "preformatted Maya verbose format")
    for string_id in ("#str_41106", "#str_06780"):
        require(
            render_system_init,
            f'common->Error( "%s", common->GetLanguageDict()->GetString( "{string_id}" ) );',
            f"literal localized renderer error format for {string_id}",
        )
        reject(
            render_system_init,
            f'common->Error( common->GetLanguageDict()->GetString( "{string_id}" ) );',
            f"dynamic localized renderer error format for {string_id}",
        )

    require(md4, "typedef uint32_t UINT4;", "MD4 word type")
    require(md4, 'static_assert( sizeof( UINT4 ) == 4, "MD4 requires 32-bit words" );', "MD4 word contract")
    reject(md4, "typedef unsigned long int UINT4;", "LP64-wide MD4 word")

    require(
        str_cpp,
        "const unsigned char character = static_cast<unsigned char>( data[i] );",
        "unsigned filename character-table index",
    )
    require(str_cpp, "upperCaseCharacter[character]", "bounded filename character-table lookup")
    require(str_cpp, "isdigit(character)", "defined ctype filename lookup")
    reject(str_cpp, "upperCaseCharacter[data[i]]", "signed filename character-table index")
    require(str_cpp, "int length = temp.Length();", "quote-strip length guard")
    require(str_cpp, "memmove(string, string + 1, length);", "overlap-safe leading quote removal")
    require(str_cpp, "length > 0 && string[length - 1] == '\\\"'", "empty-safe trailing quote check")
    reject(str_cpp, "strcpy(string, string + 1);", "overlapping quote-strip copy")
    reject(str_cpp, "string[strlen(string) - 1]", "empty quote-strip index")
    require(
        trace_model,
        "if ( numSilEdges == 0 ) {\n\t\treturn 0;\n\t}",
        "empty silhouette edge list",
    )

    for source, word_type, assertion, context in (
        (crc32, "crc32Word_t", "CRC-32 requires a 32-bit word", "CRC-32"),
        (honeyman, "honeymanWord_t", "Honeyman checksum requires a 32-bit word", "Honeyman"),
    ):
        require(source, f"typedef uint32_t {word_type};", f"fixed-width {context} word")
        require(source, f'static_assert( sizeof( {word_type} ) == 4, "{assertion}" );', f"fixed-width {context} contract")
        require(source, f"static const {word_type} crctable[256]", f"fixed-width {context} table")
        require(source, f"static_cast<{word_type}>( crcvalue )", f"32-bit {context} public-state truncation")
        reject(source, "static unsigned long crctable[256]", f"LP64-wide {context} table")

    for helper in ("idMath_FloatBits", "idMath_FloatFromBits", "idMath_FloatXorBits"):
        require(math, helper, "alias-safe float-bit helpers")
    reject(math, "reinterpret_cast<int *>", "strict-aliasing float-bit access")
    require(random, "int\t\t\t\t\tseed;", "legacy signed idRandom save/network state")
    require(random, "unsigned int\t\t\tseed;", "32-bit idRandom2 state")
    require(random, "const unsigned int nextSeed = 69069u * static_cast<unsigned int>( seed ) + 1u;", "32-bit idRandom wraparound")
    require(random, "? -1 - static_cast<int>( ~nextSeed )", "representable signed idRandom state mapping")
    require(random, "return static_cast<int>( nextSeed & static_cast<unsigned int>( idRandom::MAX_RAND ) );", "unsigned idRandom output mask")
    reject(random, "seed = 69069 * seed + 1;", "signed-overflow idRandom state update")
    require(random, "seed = 1664525u * seed + 1013904223u;", "32-bit idRandom2 wraparound")
    reject(random, "unsigned long\t\t\tseed;", "LP64-wide idRandom2 state")
    require(simd_generic, "signBit = idMath_FloatBits( area ) & ( 1u << 31 );", "generic tangent sign extraction")
    if simd_generic.count("if ( count <= 0 ) {\n\t\treturn;\n\t}") != 3:
        raise AssertionError("SIMD Memcpy, Memset, and Zero16 must accept empty null-backed ranges")
    require(vector, "idMath_FloatBits( x ) | idMath_FloatBits( y ) | idMath_FloatBits( z )", "idVec3 zero test")
    initialized_vector_hash = "boxHashSize = 16;\n\thash.Clear( idMath::IPow( boxHashSize, dimension ), 128 );"
    if vector_set.count(initialized_vector_hash) != 2:
        raise AssertionError("Both default vector hash containers must initialize boxHashSize before using it")
    reject(
        vector_set,
        "hash.Clear( idMath::IPow( boxHashSize, dimension ), 128 );\n\tboxHashSize = 16;",
        "uninitialized default vector hash size",
    )
    require(simd_sse2, "defined( __x86_64__ )", "x86-only SSE2 declaration guard")
    require(simd, "#if defined( ID_SIMD_SSE2_AVAILABLE )", "guarded SSE2 processor selection")

    require(token, "unsigned int\tintvalue;", "32-bit binary token storage")
    reject(token, "unsigned long\tintvalue;", "LP64-wide binary token storage")
    for expected in (
        "unsigned int val = static_cast<unsigned int>( tok->GetUnsignedLongValue() );",
        "case BTT_SUBTYPE_INT: {",
        "const int signedValue = static_cast<int>( token->intvalue );",
        'idStr::snPrintf( buffer, buffersize, "%d", signedValue );',
        'idStr::snPrintf( buffer, buffersize, "%u", token->intvalue );',
        "signed char byteVal = static_cast<signed char>( val );",
        "signed char byteVal = static_cast<signed char>( intval );",
        "TextCompiler::WriteValue<signed char>(byteVal, mBinaryFile, swapBytes);",
        "token->intvalue = ReadValue<signed char>(OBJ);",
        "token->floatvalue = ReadValue<signed char>(OBJ);",
    ):
        require(lexer, expected, "32-bit binary token serialization")
    for legacy in (
        "assert(sizeof(long) == sizeof(int));",
        '"%ld"',
        '"%lu"',
        "char byteVal = (char)val;",
        "char byteVal = (char)intval;",
        "TextCompiler::WriteValue<char>(byteVal, mBinaryFile, swapBytes);",
        "token->intvalue = ReadValue<char>(OBJ);",
        "token->floatvalue = ReadValue<char>(OBJ);",
    ):
        reject(lexer, legacy, "LP64-unsafe binary token serialization")

    if parser.count('sprintf( buf, "%ld", abs( value ) );') != 2:
        raise AssertionError("integer parser evaluation must format both signed-long results portably")
    reject(parser, 'sprintf(buf, "%d", abs(value));', "LP64-unsafe parser evaluation")
    reject(parser, 'sprintf( buf, "%d", abs( value ) );', "LP64-unsafe dollar parser evaluation")

    require(
        common_cpp,
        'truncated to %zu characters\\n", strlen(msg)-1',
        "size_t-safe common truncation diagnostic",
    )
    reject(
        common_cpp,
        'truncated to %d characters\\n", strlen(msg)-1',
        "LP64-unsafe common truncation diagnostic",
    )
    require(
        linux_input,
        "XLookupString buffer '%s' (%zu)\\n\", buf, strlen(buf)",
        "size_t-safe Linux input diagnostic",
    )
    reject(
        linux_input,
        "XLookupString buffer '%s' (%d)\\n\", buf, strlen(buf)",
        "LP64-unsafe Linux input diagnostic",
    )
    for path_name, extension in (("regionPath", ".reg"), ("leakPath", ".lin")):
        require(dmap, f"idStr {path_name} = dmapGlobals.mapFileBase;", f"dynamic dmap {extension} path")
        require(dmap, f'{path_name} += "{extension}";', f"bounded dmap {extension} suffix")
        require(dmap, f"fileSystem->RemoveFile( {path_name} );", f"dmap {extension} cleanup")
    reject(dmap, 'sprintf( path, "%s.reg"', "fixed-buffer dmap region path")
    reject(dmap, 'sprintf( path, "%s.lin"', "fixed-buffer dmap leak path")

    require(decl_mat_type, "memset( mTint, 0, sizeof( mTint ) );", "four-byte material tint initialization")
    require(decl_mat_type, "memcpy( mTint, tint, sizeof( mTint ) );", "four-byte material tint copy")
    reject(decl_mat_type, "*( ulong *)mTint", "LP64-wide material tint access")
    require(decl_manager, "uint32_t\t\t\t\tbits[8];", "32-bit Huffman storage")
    require(decl_manager, "1u << ( code.numBits & 31 )", "defined Huffman high-bit shift")

    require(collision_local, "unsigned int\t\t\tside;", "32-bit collision sidedness mask")
    require(collision_local, "unsigned int\t\t\tsideSet;", "32-bit collision sidedness cache mask")
    reject(collision_local, "unsigned long\t\t\tside", "LP64-wide collision sidedness mask")
    for source in (collision_contents, collision_translate):
        reject(source, "(1<<bitNum)", "signed collision high-bit shift")
        reject(source, "(1 << bitNum)", "signed collision high-bit shift")
    require(collision_contents, "(1u<<bitNum)", "unsigned collision contents shift")
    require(collision_translate, "(1u << bitNum)", "unsigned collision translation shift")
    for source, context in (
        (collision_model, "collision-model instance memory report"),
        (collision_load, "collision-model manager memory report"),
    ):
        for label in ("vertices", "edges", "nodes", "polygon refs", "brush refs"):
            require(source, f"{label} (%zu KB)", f"size_t-safe {context}")
            reject(source, f"{label} (%i KB)", f"LP64-unsafe {context}")

    require(compressor, "memcpy( &word1, p1, sizeof( word1 ) );", "unaligned bitstream comparison")
    require(compressor, "memcpy( &word2, p2, sizeof( word2 ) );", "unaligned bitstream comparison")
    reject(compressor, "*(const int *)p1", "unaligned bitstream integer access")
    require(msg_channel, "#define\tFRAGMENT_BIT\t\t\t(1u<<31)", "defined unsigned network fragment flag")
    require(msg_channel, "static_cast<unsigned int>( outgoingSequence ) | FRAGMENT_BIT", "unsigned fragment flag serialization")
    require(msg_channel, "const unsigned int sequenceBits = static_cast<unsigned int>( sequence );", "unsigned fragment flag parsing")
    reject(msg_channel, "#define\tFRAGMENT_BIT\t\t\t(1<<31)", "undefined signed network high-bit shift")
    require(renderer_light, "memcpy( &word, bytes + i * sizeof( word ), sizeof( word ) );", "unaligned joint hash access")
    reject(renderer_light, "reinterpret_cast<const unsigned long long *>( joints )", "unaligned joint hash access")
    require(
        renderer_light,
        "if ( count > 0 ) {\n\t\t\tmemcpy( tr.viewDef->drawSurfs, old, count );\n\t\t}",
        "empty draw-surface list growth",
    )
    require(renderer_local, "alignas(16) byte base[4];", "16-byte frame allocator payload")
    require(
        renderer_local,
        'static_assert( offsetof( frameMemoryBlock_t, base ) % 16 == 0, "frame allocator payload must remain 16-byte aligned" );',
        "compile-time frame allocator alignment contract",
    )
    reject(renderer_local, "int\t\tpoop;", "32-bit-only manual frame allocator padding")
    require(renderer_main, "buf = block->base + block->used;", "aligned frame allocation base")
    if renderer_main.count("Mem_Alloc16( size + sizeof( *block ) )") != 2:
        raise AssertionError("Frame allocator blocks must use exactly two aligned allocations")
    require(renderer_main, "Mem_Free16( block );", "paired aligned frame block release")
    reject(renderer_main, "Mem_Alloc( size + sizeof( *block ) )", "unaligned frame block allocation")
    reject(renderer_main, "Mem_Free( block );", "mismatched frame block release")
    require(renderer_draw_arb2, "char\t*start = NULL, *end;", "initialized ARB program start")
    require(
        renderer_vertex_cache,
        "ID_INLINE void *RB_DrawVertAttributePointer( const void *base, const size_t byteOffset ) {",
        "shared VBO attribute-offset helper",
    )
    require(
        renderer_vertex_cache,
        "reinterpret_cast<uintptr_t>( base ) + byteOffset",
        "integer-space VBO attribute offset",
    )
    self_check_renderer_vertex_cache_audit()
    audit_renderer_vertex_cache_consumers()
    for guarded_copy, context in (
        (
            "if ( tri->numDupVerts > 0 ) {\n\t\tmemcpy( tri->dupVerts, tempDupVerts, tri->numDupVerts * 2 * sizeof( tri->dupVerts[0] ) );",
            "empty duplicate-vertex list",
        ),
        (
            "if ( numSilEdges > 0 ) {\n\t\tmemcpy( tri->silEdges, silEdges, numSilEdges * sizeof( tri->silEdges[0] ) );",
            "empty silhouette-edge list",
        ),
        (
            "if ( tri->numVerts > 0 ) {\n\t\t\tmemcpy( newTri->verts + totalVerts, tri->verts, tri->numVerts * sizeof( *tri->verts ) );",
            "empty merged surface vertex list",
        ),
        (
            "if ( tri.numVerts > 0 ) {\n\t\tSIMDProcessor->Memcpy( tri.verts, verts, tri.numVerts * sizeof( tri.verts[0] ) );",
            "empty deform-info vertex list",
        ),
    ):
        require(renderer_trisurf, guarded_copy, context)
    require(renderer_trisurf, '"%6zu kB in %d triangle surfaces\\n"', "size_t-safe triangle surface memory report")
    require(renderer_trisurf, '"%6zu kB total triangle memory\\n"', "size_t-safe total triangle memory report")
    reject(renderer_trisurf, '"%6d kB total triangle memory\\n"', "LP64-unsafe total triangle memory report")
    require(render_world, '"%i interaction take %zu bytes\\n"', "size_t-safe interaction memory report")
    reject(render_world, '"%i interaction take %i bytes\\n"', "LP64-unsafe interaction memory report")
    require(image_files, "R_ReadTargaUInt16( const byte *data )", "unaligned TGA header read")
    reject(image_files, "LittleShort ( *(short *)buf_p )", "unaligned TGA header read")
    require(
        bse_manager,
        '"bse_active: %i particles: %i traces: %i texels: %g\\n"',
        "type-correct BSE texel statistic format",
    )
    reject(
        bse_manager,
        '"bse_active: %i particles: %i traces: %i texels: %i\\n"',
        "integer format for floating BSE texel statistic",
    )
    require(model_h, "virtual\t\t\t\t\t\t~idRenderModel( void );", "polymorphic render-model destruction")
    reject(model_h, "//virtual\t\t\t\t\t\t~idRenderModel", "disabled render-model base destructor")
    require(model_h, "enum jointHandle_t : int {", "fixed-width integer joint-index handle")
    require(model_h, "INVALID_JOINT = -1", "invalid joint sentinel")
    reject(model_h, "typedef enum {\n\tINVALID_JOINT", "non-fixed enum used for integer handles")
    require(model_cpp, "idRenderModel::~idRenderModel()", "render-model base destructor definition")
    require(model_lwo, "lwKey *key = NULL;", "initialized LWO envelope key state")
    require(model_lwo, "lwTexture *tex = NULL;", "initialized legacy LWO texture state")
    require(model_lwo, "lwPlugin *shdr = NULL;", "initialized legacy LWO shader state")
    require(model_lwo, "if ( !s ) return NULL;", "legacy LWO texture source validation")
    require(model_lwo, "Mem_Free( s );\n      return NULL;", "legacy LWO texture allocation cleanup")
    surface5_start = model_lwo.index("lwSurface *lwGetSurface5(")
    surface5_end = model_lwo.index("int lwGetPolygons5(", surface5_start)
    surface5 = model_lwo[surface5_start:surface5_end]
    if surface5.count("if ( !tex ) goto Fail;") != 21:
        raise AssertionError("Every legacy LWO texture-dependent subchunk must fail closed")
    require(
        model_lwo,
        "case ID_SDAT:\n            if ( !shdr ) goto Fail;",
        "legacy LWO shader-data ordering guard",
    )
    require(
        model_lwo,
        "case ID_TFLG:\n            if ( !tex ) goto Fail;\n            flags = getU2( fp );\n            i = 0;",
        "legacy LWO texture-axis default",
    )
    require(mapfile, "return idMath_FloatBits( f );", "alias-safe map float checksum")
    require(dict_cpp, 'Printf( "%5zu KB in %d keys\\n"', "size_t-safe dictionary memory reporting")
    require(dict_cpp, 'Printf( "%5zu KB in %d values\\n"', "size_t-safe dictionary value reporting")
    reject(dict_cpp, 'Printf( "%5d KB in %d keys\\n"', "LP64-unsafe dictionary memory reporting")
    require(lib, "memcpy( &swapvalue, swaptest, sizeof( swapvalue ) );", "alignment-safe endian probe")
    reject(lib, "*(short *)swaptest", "unaligned endian probe")
    require(swap, "static constexpr uint32_t idSwapBig32( const uint32_t value )", "single-evaluation 32-bit byte swap")
    require(swap, "static constexpr uint16_t idSwapBig16( const uint16_t value )", "single-evaluation 16-bit byte swap")
    require(swap, "return static_cast<uint16_t>( ( value >> 8 ) | ( value << 8 ) );", "16-bit byte-swap truncation")
    require(swap, 'static_assert( idSwapBig32( 0x12345678u ) == 0x78563412u, "BIG32 must swap all four bytes" );', "compile-time BIG32 value contract")
    require(swap, 'static_assert( idSwapBig16( 0x1234u ) == 0x3412u, "BIG16 must swap and truncate both bytes" );', "compile-time BIG16 value contract")
    require(swap, "#define BIG32(v) idSwapBig32( static_cast<uint32_t>( v ) )", "fixed-width BIG32 helper")
    require(swap, "#define BIG16(v) idSwapBig16( static_cast<uint16_t>( v ) )", "fixed-width BIG16 helper")
    reject(swap, "((uint32)(v))", "undefined BIG32 alias")
    reject(swap, "((uint16)(v))", "undefined BIG16 alias")

    print("linux_arm64_source_portability: ok")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Execute the production multiplayer lag-compensation state machine.

tools/tests/native/MpLagCompensationFixture.cpp supplies a mock server - players walking
through a rewind history, a network system reporting each client's prediction lead and
round trip, and user info carrying the advertised remote extrapolation - and the
history, estimate, advertisement and rewind/restore functions are spliced in verbatim
from src/mpgame/Game_local.cpp.

The fixture proves the coupled contract MP-07 asks for: a client in legacy prediction
is rewound by its whole displayed pose age; a client that extrapolates remote players
forward has that extrapolation taken off, so the two corrections never stack; every
client-authored term is bounded by what the server measured; and the history is
invalidated across teleports, respawns, slot reuse and map loads.

Single-reversal controls then re-run the fixture against mutated production source and
require each one to fail, so the fixture cannot quietly stop checking what it claims.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

FUNCTIONS = (
    "void idGameLocal::ResetMPLagCompensationHistory(",
    "void idGameLocal::CaptureMPLagCompensationFrame(",
    "void idGameLocal::InvalidateMPLagCompensationHistory(",
    "bool idGameLocal::SelectMPLagCompensationFrame(",
    "void idGameLocal::EstimateMPLagCompensation(",
    "bool idGameLocal::ComputeMPLagCompensationRewind(",
    "int idGameLocal::AdvertisedRemoteExtrapolationTics(",
    "int idGameLocal::RemoteExtrapolationMS(",
    "void idGameLocal::UpdateAdvertisedRemoteExtrapolation(",
    "bool idGameLocal::BeginMPLagCompensation(",
    "void idGameLocal::EndMPLagCompensation(",
    "bool idGameLocal::ResolveMPLagCompensationHit(",
)

STRUCTS = (
    "struct mpLagCompFrame_t {",
    "struct mpLagCompRestore_t {",
    "struct mpLagCompEstimate_t {",
)

# Each control reverses one agreement; the fixture must fail on every one.
MUTATIONS = (
    (
        "extrapolation ignored (both corrections stack)",
        "return Min( leadMS, AdvertisedRemoteExtrapolationTics( clientNum ) * GetMSec() );",
        "return 0;",
    ),
    (
        "lead no longer capped by the measured round trip",
        "estimate.poseAgeMS = Max( 0, Min( claim, ceiling ) - estimate.extrapolationMS );",
        "estimate.poseAgeMS = Max( 0, claim - estimate.extrapolationMS );",
    ),
    (
        "advertisement parsed without bounds",
        'if ( !idNumericString::ParseUnsignedBounded( userInfo[ clientNum ].GetString( "net_mpRemoteExtrapolation", "0" ), 64, tics ) ) {',
        'tics = atoi( userInfo[ clientNum ].GetString( "net_mpRemoteExtrapolation", "0" ) ); if ( false ) {',
    ),
    (
        "hit target not returned to the present",
        "restoreState[ i ] = restoreState[ --restoreCount ];",
        "return false;",
    ),
    (
        "future stamps accepted after a map load",
        "if ( frame.time > time || frame.time < oldestAcceptableTime ) {",
        "if ( frame.time < oldestAcceptableTime ) {",
    ),
)


def block(source: str, signature: str) -> str:
    """Return one brace-delimited block, from its signature to the matching closing brace."""
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_fixture(game_local: str, header: str) -> str:
    structs = "\n".join(block(header, signature) + ";" for signature in STRUCTS)
    bodies = "\n".join(block(game_local, signature) for signature in FUNCTIONS)
    harness = (ROOT / "tools/tests/native/MpLagCompensationFixture.cpp").read_text(encoding="utf-8")
    return harness.replace("// PRODUCTION_STRUCTS", structs).replace("// PRODUCTION_FUNCTIONS", bodies)


def compile_and_run(source: str, directory: Path, name: str, compiler: str) -> subprocess.CompletedProcess:
    cpp = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
               "-Wno-unused-const-variable", "-Wno-unused-function", f"-I{ROOT}", str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True)
    return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)


def main() -> None:
    game_local = (ROOT / "src/mpgame/Game_local.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/mpgame/Game_local.h").read_text(encoding="utf-8")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the lag compensation fixture")

    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-lag-compensation-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(game_local, header), path, "lagcomp", compiler)
        if result.returncode != 0:
            raise SystemExit(f"mp_lag_compensation_state: FAILED\n{result.stdout}{result.stderr}")

        for index, (label, original, mutated) in enumerate(MUTATIONS):
            if game_local.count(original) != 1:
                raise SystemExit(f"mp_lag_compensation_state: mutation anchor not unique: {label}")
            control = compile_and_run(
                build_fixture(game_local.replace(original, mutated, 1), header), path, f"control{index}", compiler
            )
            if control.returncode == 0:
                raise SystemExit(f"mp_lag_compensation_state: control did not fail: {label}")

    print(f"mp_lag_compensation_state: ok ({len(MUTATIONS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()

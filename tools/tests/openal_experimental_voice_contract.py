#!/usr/bin/env python3
"""Regression checks for the opt-in OpenAL experimental voice path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VOICE_SOURCE = ROOT / "src" / "sound" / "OpenAL" / "AL_SoundVoice.cpp"
SOUND_SYSTEM_SOURCE = ROOT / "src" / "sound" / "snd_system.cpp"
DECL_MANAGER_SOURCE = ROOT / "src" / "framework" / "DeclManager.cpp"


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_order(source: str, first: str, second: str, context: str) -> None:
    first_index = source.find(first)
    second_index = source.find(second)
    if first_index < 0:
        raise AssertionError(f"Missing {first!r} in {context}")
    if second_index < 0:
        raise AssertionError(f"Missing {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def main() -> None:
    voice_source = VOICE_SOURCE.read_text(encoding="utf-8")
    sound_system = SOUND_SYSTEM_SOURCE.read_text(encoding="utf-8")
    decl_manager = DECL_MANAGER_SOURCE.read_text(encoding="utf-8")

    pause_start = voice_source.index("void idSoundVoice_OpenAL::Pause()")
    unpause_start = voice_source.index("void idSoundVoice_OpenAL::UnPause()")
    stop_start = voice_source.index("void idSoundVoice_OpenAL::Stop()")
    pause_block = voice_source[pause_start:unpause_start]
    unpause_block = voice_source[unpause_start:stop_start]
    pause_after_call = pause_block[pause_block.index("alSourcePause( openalSource );") :]
    unpause_after_call = unpause_block[unpause_block.index("alSourcePlay( openalSource );") :]

    require(
        sound_system,
        'idCVar s_openALExperimentalVoices( "s_openALExperimentalVoices", "0", CVAR_ARCHIVE | CVAR_BOOL',
        "canonical experimental voice cvar",
    )
    require(
        sound_system,
        'static idCVar s_openALPostPlanBehavior( "s_openALPostPlanBehavior", "0", CVAR_BOOL',
        "legacy post-plan cvar alias",
    )
    require(
        sound_system,
        "bool Sound_OpenALExperimentalVoicesEnabled()",
        "shared experimental voice gate",
    )
    require(
        sound_system,
        'idLib::Warning( "s_openALPostPlanBehavior is deprecated; migrating to s_openALExperimentalVoices." );',
        "legacy cvar migration warning",
    )
    require(
        sound_system,
        "s_openALExperimentalVoices.SetBool( true );",
        "legacy cvar migration target",
    )
    require(
        sound_system,
        "s_openALPostPlanBehavior.SetBool( false );",
        "legacy cvar migration cleanup",
    )
    require(
        sound_system,
        "s_openALPostPlanBehavior.RemoveFlag( CVAR_ARCHIVE );",
        "legacy cvar archive cleanup",
    )
    require(
        sound_system,
        "s_openALPostPlanBehavior.ClearModified();",
        "legacy cvar modified cleanup",
    )
    require(
        sound_system,
        'idLib::Printf( "testSound: no active sound world for \'%s\'\\n", soundName );',
        "testSound inactive-world diagnostic",
    )
    require(
        sound_system,
        'idLib::Printf( "testSound: failed to start \'%s\'\\n", soundName );',
        "testSound start-failure diagnostic",
    )
    require(
        sound_system,
        'idLib::Printf( "testSound: started \'%s\' on handle %d\\n", soundName, handle );',
        "testSound started diagnostic",
    )
    require(
        sound_system,
        '"OpenAL experimental voice path changed from %d to %d; restarting sound system.\\n"',
        "runtime restart notice",
    )
    require(
        voice_source,
        "extern bool Sound_OpenALExperimentalVoicesEnabled();",
        "voice gate declaration",
    )
    require(
        decl_manager,
        "extern bool Sound_OpenALExperimentalVoicesEnabled();",
        "decl manager gate declaration",
    )
    require(
        decl_manager,
        "type == DECL_SOUND && Sound_OpenALExperimentalVoicesEnabled()",
        "gated sound decl reference marking",
    )
    reject(
        voice_source + decl_manager,
        "s_openALPostPlanBehavior.GetBool()",
        "direct legacy cvar checks outside the shared gate",
    )

    require(pause_block, "alSourcePause( openalSource );", "OpenAL pause call")
    require(unpause_block, "alSourcePlay( openalSource );", "OpenAL play call")
    require(pause_block, "if( CheckALErrors() != AL_NO_ERROR )", "pause error check")
    require(unpause_block, "if( CheckALErrors() != AL_NO_ERROR )", "unpause error check")
    require(pause_block, "FlushSourceBuffers();", "experimental pause cleanup")
    require(unpause_block, "FlushSourceBuffers();", "experimental unpause cleanup")
    require_order(
        pause_after_call,
        "alSourcePause( openalSource );",
        "paused = true;",
        "pause only updates local state after OpenAL accepts the call",
    )
    require_order(
        unpause_after_call,
        "alSourcePlay( openalSource );",
        "paused = false;",
        "unpause only updates local state after OpenAL accepts the call",
    )


if __name__ == "__main__":
    main()

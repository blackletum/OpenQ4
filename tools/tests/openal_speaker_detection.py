#!/usr/bin/env python3
"""Regression checks for OpenAL speaker setup detection."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "sound" / "OpenAL" / "AL_SoundHardware.cpp"


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


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
    source = SOURCE.read_text(encoding="utf-8")

    require(source, "static int openQ4_GetOpenALSpeakerCount( ALCdevice* device )", "OpenAL speaker resolver")
    require(source, "static int openQ4_GetEffectiveSpeakerCount( ALCdevice* device )", "effective speaker resolver")
    require(source, "static bool openQ4_ActiveDeviceIsSystemDefault( ALCdevice* device )", "default-device guard")
    require(source, "static void openQ4_SetResolvedSpeakerCount", "throttled cvar correction")
    require(
        source,
        "static bool openQ4_ClampRequestedSpeakerCountForSystemDefault( const char* targetDeviceName, const int requestedSpeakerCount )",
        "pre-open speaker clamp",
    )
    require(
        source,
        "static const ALCint* openQ4_BuildDeviceModeContextAttributes",
        "combined OpenAL device mode attributes",
    )
    require(source, "openQ4_AppendHrtfDeviceModeAttributes", "HRTF carried with device mode attributes")
    require(source, "if( !openQ4_QueryOutputMode( device, outputMode ) )", "query failure handling")
    require(source, "return OPENQ4_OPENAL_SPEAKERS_STEREO;", "stereo fallback")
    require(source, "audioClient->GetMixFormat( &mixFormat )", "Windows endpoint format query")
    require(source, "enumerator->GetDefaultAudioEndpoint( eRender, eMultimedia, &device )", "Windows media-role endpoint")
    require(source, "openQ4_SpeakerCountForWindowsMixFormat", "Windows endpoint format classifier")
    require(source, "openQ4_WindowsChannelMaskHasSurroundLayout", "Windows channel-mask guard")
    require(source, "mixFormat->nChannels < OPENQ4_OPENAL_SPEAKERS_SURROUND", "Windows endpoint channel clamp")
    require(source, "WAVE_FORMAT_EXTENSIBLE", "Windows extensible mix-format handling")
    require(source, "idSoundHardware_OpenAL::GetActivePlaybackDeviceName( device )", "active OpenAL device identity")
    require(source, "openQ4_PlaybackDeviceNameIsSystemDefault( targetDeviceName )", "target default-device clamp")
    require(
        source,
        "openalSpeakerCount != OPENQ4_OPENAL_SPEAKERS_SURROUND || !openQ4_ActiveDeviceIsSystemDefault( device )",
        "effective default-device clamp",
    )
    require(source, "platformSpeakerCount < OPENQ4_OPENAL_SPEAKERS_SURROUND", "platform stereo guard")
    require(source, "openQ4_ReportPlatformSpeakerSetup();", "platform speaker diagnostics")
    require(source, "openQ4_SetResolvedSpeakerCount( requestedSpeakerCount, effectiveSpeakerCount );", "cvar correction")
    require(source, "openQ4_SetResolvedSpeakerCount( requestedSpeakerCount, platformSpeakerCount );", "pre-open cvar correction")
    require(source, "openQ4_InvalidateWindowsDefaultPlaybackSpeakerCache();", "platform speaker cache invalidation")
    require(
        source,
        "openQ4_ClampRequestedSpeakerCountForSystemDefault( NULL, requestedSpeakerCount );",
        "runtime cvar correction",
    )
    require(
        source,
        "openedSpeakerCount = openQ4_GetEffectiveSpeakerCount( openalDevice );",
        "captured effective speaker count",
    )
    require(source, "qalcReopenDeviceSOFT( openalDevice, reopenDeviceName, requestedReopenAttributes )", "reopen attributes")
    require(source, "OpenAL in-place device reopen rejected requested device mode", "reopen device-mode retry")
    require(source, "OpenAL active speaker setup changed", "active speaker change monitoring")

    require_order(
        source,
        "openQ4_ApplyEffectiveSpeakerCount( openalDevice, requestedSpeakerCount );",
        "CaptureOpenedDeviceState( requestedDeviceName.c_str() );",
        "startup captures resolved speaker state",
    )
    require_order(
        source,
        "openQ4_ClampRequestedSpeakerCountForSystemDefault( openedDeviceRequestName, requestedSpeakerCount );",
        "const ALCint* requestedContextAttributes = openQ4_BuildDeviceModeContextAttributes( openalDevice, openalContextAttributes );",
        "startup clamps before context attributes",
    )
    require_order(
        source,
        "openQ4_ClampRequestedSpeakerCountForSystemDefault( reopenDeviceName, requestedSpeakerCount );",
        "const ALCint* requestedReopenAttributes = openQ4_BuildDeviceModeContextAttributes( openalDevice, reopenAttributes );",
        "reopen clamps before reopen attributes",
    )
    require_order(
        source,
        "openQ4_ClampRequestedSpeakerCountForSystemDefault( NULL, requestedSpeakerCount );",
        "if( speakerCount != openedSpeakerCount )",
        "monitoring clamps late cvar changes before restart decision",
    )


if __name__ == "__main__":
    main()

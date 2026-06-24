/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013 Robert Beckebans
Copyright (c) 2010 by Chris Robinson <chris.kcat@gmail.com> (OpenAL Info Utility)

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../snd_local.h"
#if defined(USE_DOOMCLASSIC)
	#include "../../../doomclassic/doom/i_sound.h"
#endif

#if defined( USE_SDL3 )
	#include <SDL3/SDL.h>
#endif

idCVar s_showLevelMeter( "s_showLevelMeter", "0", CVAR_BOOL | CVAR_ARCHIVE, "Show VU meter" );
idCVar s_meterTopTime( "s_meterTopTime", "1000", CVAR_INTEGER | CVAR_ARCHIVE, "How long (in milliseconds) peaks are displayed on the VU meter" );
idCVar s_meterPosition( "s_meterPosition", "100 100 20 200", CVAR_ARCHIVE, "VU meter location (x y w h)" );
idCVar s_device( "s_device", "-1", CVAR_INTEGER | CVAR_ARCHIVE, "Which audio device to use (listDevices to list, -1 for default)" );
idCVar s_showPerfData( "s_showPerfData", "0", CVAR_BOOL, "Show sound backend performance data" );
idCVar s_openALResampler( "s_openALResampler", "-2", CVAR_ARCHIVE | CVAR_INTEGER, "OpenAL Soft source resampler: -2 = quality auto, -1 = runtime default, 0+ = advertised resampler index" );
extern idCVar s_useEAXReverb;
extern idCVar s_deviceName;
extern idCVar s_openALHRTF;
extern idCVar s_numberOfSpeakers;

static const char* OPENQ4_AUDIO_DEVICE_DEFAULT_CHOICE = "__OPENQ4_DEFAULT_AUDIO_DEVICE__";
static const int OPENQ4_OPENAL_HRTF_AUTO = 0;
static const int OPENQ4_OPENAL_HRTF_OFF = 1;
static const int OPENQ4_OPENAL_HRTF_ON = 2;
static const int OPENQ4_OPENAL_SPEAKERS_STEREO = 2;
static const int OPENQ4_OPENAL_SPEAKERS_SURROUND = 6;
static const int OPENQ4_OPENAL_RESAMPLER_QUALITY_AUTO = -2;
static const int OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT = -1;

#if defined( AL_EXT_SOURCE_RADIUS ) && defined( AL_SOURCE_RADIUS )
	#define OPENQ4_OPENAL_SOURCE_RADIUS_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_SOURCE_RADIUS_SUPPORTED 0
#endif

#if defined( AL_SOFT_callback_buffer ) && defined( AL_BUFFER_CALLBACK_FUNCTION_SOFT ) && defined( AL_BUFFER_CALLBACK_USER_PARAM_SOFT )
	#define OPENQ4_OPENAL_CALLBACK_BUFFER_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_CALLBACK_BUFFER_SUPPORTED 0
#endif

struct openQ4OpenALDiagnostics_t
{
	int alErrors;
	int alcErrors;
	int sampleUploadAttempts;
	int sampleUploadRetries;
	int sampleUploadContextMisses;
	int sampleUploadFailures;
	int queueFallbackEntries;
	int queueFallbackRefusals;
	int queueBuffersSubmitted;
	int queueBuffersRefilled;
	int queueUnderrunRestarts;
	int deviceEvents;
	int deviceReopenAttempts;
	int deviceReopenSuccesses;
	int deviceReopenFailures;
	int soundRestarts;
	int deferredUpdateErrors;
	int efxFilterErrors;
};

static openQ4OpenALDiagnostics_t openQ4_openALDiagnostics;

#if defined( AL_SOFT_source_resampler ) && defined( AL_NUM_RESAMPLERS_SOFT ) && defined( AL_DEFAULT_RESAMPLER_SOFT ) && defined( AL_SOURCE_RESAMPLER_SOFT ) && defined( AL_RESAMPLER_NAME_SOFT )
	#define OPENQ4_OPENAL_SOURCE_RESAMPLER_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_SOURCE_RESAMPLER_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_SOURCE_RESAMPLER_SUPPORTED
static LPALGETSTRINGISOFT qalGetStringiSOFT = NULL;
#endif

#if OPENQ4_OPENAL_CALLBACK_BUFFER_SUPPORTED
static LPALBUFFERCALLBACKSOFT qalBufferCallbackSOFT = NULL;

static ALsizei AL_APIENTRY openQ4_OpenALCallbackBufferProbe( ALvoid* userptr, ALvoid* sampledata, ALsizei numbytes ) AL_API_NOEXCEPT
{
	(void)userptr;
	if( sampledata != NULL && numbytes > 0 )
	{
		memset( sampledata, 0, numbytes );
	}
	return numbytes > 0 ? numbytes : 0;
}
#endif

/*
========================
idSoundHardware_OpenAL::CountDiagnosticEvent
========================
*/
void idSoundHardware_OpenAL::CountDiagnosticEvent( openALDiagnosticCounter_t counter, int amount )
{
	if( amount <= 0 )
	{
		return;
	}

	switch( counter )
	{
		case OPENAL_DIAG_AL_ERRORS:
			openQ4_openALDiagnostics.alErrors += amount;
			break;
		case OPENAL_DIAG_ALC_ERRORS:
			openQ4_openALDiagnostics.alcErrors += amount;
			break;
		case OPENAL_DIAG_SAMPLE_UPLOAD_ATTEMPTS:
			openQ4_openALDiagnostics.sampleUploadAttempts += amount;
			break;
		case OPENAL_DIAG_SAMPLE_UPLOAD_RETRIES:
			openQ4_openALDiagnostics.sampleUploadRetries += amount;
			break;
		case OPENAL_DIAG_SAMPLE_UPLOAD_CONTEXT_MISSES:
			openQ4_openALDiagnostics.sampleUploadContextMisses += amount;
			break;
		case OPENAL_DIAG_SAMPLE_UPLOAD_FAILURES:
			openQ4_openALDiagnostics.sampleUploadFailures += amount;
			break;
		case OPENAL_DIAG_QUEUE_FALLBACK_ENTRIES:
			openQ4_openALDiagnostics.queueFallbackEntries += amount;
			break;
		case OPENAL_DIAG_QUEUE_FALLBACK_REFUSALS:
			openQ4_openALDiagnostics.queueFallbackRefusals += amount;
			break;
		case OPENAL_DIAG_QUEUE_BUFFERS_SUBMITTED:
			openQ4_openALDiagnostics.queueBuffersSubmitted += amount;
			break;
		case OPENAL_DIAG_QUEUE_BUFFERS_REFILLED:
			openQ4_openALDiagnostics.queueBuffersRefilled += amount;
			break;
		case OPENAL_DIAG_QUEUE_UNDERRUN_RESTARTS:
			openQ4_openALDiagnostics.queueUnderrunRestarts += amount;
			break;
		case OPENAL_DIAG_DEVICE_EVENTS:
			openQ4_openALDiagnostics.deviceEvents += amount;
			break;
		case OPENAL_DIAG_DEVICE_REOPEN_ATTEMPTS:
			openQ4_openALDiagnostics.deviceReopenAttempts += amount;
			break;
		case OPENAL_DIAG_DEVICE_REOPEN_SUCCESSES:
			openQ4_openALDiagnostics.deviceReopenSuccesses += amount;
			break;
		case OPENAL_DIAG_DEVICE_REOPEN_FAILURES:
			openQ4_openALDiagnostics.deviceReopenFailures += amount;
			break;
		case OPENAL_DIAG_SOUND_RESTARTS:
			openQ4_openALDiagnostics.soundRestarts += amount;
			break;
		case OPENAL_DIAG_DEFERRED_UPDATE_ERRORS:
			openQ4_openALDiagnostics.deferredUpdateErrors += amount;
			break;
		case OPENAL_DIAG_EFX_FILTER_ERRORS:
			openQ4_openALDiagnostics.efxFilterErrors += amount;
			break;
	}
}

/*
========================
idSoundHardware_OpenAL::PrintDiagnosticCounters
========================
*/
void idSoundHardware_OpenAL::PrintDiagnosticCounters()
{
	common->Printf( "OpenAL diag: alErrors %d alcErrors %d uploadAttempts %d uploadRetries %d uploadContextMisses %d uploadFailures %d fallbackEntries %d fallbackRefusals %d\n",
		openQ4_openALDiagnostics.alErrors,
		openQ4_openALDiagnostics.alcErrors,
		openQ4_openALDiagnostics.sampleUploadAttempts,
		openQ4_openALDiagnostics.sampleUploadRetries,
		openQ4_openALDiagnostics.sampleUploadContextMisses,
		openQ4_openALDiagnostics.sampleUploadFailures,
		openQ4_openALDiagnostics.queueFallbackEntries,
		openQ4_openALDiagnostics.queueFallbackRefusals );

	common->Printf( "OpenAL diag: queueBuffers %d queueRefills %d underrunRestarts %d deviceEvents %d reopenAttempts %d reopenSuccesses %d reopenFailures %d soundRestarts %d deferredErrors %d efxFilterErrors %d\n",
		openQ4_openALDiagnostics.queueBuffersSubmitted,
		openQ4_openALDiagnostics.queueBuffersRefilled,
		openQ4_openALDiagnostics.queueUnderrunRestarts,
		openQ4_openALDiagnostics.deviceEvents,
		openQ4_openALDiagnostics.deviceReopenAttempts,
		openQ4_openALDiagnostics.deviceReopenSuccesses,
		openQ4_openALDiagnostics.deviceReopenFailures,
		openQ4_openALDiagnostics.soundRestarts,
		openQ4_openALDiagnostics.deferredUpdateErrors,
		openQ4_openALDiagnostics.efxFilterErrors );
}

static bool openQ4_UseEnumerateAllDevices( ALCdevice* device ) {
#if defined( ALC_ALL_DEVICES_SPECIFIER ) && defined( ALC_DEFAULT_ALL_DEVICES_SPECIFIER )
	return alcIsExtensionPresent( device, "ALC_ENUMERATE_ALL_EXT" ) != AL_FALSE;
#else
	return false;
#endif
}

static ALCenum openQ4_GetPlaybackDevicesToken( ALCdevice* device ) {
#if defined( ALC_ALL_DEVICES_SPECIFIER )
	if( openQ4_UseEnumerateAllDevices( device ) ) {
		return ALC_ALL_DEVICES_SPECIFIER;
	}
#endif
	return ALC_DEVICE_SPECIFIER;
}

static ALCenum openQ4_GetDefaultPlaybackDeviceToken( ALCdevice* device ) {
#if defined( ALC_DEFAULT_ALL_DEVICES_SPECIFIER )
	if( openQ4_UseEnumerateAllDevices( device ) ) {
		return ALC_DEFAULT_ALL_DEVICES_SPECIFIER;
	}
#endif
	return ALC_DEFAULT_DEVICE_SPECIFIER;
}

#if defined( ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT ) && defined( ALC_EVENT_TYPE_DEVICE_ADDED_SOFT ) && defined( ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT ) && defined( ALC_PLAYBACK_DEVICE_SOFT ) && defined( ALC_EVENT_SUPPORTED_SOFT )
	#define OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED 0
#endif

static const int OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED = BIT( 0 );
static const int OPENQ4_OPENAL_DEVICE_EVENT_ADDED = BIT( 1 );
static const int OPENQ4_OPENAL_DEVICE_EVENT_REMOVED = BIT( 2 );
static const int OPENQ4_OPENAL_DEVICE_EVENT_ALL = OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED | OPENQ4_OPENAL_DEVICE_EVENT_ADDED | OPENQ4_OPENAL_DEVICE_EVENT_REMOVED;
static const int OPENQ4_OPENAL_DEVICE_POLL_MSEC = 1000;
static const int OPENQ4_OPENAL_DEVICE_EVENT_SETTLE_MSEC = 500;
static const int OPENQ4_OPENAL_DEVICE_RECOVERY_COOLDOWN_MSEC = 1500;

#if defined( USE_SDL3 )
static SDL_AtomicInt openQ4_PendingOpenALDeviceEvents = { 0 };
#else
static volatile int openQ4_PendingOpenALDeviceEvents = 0;
#endif

static const char* openQ4_DeviceEventFlagName( const int flag )
{
	switch( flag )
	{
		case OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED:
			return "default changed";
		case OPENQ4_OPENAL_DEVICE_EVENT_ADDED:
			return "device added";
		case OPENQ4_OPENAL_DEVICE_EVENT_REMOVED:
			return "device removed";
		default:
			return "unknown";
	}
}

static void openQ4_AppendDeviceEventName( idStr& eventNames, const int flag )
{
	if( eventNames.Length() > 0 )
	{
		eventNames += ", ";
	}
	eventNames += openQ4_DeviceEventFlagName( flag );
}

static void openQ4_AppendDeviceEventNames( idStr& eventNames, const int eventFlags )
{
	const int flags[] = {
		OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED,
		OPENQ4_OPENAL_DEVICE_EVENT_ADDED,
		OPENQ4_OPENAL_DEVICE_EVENT_REMOVED
	};
	for( int i = 0; i < sizeof( flags ) / sizeof( flags[ 0 ] ); ++i )
	{
		if( ( eventFlags & flags[ i ] ) != 0 )
		{
			openQ4_AppendDeviceEventName( eventNames, flags[ i ] );
		}
	}
}

static void openQ4_QueuePendingDeviceEventFlags( const int eventFlags )
{
	if( eventFlags != 0 )
	{
#if defined( USE_SDL3 )
		for( ;; )
		{
			const int currentFlags = SDL_GetAtomicInt( &openQ4_PendingOpenALDeviceEvents );
			const int newFlags = currentFlags | eventFlags;
			if( newFlags == currentFlags || SDL_CompareAndSwapAtomicInt( &openQ4_PendingOpenALDeviceEvents, currentFlags, newFlags ) )
			{
				return;
			}
		}
#else
		openQ4_PendingOpenALDeviceEvents |= eventFlags;
#endif
	}
}

static int openQ4_ConsumePendingDeviceEventFlags()
{
#if defined( USE_SDL3 )
	return SDL_SetAtomicInt( &openQ4_PendingOpenALDeviceEvents, 0 );
#else
	const int eventFlags = openQ4_PendingOpenALDeviceEvents;
	openQ4_PendingOpenALDeviceEvents = 0;
	return eventFlags;
#endif
}

#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
typedef ALCenum ( ALC_APIENTRY *openq4_alcEventIsSupportedSOFT_t )( ALCenum eventType, ALCenum deviceType );
typedef ALCboolean ( ALC_APIENTRY *openq4_alcEventControlSOFT_t )( ALCsizei count, const ALCenum* events, ALCboolean enable );
typedef void ( ALC_APIENTRY *openq4_alcEventCallbackSOFT_t )( ALCEVENTPROCTYPESOFT callback, void* userParam );

static openq4_alcEventIsSupportedSOFT_t qalcEventIsSupportedSOFT = NULL;
static openq4_alcEventControlSOFT_t qalcEventControlSOFT = NULL;
static openq4_alcEventCallbackSOFT_t qalcEventCallbackSOFT = NULL;

static int openQ4_DeviceEventFlagForType( const ALCenum eventType )
{
	switch( eventType )
	{
		case ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT:
			return OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED;
		case ALC_EVENT_TYPE_DEVICE_ADDED_SOFT:
			return OPENQ4_OPENAL_DEVICE_EVENT_ADDED;
		case ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT:
			return OPENQ4_OPENAL_DEVICE_EVENT_REMOVED;
		default:
			return 0;
	}
}

static ALCenum openQ4_DeviceEventTypeForFlag( const int flag )
{
	switch( flag )
	{
		case OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED:
			return ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT;
		case OPENQ4_OPENAL_DEVICE_EVENT_ADDED:
			return ALC_EVENT_TYPE_DEVICE_ADDED_SOFT;
		case OPENQ4_OPENAL_DEVICE_EVENT_REMOVED:
			return ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT;
		default:
			return 0;
	}
}

static void ALC_APIENTRY openQ4_OpenALDeviceEventCallback( ALCenum eventType, ALCenum deviceType, ALCdevice* device, ALCsizei length, const ALCchar* message, void* userParam ) ALC_API_NOEXCEPT
{
	(void)device;
	(void)length;
	(void)message;
	(void)userParam;

	if( deviceType != ALC_PLAYBACK_DEVICE_SOFT )
	{
		return;
	}

	const int flag = openQ4_DeviceEventFlagForType( eventType );
	if( flag != 0 )
	{
		openQ4_QueuePendingDeviceEventFlags( flag );
	}
}

static bool openQ4_LoadSystemEventProcs( ALCdevice* device )
{
	qalcEventIsSupportedSOFT = reinterpret_cast<openq4_alcEventIsSupportedSOFT_t>( alcGetProcAddress( device, "alcEventIsSupportedSOFT" ) );
	qalcEventControlSOFT = reinterpret_cast<openq4_alcEventControlSOFT_t>( alcGetProcAddress( device, "alcEventControlSOFT" ) );
	qalcEventCallbackSOFT = reinterpret_cast<openq4_alcEventCallbackSOFT_t>( alcGetProcAddress( device, "alcEventCallbackSOFT" ) );
	return qalcEventIsSupportedSOFT != NULL && qalcEventControlSOFT != NULL && qalcEventCallbackSOFT != NULL;
}

#endif

#if defined( ALC_SOFT_reopen_device )
	#define OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED
typedef ALCboolean ( ALC_APIENTRY *openq4_alcReopenDeviceSOFT_t )( ALCdevice* device, const ALCchar* deviceName, const ALCint* attribs );
static openq4_alcReopenDeviceSOFT_t qalcReopenDeviceSOFT = NULL;

static bool openQ4_LoadReopenDeviceProc( ALCdevice* device )
{
	qalcReopenDeviceSOFT = reinterpret_cast<openq4_alcReopenDeviceSOFT_t>( alcGetProcAddress( device, "alcReopenDeviceSOFT" ) );
	return qalcReopenDeviceSOFT != NULL;
}
#else
static bool openQ4_LoadReopenDeviceProc( ALCdevice* device )
{
	(void)device;
	return false;
}
#endif

#if defined( AL_DEFERRED_UPDATES_SOFT )
	#define OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED
typedef void ( AL_APIENTRY *openq4_alDeferUpdatesSOFT_t )( void );
typedef void ( AL_APIENTRY *openq4_alProcessUpdatesSOFT_t )( void );

static openq4_alDeferUpdatesSOFT_t qalDeferUpdatesSOFT = NULL;
static openq4_alProcessUpdatesSOFT_t qalProcessUpdatesSOFT = NULL;

static bool openQ4_LoadDeferredUpdateProcs()
{
	qalDeferUpdatesSOFT = reinterpret_cast<openq4_alDeferUpdatesSOFT_t>( alGetProcAddress( "alDeferUpdatesSOFT" ) );
	qalProcessUpdatesSOFT = reinterpret_cast<openq4_alProcessUpdatesSOFT_t>( alGetProcAddress( "alProcessUpdatesSOFT" ) );
	return qalDeferUpdatesSOFT != NULL && qalProcessUpdatesSOFT != NULL;
}
#else
static bool openQ4_LoadDeferredUpdateProcs()
{
	return false;
}
#endif

#if defined( AL_EFFECTSLOT_EFFECT ) && defined( AL_EFFECT_NULL ) && defined( AL_AUXILIARY_SEND_FILTER )
	#define OPENQ4_OPENAL_EFX_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_EFX_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_EFX_SUPPORTED
typedef void ( AL_APIENTRY *openq4_alGenEffects_t )( ALsizei n, ALuint *effects );
typedef void ( AL_APIENTRY *openq4_alDeleteEffects_t )( ALsizei n, const ALuint *effects );
typedef void ( AL_APIENTRY *openq4_alEffecti_t )( ALuint effect, ALenum param, ALint iValue );
typedef void ( AL_APIENTRY *openq4_alGenAuxiliaryEffectSlots_t )( ALsizei n, ALuint *effectslots );
typedef void ( AL_APIENTRY *openq4_alDeleteAuxiliaryEffectSlots_t )( ALsizei n, const ALuint *effectslots );
typedef void ( AL_APIENTRY *openq4_alAuxiliaryEffectSloti_t )( ALuint effectslot, ALenum param, ALint iValue );

static openq4_alGenEffects_t qalGenEffects = NULL;
static openq4_alDeleteEffects_t qalDeleteEffects = NULL;
static openq4_alEffecti_t qalEffecti = NULL;
static openq4_alGenAuxiliaryEffectSlots_t qalGenAuxiliaryEffectSlots = NULL;
static openq4_alDeleteAuxiliaryEffectSlots_t qalDeleteAuxiliaryEffectSlots = NULL;
static openq4_alAuxiliaryEffectSloti_t qalAuxiliaryEffectSloti = NULL;

static bool openQ4_LoadHardwareEfxProcs() {
	static bool initialized = false;
	static bool available = false;

	if ( initialized ) {
		return available;
	}
	initialized = true;

	qalGenEffects = reinterpret_cast<openq4_alGenEffects_t>( alGetProcAddress( "alGenEffects" ) );
	qalDeleteEffects = reinterpret_cast<openq4_alDeleteEffects_t>( alGetProcAddress( "alDeleteEffects" ) );
	qalEffecti = reinterpret_cast<openq4_alEffecti_t>( alGetProcAddress( "alEffecti" ) );
	qalGenAuxiliaryEffectSlots = reinterpret_cast<openq4_alGenAuxiliaryEffectSlots_t>( alGetProcAddress( "alGenAuxiliaryEffectSlots" ) );
	qalDeleteAuxiliaryEffectSlots = reinterpret_cast<openq4_alDeleteAuxiliaryEffectSlots_t>( alGetProcAddress( "alDeleteAuxiliaryEffectSlots" ) );
	qalAuxiliaryEffectSloti = reinterpret_cast<openq4_alAuxiliaryEffectSloti_t>( alGetProcAddress( "alAuxiliaryEffectSloti" ) );

	available =
		( qalGenEffects != NULL ) &&
		( qalDeleteEffects != NULL ) &&
		( qalEffecti != NULL ) &&
		( qalGenAuxiliaryEffectSlots != NULL ) &&
		( qalDeleteAuxiliaryEffectSlots != NULL ) &&
		( qalAuxiliaryEffectSloti != NULL );
	return available;
}
#endif

#if defined( ALC_HRTF_SOFT ) && defined( ALC_HRTF_STATUS_SOFT ) && defined( ALC_HRTF_DISABLED_SOFT ) && defined( ALC_HRTF_ENABLED_SOFT )
	#define OPENQ4_OPENAL_HRTF_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_HRTF_SUPPORTED 0
#endif

static int openQ4_GetHrtfMode()
{
	return idMath::ClampInt( OPENQ4_OPENAL_HRTF_AUTO, OPENQ4_OPENAL_HRTF_ON, s_openALHRTF.GetInteger() );
}

static const char* openQ4_HrtfModeName( const int mode )
{
	switch( mode )
	{
		case OPENQ4_OPENAL_HRTF_OFF:
			return "off";
		case OPENQ4_OPENAL_HRTF_ON:
			return "on";
		case OPENQ4_OPENAL_HRTF_AUTO:
		default:
			return "auto";
	}
}

static int openQ4_GetSpeakerCount()
{
	return ( s_numberOfSpeakers.GetInteger() == OPENQ4_OPENAL_SPEAKERS_SURROUND ) ? OPENQ4_OPENAL_SPEAKERS_SURROUND : OPENQ4_OPENAL_SPEAKERS_STEREO;
}

static const char* openQ4_SpeakerCountName( const int speakerCount )
{
	return ( speakerCount == OPENQ4_OPENAL_SPEAKERS_SURROUND ) ? "5.1 surround" : "stereo";
}

#if defined( ALC_OUTPUT_MODE_SOFT ) && defined( ALC_ANY_SOFT ) && defined( ALC_STEREO_SOFT ) && defined( ALC_SURROUND_5_1_SOFT )
	#define OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_OUTPUT_MODE_SUPPORTED
static const char* openQ4_OutputModeName( const ALCint outputMode )
{
	switch( outputMode )
	{
		case ALC_ANY_SOFT:
			return "any";
#if defined( ALC_MONO_SOFT )
		case ALC_MONO_SOFT:
			return "mono";
#endif
		case ALC_STEREO_SOFT:
			return "stereo";
#if defined( ALC_STEREO_BASIC_SOFT )
		case ALC_STEREO_BASIC_SOFT:
			return "basic stereo";
#endif
#if defined( ALC_STEREO_UHJ_SOFT )
		case ALC_STEREO_UHJ_SOFT:
			return "UHJ stereo";
#endif
#if defined( ALC_STEREO_HRTF_SOFT )
		case ALC_STEREO_HRTF_SOFT:
			return "HRTF stereo";
#endif
#if defined( ALC_QUAD_SOFT )
		case ALC_QUAD_SOFT:
			return "quad";
#endif
		case ALC_SURROUND_5_1_SOFT:
			return "5.1 surround";
#if defined( ALC_SURROUND_6_1_SOFT )
		case ALC_SURROUND_6_1_SOFT:
			return "6.1 surround";
#endif
#if defined( ALC_SURROUND_7_1_SOFT )
		case ALC_SURROUND_7_1_SOFT:
			return "7.1 surround";
#endif
		default:
			return "unknown";
	}
}

static ALCint openQ4_GetRequestedOutputMode()
{
#if defined( ALC_STEREO_HRTF_SOFT )
	if( openQ4_GetHrtfMode() == OPENQ4_OPENAL_HRTF_ON )
	{
		return ALC_STEREO_HRTF_SOFT;
	}
#endif
	return ( openQ4_GetSpeakerCount() == OPENQ4_OPENAL_SPEAKERS_SURROUND ) ? ALC_SURROUND_5_1_SOFT : ALC_STEREO_SOFT;
}

static const ALCint* openQ4_BuildOutputModeContextAttributes( ALCdevice* device, ALCint attributes[ 3 ] )
{
	attributes[0] = 0;
	if( device == NULL || alcIsExtensionPresent( device, "ALC_SOFT_output_mode" ) != AL_TRUE )
	{
		if( openQ4_GetSpeakerCount() == OPENQ4_OPENAL_SPEAKERS_SURROUND )
		{
			common->Warning( "OpenAL output mode requested '%s', but ALC_SOFT_output_mode is not available.", openQ4_SpeakerCountName( openQ4_GetSpeakerCount() ) );
		}
		return NULL;
	}

	const ALCint outputMode = openQ4_GetRequestedOutputMode();
	attributes[0] = ALC_OUTPUT_MODE_SOFT;
	attributes[1] = outputMode;
	attributes[2] = 0;
	common->Printf( "OpenAL output mode requested: %s\n", openQ4_OutputModeName( outputMode ) );
	return attributes;
}

static void openQ4_ReportOutputMode( ALCdevice* device )
{
	if( device == NULL || alcIsExtensionPresent( device, "ALC_SOFT_output_mode" ) != AL_TRUE )
	{
		common->Printf( "OpenAL output mode: unavailable\n" );
		return;
	}

	ALCint outputMode = ALC_ANY_SOFT;
	alcGetIntegerv( device, ALC_OUTPUT_MODE_SOFT, 1, &outputMode );
	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL output mode active: %s\n", openQ4_OutputModeName( outputMode ) );
	}
}
#else
static const ALCint* openQ4_BuildOutputModeContextAttributes( ALCdevice* device, ALCint attributes[ 3 ] )
{
	(void)device;
	(void)attributes;
	if( openQ4_GetSpeakerCount() == OPENQ4_OPENAL_SPEAKERS_SURROUND )
	{
		common->Warning( "OpenAL output mode requested '%s', but this build does not expose ALC_SOFT_output_mode symbols.", openQ4_SpeakerCountName( openQ4_GetSpeakerCount() ) );
	}
	return NULL;
}

static void openQ4_ReportOutputMode( ALCdevice* device )
{
	(void)device;
	common->Printf( "OpenAL output mode: unavailable\n" );
}
#endif

#if OPENQ4_OPENAL_HRTF_SUPPORTED
typedef const ALCchar* ( ALC_APIENTRY *openq4_alcGetStringiSOFT_t )( ALCdevice* device, ALCenum paramName, ALCsizei index );
typedef ALCboolean ( ALC_APIENTRY *openq4_alcResetDeviceSOFT_t )( ALCdevice* device, const ALCint* attribs );

static openq4_alcGetStringiSOFT_t qalcGetStringiSOFT = NULL;
static openq4_alcResetDeviceSOFT_t qalcResetDeviceSOFT = NULL;

static const char* openQ4_HrtfStatusName( const ALCint status )
{
	switch( status )
	{
		case ALC_HRTF_DISABLED_SOFT:
			return "disabled";
		case ALC_HRTF_ENABLED_SOFT:
			return "enabled";
#if defined( ALC_HRTF_DENIED_SOFT )
		case ALC_HRTF_DENIED_SOFT:
			return "denied";
#endif
#if defined( ALC_HRTF_REQUIRED_SOFT )
		case ALC_HRTF_REQUIRED_SOFT:
			return "required";
#endif
#if defined( ALC_HRTF_HEADPHONES_DETECTED_SOFT )
		case ALC_HRTF_HEADPHONES_DETECTED_SOFT:
			return "headphones-detected";
#endif
#if defined( ALC_HRTF_UNSUPPORTED_FORMAT_SOFT )
		case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT:
			return "unsupported-format";
#endif
		default:
			return "unknown";
	}
}

static bool openQ4_LoadHrtfProcs( ALCdevice* device )
{
	qalcGetStringiSOFT = reinterpret_cast<openq4_alcGetStringiSOFT_t>( alcGetProcAddress( device, "alcGetStringiSOFT" ) );
	qalcResetDeviceSOFT = reinterpret_cast<openq4_alcResetDeviceSOFT_t>( alcGetProcAddress( device, "alcResetDeviceSOFT" ) );
	return qalcResetDeviceSOFT != NULL;
}

static void openQ4_ApplyHrtfPreference( ALCdevice* device )
{
	if( device == NULL )
	{
		return;
	}

	const int mode = openQ4_GetHrtfMode();
	if( alcIsExtensionPresent( device, "ALC_SOFT_HRTF" ) != AL_TRUE )
	{
		if( mode != OPENQ4_OPENAL_HRTF_AUTO )
		{
			common->Warning( "OpenAL HRTF requested '%s', but ALC_SOFT_HRTF is not available.", openQ4_HrtfModeName( mode ) );
		}
		return;
	}

	if( !openQ4_LoadHrtfProcs( device ) )
	{
		if( mode != OPENQ4_OPENAL_HRTF_AUTO )
		{
			common->Warning( "OpenAL HRTF requested '%s', but alcResetDeviceSOFT is unavailable.", openQ4_HrtfModeName( mode ) );
		}
		return;
	}

	common->Printf( "OpenAL HRTF requested mode: %s\n", openQ4_HrtfModeName( mode ) );
	if( mode == OPENQ4_OPENAL_HRTF_AUTO )
	{
		return;
	}

	const ALCint hrtfValue = ( mode == OPENQ4_OPENAL_HRTF_ON ) ? ALC_TRUE : ALC_FALSE;
	const ALCint hrtfAttributes[] = {
		ALC_HRTF_SOFT, hrtfValue,
		0
	};
	(void)alcGetError( device );
	const bool resetSucceeded = qalcResetDeviceSOFT( device, hrtfAttributes ) == ALC_TRUE;
	const ALCenum resetError = CheckALCErrors( device );
	if( !resetSucceeded || resetError != ALC_NO_ERROR )
	{
		common->Warning( "OpenAL HRTF request '%s' was rejected by the active device.", openQ4_HrtfModeName( mode ) );
	}
}

static void openQ4_ReportHrtfStatus( ALCdevice* device )
{
	if( device == NULL || alcIsExtensionPresent( device, "ALC_SOFT_HRTF" ) != AL_TRUE )
	{
		common->Printf( "OpenAL HRTF: unavailable\n" );
		return;
	}

	ALCint hrtfStatus = ALC_HRTF_DISABLED_SOFT;
	alcGetIntegerv( device, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus );
	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL HRTF status: %s\n", openQ4_HrtfStatusName( hrtfStatus ) );
	}

#if defined( ALC_NUM_HRTF_SPECIFIERS_SOFT ) && defined( ALC_HRTF_SPECIFIER_SOFT )
	if( qalcGetStringiSOFT != NULL )
	{
		ALCint numSpecifiers = 0;
		alcGetIntegerv( device, ALC_NUM_HRTF_SPECIFIERS_SOFT, 1, &numSpecifiers );
		if( CheckALCErrors( device ) == ALC_NO_ERROR && numSpecifiers > 0 )
		{
			common->Printf( "OpenAL HRTF specifiers:\n" );
			for( ALCint i = 0; i < numSpecifiers; ++i )
			{
				const ALCchar* specifier = qalcGetStringiSOFT( device, ALC_HRTF_SPECIFIER_SOFT, i );
				if( specifier != NULL && specifier[0] != '\0' )
				{
					common->Printf( "    %s\n", specifier );
				}
			}
		}
	}
#endif
}
#else
static void openQ4_ApplyHrtfPreference( ALCdevice* device )
{
	(void)device;
	if( openQ4_GetHrtfMode() != OPENQ4_OPENAL_HRTF_AUTO )
	{
		common->Warning( "OpenAL HRTF requested '%s', but this build does not expose ALC_SOFT_HRTF symbols.", openQ4_HrtfModeName( openQ4_GetHrtfMode() ) );
	}
}

static void openQ4_ReportHrtfStatus( ALCdevice* device )
{
	(void)device;
	common->Printf( "OpenAL HRTF: unavailable\n" );
}
#endif

/*
========================
openQ4_GetSourceResamplerName
========================
*/
static const char* openQ4_GetSourceResamplerName( int index )
{
#if OPENQ4_OPENAL_SOURCE_RESAMPLER_SUPPORTED
	if( qalGetStringiSOFT == NULL || index < 0 )
	{
		return NULL;
	}
	return qalGetStringiSOFT( AL_RESAMPLER_NAME_SOFT, index );
#else
	(void)index;
	return NULL;
#endif
}

/*
========================
openQ4_SourceResamplerQualityScore
========================
*/
static int openQ4_SourceResamplerQualityScore( const char* name )
{
	if( name == NULL || name[0] == '\0' )
	{
		return 0;
	}

	const idStr resamplerName( name );
	const bool isFast = resamplerName.Find( "fast", false ) >= 0;
	const bool isSinc = resamplerName.Find( "sinc", false ) >= 0;
	if( isSinc && resamplerName.Find( "47", false ) >= 0 )
	{
		return isFast ? 650 : 700;
	}
	if( isSinc && resamplerName.Find( "23", false ) >= 0 )
	{
		return isFast ? 550 : 600;
	}
	if( isSinc && resamplerName.Find( "11", false ) >= 0 )
	{
		return isFast ? 450 : 500;
	}
	if( resamplerName.Find( "bsinc24", false ) >= 0 )
	{
		return isFast ? 550 : 600;
	}
	if( resamplerName.Find( "bsinc12", false ) >= 0 )
	{
		return isFast ? 450 : 500;
	}
	if( resamplerName.Find( "gaussian", false ) >= 0 )
	{
		return 350;
	}
	if( resamplerName.Find( "cubic", false ) >= 0 )
	{
		return 300;
	}
	if( resamplerName.Find( "linear", false ) >= 0 )
	{
		return 200;
	}
	if( resamplerName.Find( "point", false ) >= 0 )
	{
		return 100;
	}
	return 1;
}

/*
========================
idSoundHardware_OpenAL::InitSourceResampler
========================
*/
void idSoundHardware_OpenAL::InitSourceResampler()
{
	sourceResamplerAvailable = false;
	sourceResamplerWarningIssued = false;
	sourceResamplerCount = 0;
	sourceResamplerDefault = OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT;
	sourceResamplerSelected = OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT;

#if OPENQ4_OPENAL_SOURCE_RESAMPLER_SUPPORTED
	if( openalContext == NULL || alcGetCurrentContext() != openalContext )
	{
		return;
	}
	if( alIsExtensionPresent( "AL_SOFT_source_resampler" ) != AL_TRUE )
	{
		if( s_openALResampler.GetInteger() >= 0 )
		{
			common->Warning( "OpenAL source resampler index %d requested, but AL_SOFT_source_resampler is not available.", s_openALResampler.GetInteger() );
		}
		return;
	}

	qalGetStringiSOFT = reinterpret_cast<LPALGETSTRINGISOFT>( alGetProcAddress( "alGetStringiSOFT" ) );
	if( qalGetStringiSOFT == NULL )
	{
		common->Warning( "OpenAL source resampler extension is reported, but alGetStringiSOFT is unavailable." );
		return;
	}

	CheckALErrors();
	ALint numResamplers = 0;
	ALint defaultResampler = 0;
	alGetIntegerv( AL_NUM_RESAMPLERS_SOFT, &numResamplers );
	if( CheckALErrors() != AL_NO_ERROR || numResamplers <= 0 )
	{
		common->Warning( "OpenAL source resampler extension is reported, but no resamplers were enumerated." );
		return;
	}
	alGetIntegerv( AL_DEFAULT_RESAMPLER_SOFT, &defaultResampler );
	if( CheckALErrors() != AL_NO_ERROR || defaultResampler < 0 || defaultResampler >= numResamplers )
	{
		defaultResampler = 0;
	}

	sourceResamplerAvailable = true;
	sourceResamplerCount = numResamplers;
	sourceResamplerDefault = defaultResampler;
	sourceResamplerSelected = defaultResampler;

	int bestScore = openQ4_SourceResamplerQualityScore( openQ4_GetSourceResamplerName( defaultResampler ) );
	for( int i = 0; i < sourceResamplerCount; i++ )
	{
		const char* name = openQ4_GetSourceResamplerName( i );
		const int score = openQ4_SourceResamplerQualityScore( name );
		common->Printf( "OpenAL source resampler %d%s: %s\n",
			i,
			i == sourceResamplerDefault ? " (runtime default)" : "",
			name != NULL && name[0] != '\0' ? name : "<unknown>" );
		if( score > bestScore )
		{
			bestScore = score;
			sourceResamplerSelected = i;
		}
	}

	if( s_openALResampler.GetInteger() == OPENQ4_OPENAL_RESAMPLER_QUALITY_AUTO )
	{
		common->Printf( "OpenAL source resampler quality-auto selected %d: %s\n",
			sourceResamplerSelected,
			openQ4_GetSourceResamplerName( sourceResamplerSelected ) != NULL ? openQ4_GetSourceResamplerName( sourceResamplerSelected ) : "<unknown>" );
	}
	else if( s_openALResampler.GetInteger() == OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT )
	{
		common->Printf( "OpenAL source resampler using runtime default %d: %s\n",
			sourceResamplerDefault,
			openQ4_GetSourceResamplerName( sourceResamplerDefault ) != NULL ? openQ4_GetSourceResamplerName( sourceResamplerDefault ) : "<unknown>" );
	}
	else if( s_openALResampler.GetInteger() >= 0 && s_openALResampler.GetInteger() < sourceResamplerCount )
	{
		sourceResamplerSelected = s_openALResampler.GetInteger();
		common->Printf( "OpenAL source resampler user-selected %d: %s\n",
			sourceResamplerSelected,
			openQ4_GetSourceResamplerName( sourceResamplerSelected ) != NULL ? openQ4_GetSourceResamplerName( sourceResamplerSelected ) : "<unknown>" );
	}
	else
	{
		common->Warning( "OpenAL source resampler index %d is out of range; using quality-auto selection %d.",
			s_openALResampler.GetInteger(),
			sourceResamplerSelected );
	}

	ALuint testSource = 0;
	CheckALErrors();
	alGenSources( 1, &testSource );
	if( CheckALErrors() != AL_NO_ERROR || testSource == 0 )
	{
		common->Warning( "OpenAL source resampler selection could not be validated; using runtime source defaults." );
		sourceResamplerAvailable = false;
		return;
	}

	alSourcei( testSource, AL_SOURCE_RESAMPLER_SOFT, sourceResamplerSelected );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		common->Warning( "OpenAL source resampler %d was rejected; trying runtime default %d.", sourceResamplerSelected, sourceResamplerDefault );
		CheckALErrors();
		alSourcei( testSource, AL_SOURCE_RESAMPLER_SOFT, sourceResamplerDefault );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			common->Warning( "OpenAL runtime default source resampler was rejected; using runtime source defaults." );
			sourceResamplerAvailable = false;
		}
		else
		{
			sourceResamplerSelected = sourceResamplerDefault;
		}
	}

	alDeleteSources( 1, &testSource );
	CheckALErrors();
#else
	if( s_openALResampler.GetInteger() >= 0 )
	{
		common->Warning( "OpenAL source resampler requested, but this build does not expose AL_SOFT_source_resampler symbols." );
	}
#endif
}

/*
========================
idSoundHardware_OpenAL::ApplySourceResampler
========================
*/
void idSoundHardware_OpenAL::ApplySourceResampler( ALuint source )
{
#if OPENQ4_OPENAL_SOURCE_RESAMPLER_SUPPORTED
	if( !sourceResamplerAvailable || !alIsSource( source ) )
	{
		return;
	}

	int selectedResampler = sourceResamplerSelected;
	const int requestedResampler = s_openALResampler.GetInteger();
	if( requestedResampler == OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT )
	{
		selectedResampler = sourceResamplerDefault;
	}
	else if( requestedResampler >= 0 )
	{
		if( requestedResampler < sourceResamplerCount )
		{
			selectedResampler = requestedResampler;
		}
		else if( !sourceResamplerWarningIssued )
		{
			sourceResamplerWarningIssued = true;
			common->Warning( "OpenAL source resampler index %d is out of range; using quality-auto selection %d.",
				requestedResampler,
				sourceResamplerSelected );
		}
	}

	CheckALErrors();
	alSourcei( source, AL_SOURCE_RESAMPLER_SOFT, selectedResampler );
	if( CheckALErrors() != AL_NO_ERROR && !sourceResamplerWarningIssued )
	{
		sourceResamplerWarningIssued = true;
		common->Warning( "OpenAL source resampler %d could not be applied; using runtime source defaults for later voices.", selectedResampler );
	}
#else
	(void)source;
#endif
}

/*
========================
idSoundHardware_OpenAL::ApplySourceRadius
========================
*/
void idSoundHardware_OpenAL::ApplySourceRadius( ALuint source, float radius )
{
#if OPENQ4_OPENAL_SOURCE_RADIUS_SUPPORTED
	if( !sourceRadiusAvailable || !alIsSource( source ) )
	{
		return;
	}

	const float clampedRadius = Max( 0.0f, radius );
	CheckALErrors();
	alSourcef( source, AL_SOURCE_RADIUS, clampedRadius );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		sourceRadiusAvailable = false;
		if( !sourceRadiusWarningIssued )
		{
			sourceRadiusWarningIssued = true;
			common->Warning( "OpenAL source-radius spatialization disabled for this session; AL_SOURCE_RADIUS was rejected." );
		}
	}
#else
	(void)source;
	(void)radius;
#endif
}

/*
========================
idSoundHardware_OpenAL::InitCallbackBufferSupport
========================
*/
void idSoundHardware_OpenAL::InitCallbackBufferSupport()
{
	callbackBufferAvailable = false;
	callbackBufferWarningIssued = false;

#if OPENQ4_OPENAL_CALLBACK_BUFFER_SUPPORTED
	qalBufferCallbackSOFT = NULL;
	if( openalContext == NULL || alcGetCurrentContext() != openalContext )
	{
		return;
	}
	if( alIsExtensionPresent( "AL_SOFT_callback_buffer" ) != AL_TRUE )
	{
		return;
	}

	qalBufferCallbackSOFT = reinterpret_cast<LPALBUFFERCALLBACKSOFT>( alGetProcAddress( "alBufferCallbackSOFT" ) );
	if( qalBufferCallbackSOFT == NULL )
	{
		callbackBufferWarningIssued = true;
		common->Warning( "OpenAL callback-buffer extension is reported, but alBufferCallbackSOFT is unavailable." );
		return;
	}

	ALuint testBuffer = 0;
	CheckALErrors();
	alGenBuffers( 1, &testBuffer );
	if( CheckALErrors() != AL_NO_ERROR || testBuffer == 0 )
	{
		callbackBufferWarningIssued = true;
		common->Warning( "OpenAL callback-buffer support could not be validated; scratch buffer allocation failed." );
		return;
	}

	CheckALErrors();
	qalBufferCallbackSOFT( testBuffer, AL_FORMAT_MONO16, 44100, openQ4_OpenALCallbackBufferProbe, NULL );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		callbackBufferWarningIssued = true;
		common->Warning( "OpenAL callback-buffer support could not be validated; alBufferCallbackSOFT was rejected." );
		alDeleteBuffers( 1, &testBuffer );
		CheckALErrors();
		return;
	}

	alDeleteBuffers( 1, &testBuffer );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		callbackBufferWarningIssued = true;
		common->Warning( "OpenAL callback-buffer support could not be validated; scratch buffer cleanup failed." );
		return;
	}

	callbackBufferAvailable = true;
	common->Printf( "OpenAL callback buffers validated for future streaming path.\n" );
#else
	if( alIsExtensionPresent( "AL_SOFT_callback_buffer" ) == AL_TRUE && !callbackBufferWarningIssued )
	{
		callbackBufferWarningIssued = true;
		common->Warning( "OpenAL callback-buffer extension is reported, but this build does not expose AL_SOFT_callback_buffer symbols." );
	}
#endif
}


/*
========================
idSoundHardware_OpenAL::idSoundHardware_OpenAL
========================
*/
idSoundHardware_OpenAL::idSoundHardware_OpenAL()
{
	openalDevice = NULL;
	openalContext = NULL;
	efxFiltersAvailable = false;
	efxEnabled = false;
	auxEffectSlot = 0;
	auxReverbEffect = 0;
	deviceEventsEnabled = false;
	enabledDeviceEventFlags = 0;
	reopenDeviceAvailable = false;
	deferredUpdatesAvailable = false;
	deferredUpdatesActive = false;
	deferredUpdatesDisableLogged = false;
	sourceResamplerAvailable = false;
	sourceResamplerWarningIssued = false;
	sourceResamplerCount = 0;
	sourceResamplerDefault = OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT;
	sourceResamplerSelected = OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT;
	sourceRadiusAvailable = false;
	sourceRadiusWarningIssued = false;
	callbackBufferAvailable = false;
	callbackBufferWarningIssued = false;
	lastDeviceCheckTime = 0;
	pendingDeviceEventFlags = 0;
	pendingDeviceEventCheckTime = 0;
	lastDeviceRecoveryTime = 0;
	pendingSyntheticActiveDeviceDisconnect = false;
	lastPerfPrintTime = 0;
	openedWithDefaultFallback = false;
	openedHrtfMode = openQ4_GetHrtfMode();
	openedSpeakerCount = openQ4_GetSpeakerCount();

	//vuMeterRMS = NULL;
	//vuMeterPeak = NULL;

	//outputChannels = 0;
	//channelMask = 0;

	voices.SetNum( 0 );
	zombieVoices.SetNum( 0 );
	freeVoices.SetNum( 0 );

	lastResetTime = 0;
}

bool idSoundHardware_OpenAL::IsDefaultDeviceChoiceValue( const char* deviceName ) {
	return deviceName != NULL && idStr::Icmp( deviceName, OPENQ4_AUDIO_DEVICE_DEFAULT_CHOICE ) == 0;
}

idStr idSoundHardware_OpenAL::NormalizeRequestedDeviceName( const char* deviceName ) {
	if( deviceName == NULL || IsDefaultDeviceChoiceValue( deviceName ) ) {
		return "";
	}

	return deviceName;
}

idStr idSoundHardware_OpenAL::SanitizeDeviceLabel( const char* deviceName ) {
	idStr sanitized = ( deviceName != NULL ) ? deviceName : "";
	sanitized.Replace( "\r", " " );
	sanitized.Replace( "\n", " " );
	sanitized.Replace( "\t", " " );
	sanitized.StripLeading( ' ' );
	sanitized.StripTrailingWhitespace();
	return sanitized;
}

void idSoundHardware_OpenAL::AppendChoiceItem( idStr& list, const char* item ) {
	if( list.Length() > 0 ) {
		list += ";";
	}

	list += "\"";
	for( const char* it = item; it != NULL && *it != '\0'; ++it ) {
		if( *it == '\\' || *it == '"' ) {
			list += "\\";
		}
		list += *it;
	}
	list += "\"";
}

bool idSoundHardware_OpenAL::DeviceListContains( const idStrList& deviceNames, const char* deviceName ) {
	if( deviceName == NULL || deviceName[0] == '\0' ) {
		return false;
	}

	for( int i = 0; i < deviceNames.Num(); ++i ) {
		if( deviceNames[i].Icmp( deviceName ) == 0 ) {
			return true;
		}
	}

	return false;
}

void idSoundHardware_OpenAL::GetAvailablePlaybackDevices( idStrList& deviceNames, idStr& defaultDeviceName ) {
	deviceNames.Clear();
	defaultDeviceName.Clear();

	const ALCchar* defaultDevice = alcGetString( NULL, openQ4_GetDefaultPlaybackDeviceToken( NULL ) );
	if( defaultDevice != NULL && defaultDevice[0] != '\0' ) {
		defaultDeviceName = reinterpret_cast<const char*>( defaultDevice );
	}

	const ALCchar* deviceList = alcGetString( NULL, openQ4_GetPlaybackDevicesToken( NULL ) );
	if( deviceList == NULL || deviceList[0] == '\0' ) {
		return;
	}

	for( const ALCchar* it = deviceList; *it != '\0'; it += strlen( it ) + 1 ) {
		idStr deviceName = reinterpret_cast<const char*>( it );
		deviceName.StripLeading( ' ' );
		deviceName.StripTrailingWhitespace();
		if( !deviceName.Length() || DeviceListContains( deviceNames, deviceName.c_str() ) ) {
			continue;
		}

		deviceNames.Append( deviceName );
	}
}

idStr idSoundHardware_OpenAL::GetActivePlaybackDeviceName( ALCdevice* device ) {
	idStr activeDeviceName;
	if( device == NULL ) {
		return activeDeviceName;
	}

	const ALCchar* activeDevice = NULL;
#if defined( ALC_ALL_DEVICES_SPECIFIER )
	if( openQ4_UseEnumerateAllDevices( device ) ) {
		activeDevice = alcGetString( device, ALC_ALL_DEVICES_SPECIFIER );
	}
#endif
	if( CheckALCErrors( device ) != ALC_NO_ERROR || activeDevice == NULL || activeDevice[0] == '\0' ) {
		activeDevice = alcGetString( device, ALC_DEVICE_SPECIFIER );
		CheckALCErrors( device );
	}

	if( activeDevice != NULL && activeDevice[0] != '\0' ) {
		activeDeviceName = reinterpret_cast<const char*>( activeDevice );
	}

	return activeDeviceName;
}

void idSoundHardware_OpenAL::BuildDeviceChoiceStrings( const char* requestedDeviceName, idStr& choiceNames, idStr& choiceValues ) {
	const idStr normalizedRequestedDeviceName = NormalizeRequestedDeviceName( requestedDeviceName );
	const idStr defaultLabel = common->GetLanguageDict()->GetString( "#str_229913" );
	const idStr unavailableLabel = common->GetLanguageDict()->GetString( "#str_41105" );

	idStrList deviceNames;
	idStr defaultDeviceName;
	GetAvailablePlaybackDevices( deviceNames, defaultDeviceName );

	idStr displayDefaultLabel = defaultLabel;
	const idStr sanitizedDefaultDeviceName = SanitizeDeviceLabel( defaultDeviceName.c_str() );
	if( sanitizedDefaultDeviceName.Length() ) {
		displayDefaultLabel = va( "%s (%s)", defaultLabel.c_str(), sanitizedDefaultDeviceName.c_str() );
	}

	choiceNames.Clear();
	choiceValues.Clear();
	AppendChoiceItem( choiceNames, displayDefaultLabel.c_str() );
	AppendChoiceItem( choiceValues, OPENQ4_AUDIO_DEVICE_DEFAULT_CHOICE );

	bool requestedDeviceFound = normalizedRequestedDeviceName.IsEmpty();
	for( int i = 0; i < deviceNames.Num(); ++i ) {
		const idStr& deviceName = deviceNames[i];
		const idStr deviceLabel = SanitizeDeviceLabel( deviceName.c_str() );
		if( !deviceLabel.Length() ) {
			continue;
		}

		AppendChoiceItem( choiceNames, deviceLabel.c_str() );
		AppendChoiceItem( choiceValues, deviceName.c_str() );
		if( deviceName.Icmp( normalizedRequestedDeviceName ) == 0 ) {
			requestedDeviceFound = true;
		}
	}

	if( !requestedDeviceFound && normalizedRequestedDeviceName.Length() ) {
		idStr requestedLabel = SanitizeDeviceLabel( normalizedRequestedDeviceName.c_str() );
		if( unavailableLabel.Length() ) {
			requestedLabel = va( "%s (%s)", requestedLabel.c_str(), unavailableLabel.c_str() );
		}

		AppendChoiceItem( choiceNames, requestedLabel.c_str() );
		AppendChoiceItem( choiceValues, normalizedRequestedDeviceName.c_str() );
	}
}

void idSoundHardware_OpenAL::CaptureOpenedDeviceState( const char* requestedDeviceName ) {
	idStrList deviceNames;
	GetAvailablePlaybackDevices( deviceNames, openedDefaultDeviceName );

	openedRequestedDeviceName = NormalizeRequestedDeviceName( requestedDeviceName );
	openedActiveDeviceName = GetActivePlaybackDeviceName( openalDevice );
	openedWithDefaultFallback = openedRequestedDeviceName.Length() > 0 && openedActiveDeviceName.Icmp( openedRequestedDeviceName ) != 0;
	openedHrtfMode = openQ4_GetHrtfMode();
	openedSpeakerCount = openQ4_GetSpeakerCount();
	lastDeviceCheckTime = Sys_Milliseconds();
}

void idSoundHardware_OpenAL::ClearPendingDeviceRecoveryState() {
	pendingDeviceEventFlags = 0;
	pendingDeviceEventCheckTime = 0;
	pendingSyntheticActiveDeviceDisconnect = false;
}

bool idSoundHardware_OpenAL::RecoverDeviceOrRequestRestart( const char* requestedDeviceName, const char* reason, const char* restartMessage ) {
	if( TryReopenDevice( requestedDeviceName, reason ) )
	{
		return false;
	}

	common->Printf( "%s\n", restartMessage != NULL ? restartMessage : "OpenAL device recovery requires sound system restart." );
	lastDeviceRecoveryTime = Sys_Milliseconds();
	ClearPendingDeviceRecoveryState();
	soundSystemLocal.SetNeedsRestart();
	return true;
}

void idSoundHardware_OpenAL::QueueSyntheticActiveDeviceDisconnect() {
	if( openalDevice == NULL )
	{
		common->Warning( "OpenAL synthetic active-device disconnect ignored; no playback device is open." );
		return;
	}

	pendingSyntheticActiveDeviceDisconnect = true;
	openQ4_QueuePendingDeviceEventFlags( OPENQ4_OPENAL_DEVICE_EVENT_REMOVED );
	common->Printf( "OpenAL synthetic active-device disconnect queued for '%s'.\n", openedActiveDeviceName.Length() > 0 ? openedActiveDeviceName.c_str() : "<unknown>" );
}

void idSoundHardware_OpenAL::QueueSyntheticDefaultDeviceChange( const char* previousDefaultDeviceName ) {
	if( openalDevice == NULL )
	{
		common->Warning( "OpenAL synthetic default-device change ignored; no playback device is open." );
		return;
	}
	if( !openedRequestedDeviceName.IsEmpty() && !openedWithDefaultFallback )
	{
		common->Warning( "OpenAL synthetic default-device change ignored; requested device '%s' is active.", openedRequestedDeviceName.c_str() );
		return;
	}

	idStrList deviceNames;
	idStr currentDefaultDeviceName;
	GetAvailablePlaybackDevices( deviceNames, currentDefaultDeviceName );
	if( currentDefaultDeviceName.IsEmpty() )
	{
		common->Warning( "OpenAL synthetic default-device change ignored; system default playback device is unknown." );
		return;
	}

	idStr simulatedPreviousDefaultDeviceName = SanitizeDeviceLabel( previousDefaultDeviceName );
	if( simulatedPreviousDefaultDeviceName.IsEmpty() )
	{
		simulatedPreviousDefaultDeviceName = currentDefaultDeviceName;
		simulatedPreviousDefaultDeviceName += " (synthetic previous)";
	}
	else if( simulatedPreviousDefaultDeviceName.Icmp( currentDefaultDeviceName ) == 0 )
	{
		simulatedPreviousDefaultDeviceName += " (synthetic previous)";
	}

	openedDefaultDeviceName = simulatedPreviousDefaultDeviceName;
	openQ4_QueuePendingDeviceEventFlags( OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED );
	common->Printf( "OpenAL synthetic system default device change queued; previous default recorded as '%s'.\n", openedDefaultDeviceName.c_str() );
}

void idSoundHardware_OpenAL::EnableDeviceEventMonitoring() {
	DisableDeviceEventMonitoring();

#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
	if( openalDevice == NULL )
	{
		return;
	}

	if( alcIsExtensionPresent( openalDevice, "ALC_SOFT_system_events" ) != ALC_TRUE || !openQ4_LoadSystemEventProcs( openalDevice ) )
	{
		return;
	}

	ALCenum events[ 3 ];
	int eventCount = 0;
	enabledDeviceEventFlags = 0;
	const int eventFlags[] = {
		OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED,
		OPENQ4_OPENAL_DEVICE_EVENT_ADDED,
		OPENQ4_OPENAL_DEVICE_EVENT_REMOVED
	};
	for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
	{
		const ALCenum eventType = openQ4_DeviceEventTypeForFlag( eventFlags[ i ] );
		if( eventType != 0 && qalcEventIsSupportedSOFT( eventType, ALC_PLAYBACK_DEVICE_SOFT ) == ALC_EVENT_SUPPORTED_SOFT )
		{
			events[ eventCount++ ] = eventType;
			enabledDeviceEventFlags |= eventFlags[ i ];
		}
	}

	if( eventCount == 0 )
	{
		enabledDeviceEventFlags = 0;
		return;
	}

	openQ4_ConsumePendingDeviceEventFlags();
	qalcEventCallbackSOFT( openQ4_OpenALDeviceEventCallback, NULL );
	if( qalcEventControlSOFT( eventCount, events, ALC_TRUE ) != ALC_TRUE )
	{
		common->Warning( "OpenAL playback device event monitoring could not be enabled; falling back to polling." );
		qalcEventCallbackSOFT( NULL, NULL );
		enabledDeviceEventFlags = 0;
		return;
	}

	deviceEventsEnabled = true;

	idStr eventNames;
	for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
	{
		if( ( enabledDeviceEventFlags & eventFlags[ i ] ) != 0 )
		{
			openQ4_AppendDeviceEventName( eventNames, eventFlags[ i ] );
		}
	}
	common->Printf( "OpenAL playback device event monitoring enabled: %s\n", eventNames.c_str() );
#endif
}

void idSoundHardware_OpenAL::DisableDeviceEventMonitoring() {
#if OPENQ4_OPENAL_SYSTEM_EVENTS_SUPPORTED
	if( qalcEventControlSOFT != NULL && deviceEventsEnabled && enabledDeviceEventFlags != 0 )
	{
		ALCenum events[ 3 ];
		int eventCount = 0;
		const int eventFlags[] = {
			OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED,
			OPENQ4_OPENAL_DEVICE_EVENT_ADDED,
			OPENQ4_OPENAL_DEVICE_EVENT_REMOVED
		};
		for( int i = 0; i < sizeof( eventFlags ) / sizeof( eventFlags[ 0 ] ); ++i )
		{
			if( ( enabledDeviceEventFlags & eventFlags[ i ] ) != 0 )
			{
				const ALCenum eventType = openQ4_DeviceEventTypeForFlag( eventFlags[ i ] );
				if( eventType != 0 )
				{
					events[ eventCount++ ] = eventType;
				}
			}
		}
		if( eventCount > 0 )
		{
			qalcEventControlSOFT( eventCount, events, ALC_FALSE );
		}
	}
	if( qalcEventCallbackSOFT != NULL && deviceEventsEnabled )
	{
		qalcEventCallbackSOFT( NULL, NULL );
	}
#endif
	openQ4_ConsumePendingDeviceEventFlags();
	pendingSyntheticActiveDeviceDisconnect = false;
	deviceEventsEnabled = false;
	enabledDeviceEventFlags = 0;
}

bool idSoundHardware_OpenAL::TryReopenDevice( const char* requestedDeviceName, const char* reason ) {
	const idStr normalizedRequestedDeviceName = NormalizeRequestedDeviceName( requestedDeviceName );

#if OPENQ4_OPENAL_REOPEN_DEVICE_SUPPORTED
	if( openalDevice == NULL || !reopenDeviceAvailable || qalcReopenDeviceSOFT == NULL )
	{
		return false;
	}

	CountDiagnosticEvent( OPENAL_DIAG_DEVICE_REOPEN_ATTEMPTS );

	idStr reopenTargetDeviceName = normalizedRequestedDeviceName;
	if( reopenTargetDeviceName.Length() > 0 )
	{
		idStrList currentDeviceNames;
		idStr currentDefaultDeviceName;
		GetAvailablePlaybackDevices( currentDeviceNames, currentDefaultDeviceName );
		if( !DeviceListContains( currentDeviceNames, reopenTargetDeviceName.c_str() ) )
		{
			reopenTargetDeviceName.Clear();
		}
	}

	const char* reopenDeviceName = reopenTargetDeviceName.Length() > 0 ? reopenTargetDeviceName.c_str() : NULL;
	ALCint reopenContextAttributes[ 3 ];
	const ALCint* requestedReopenContextAttributes = openQ4_BuildOutputModeContextAttributes( openalDevice, reopenContextAttributes );
	if( normalizedRequestedDeviceName.Length() > 0 && reopenDeviceName == NULL )
	{
		common->Printf( "OpenAL %s; requested device '%s' is unavailable, attempting in-place device reopen to system default.\n", reason != NULL ? reason : "device change detected", normalizedRequestedDeviceName.c_str() );
	}
	else
	{
		common->Printf( "OpenAL %s; attempting in-place device reopen to %s.\n", reason != NULL ? reason : "device change detected", reopenDeviceName != NULL ? reopenDeviceName : "system default" );
	}

	(void)alcGetError( openalDevice );
	bool reopened = qalcReopenDeviceSOFT( openalDevice, reopenDeviceName, requestedReopenContextAttributes ) == ALC_TRUE;
	ALCenum reopenError = CheckALCErrors( openalDevice );
	if( ( !reopened || reopenError != ALC_NO_ERROR ) && requestedReopenContextAttributes != NULL )
	{
		common->Warning( "OpenAL in-place device reopen rejected requested output mode; retrying with runtime default." );
		(void)alcGetError( openalDevice );
		reopened = qalcReopenDeviceSOFT( openalDevice, reopenDeviceName, NULL ) == ALC_TRUE;
		reopenError = CheckALCErrors( openalDevice );
	}
	if( !reopened || reopenError != ALC_NO_ERROR )
	{
		CountDiagnosticEvent( OPENAL_DIAG_DEVICE_REOPEN_FAILURES );
		common->Warning( "OpenAL in-place device reopen failed; falling back to sound system restart." );
		return false;
	}

	CountDiagnosticEvent( OPENAL_DIAG_DEVICE_REOPEN_SUCCESSES );
	openQ4_ApplyHrtfPreference( openalDevice );
	CaptureOpenedDeviceState( normalizedRequestedDeviceName.c_str() );
	lastDeviceRecoveryTime = Sys_Milliseconds();
	ClearPendingDeviceRecoveryState();

	if( openedDefaultDeviceName.Length() > 0 )
	{
		common->Printf( "OpenAL default device: %s\n", openedDefaultDeviceName.c_str() );
	}
	if( openedActiveDeviceName.Length() > 0 )
	{
		common->Printf( "OpenAL active device: %s\n", openedActiveDeviceName.c_str() );
	}
	if( openedWithDefaultFallback )
	{
		common->Warning( "OpenAL requested device '%s' is unavailable; using '%s' until it returns.", openedRequestedDeviceName.c_str(), openedActiveDeviceName.c_str() );
	}
	openQ4_ReportHrtfStatus( openalDevice );
	openQ4_ReportOutputMode( openalDevice );
	return true;
#else
	(void)normalizedRequestedDeviceName;
	(void)reason;
	return false;
#endif
}

void idSoundHardware_OpenAL::DisableEFXFilters( const char* reason ) {
	if( !efxFiltersAvailable )
	{
		return;
	}

	efxFiltersAvailable = false;
	CountDiagnosticEvent( OPENAL_DIAG_EFX_FILTER_ERRORS );
	if( reason != NULL && reason[0] != '\0' )
	{
		common->Warning( "OpenAL EFX filter routing disabled for this session: %s.", reason );
	}
	else
	{
		common->Warning( "OpenAL EFX filter routing disabled for this session." );
	}
}

void idSoundHardware_OpenAL::DisableDeferredUpdates( const char* reason ) {
	if( !deferredUpdatesAvailable && !deferredUpdatesActive )
	{
		return;
	}

	deferredUpdatesAvailable = false;
	deferredUpdatesActive = false;
	CountDiagnosticEvent( OPENAL_DIAG_DEFERRED_UPDATE_ERRORS );
	if( !deferredUpdatesDisableLogged )
	{
		deferredUpdatesDisableLogged = true;
		if( reason != NULL && reason[0] != '\0' )
		{
			common->Warning( "OpenAL deferred source updates disabled for this session: %s.", reason );
		}
		else
		{
			common->Warning( "OpenAL deferred source updates disabled for this session." );
		}
	}
}

void idSoundHardware_OpenAL::BeginDeferredUpdates() {
#if OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED
	if( !deferredUpdatesAvailable || deferredUpdatesActive || openalContext == NULL || alcGetCurrentContext() != openalContext )
	{
		return;
	}

	qalDeferUpdatesSOFT();
	if( CheckALErrors() == AL_NO_ERROR )
	{
		deferredUpdatesActive = true;
	}
	else
	{
		DisableDeferredUpdates( "alDeferUpdatesSOFT failed" );
	}
#endif
}

void idSoundHardware_OpenAL::EndDeferredUpdates() {
#if OPENQ4_OPENAL_DEFERRED_UPDATES_SUPPORTED
	if( !deferredUpdatesActive )
	{
		return;
	}

	qalProcessUpdatesSOFT();
	deferredUpdatesActive = false;
	if( CheckALErrors() != AL_NO_ERROR )
	{
		DisableDeferredUpdates( "alProcessUpdatesSOFT failed" );
	}
#endif
}

bool idSoundHardware_OpenAL::UpdateDeviceMonitoring() {
	const int nowTime = Sys_Milliseconds();
	const int consumedDeviceEventFlags = openQ4_ConsumePendingDeviceEventFlags();
	if( consumedDeviceEventFlags != 0 )
	{
		pendingDeviceEventFlags |= consumedDeviceEventFlags;
		pendingDeviceEventCheckTime = nowTime + OPENQ4_OPENAL_DEVICE_EVENT_SETTLE_MSEC;
		CountDiagnosticEvent( OPENAL_DIAG_DEVICE_EVENTS );
		idStr eventNames;
		openQ4_AppendDeviceEventNames( eventNames, consumedDeviceEventFlags );
		common->Printf( "OpenAL playback device event received: %s; checking after %d ms settle.\n", eventNames.Length() > 0 ? eventNames.c_str() : "unknown", OPENQ4_OPENAL_DEVICE_EVENT_SETTLE_MSEC );
	}

	const idStr requestedDeviceName = NormalizeRequestedDeviceName( s_deviceName.GetString() );
	if( requestedDeviceName.Icmp( openedRequestedDeviceName ) != 0 ) {
		if( requestedDeviceName.Length() == 0 ) {
			common->Printf( "OpenAL requested device changed to system default.\n" );
		} else if( openedRequestedDeviceName.Length() == 0 ) {
			common->Printf( "OpenAL requested device changed to '%s'.\n", requestedDeviceName.c_str() );
		} else {
			common->Printf( "OpenAL requested device changed from '%s' to '%s'.\n", openedRequestedDeviceName.c_str(), requestedDeviceName.c_str() );
		}
		return RecoverDeviceOrRequestRestart( requestedDeviceName.c_str(), "requested device changed", "OpenAL requested device change requires sound system restart." );
	}

	const int hrtfMode = openQ4_GetHrtfMode();
	if( hrtfMode != openedHrtfMode ) {
		common->Printf( "OpenAL HRTF mode changed from %s to %s; restarting sound system.\n", openQ4_HrtfModeName( openedHrtfMode ), openQ4_HrtfModeName( hrtfMode ) );
		lastDeviceRecoveryTime = Sys_Milliseconds();
		ClearPendingDeviceRecoveryState();
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	const int speakerCount = openQ4_GetSpeakerCount();
	if( speakerCount != openedSpeakerCount ) {
		common->Printf( "OpenAL speaker mode changed from %s to %s; restarting sound system.\n", openQ4_SpeakerCountName( openedSpeakerCount ), openQ4_SpeakerCountName( speakerCount ) );
		lastDeviceRecoveryTime = Sys_Milliseconds();
		ClearPendingDeviceRecoveryState();
		soundSystemLocal.SetNeedsRestart();
		return true;
	}

	int settledDeviceEventFlags = 0;
	if( pendingDeviceEventFlags != 0 )
	{
		if( pendingDeviceEventCheckTime > nowTime )
		{
			return false;
		}
		if( lastDeviceRecoveryTime > 0 && lastDeviceRecoveryTime + OPENQ4_OPENAL_DEVICE_RECOVERY_COOLDOWN_MSEC > nowTime )
		{
			pendingDeviceEventCheckTime = lastDeviceRecoveryTime + OPENQ4_OPENAL_DEVICE_RECOVERY_COOLDOWN_MSEC;
			return false;
		}

		settledDeviceEventFlags = pendingDeviceEventFlags;
		pendingDeviceEventFlags = 0;
		pendingDeviceEventCheckTime = 0;

		idStr eventNames;
		openQ4_AppendDeviceEventNames( eventNames, settledDeviceEventFlags );
		common->Printf( "OpenAL checking settled playback device event: %s\n", eventNames.Length() > 0 ? eventNames.c_str() : "unknown" );
	}
	else if( lastDeviceCheckTime + OPENQ4_OPENAL_DEVICE_POLL_MSEC > nowTime )
	{
		return false;
	}

	lastDeviceCheckTime = nowTime;

	if( pendingSyntheticActiveDeviceDisconnect && ( settledDeviceEventFlags & OPENQ4_OPENAL_DEVICE_EVENT_REMOVED ) != 0 )
	{
		pendingSyntheticActiveDeviceDisconnect = false;
		common->Warning( "OpenAL synthetic device '%s' disconnected.", openedActiveDeviceName.Length() > 0 ? openedActiveDeviceName.c_str() : "<unknown>" );
		return RecoverDeviceOrRequestRestart( openedRequestedDeviceName.c_str(), "synthetic active device disconnected", "OpenAL synthetic disconnected device recovery requires sound system restart." );
	}

#if defined( ALC_CONNECTED )
	if( alcIsExtensionPresent( openalDevice, "ALC_EXT_disconnect" ) == AL_TRUE ) {
		ALCint connected = ALC_TRUE;
		alcGetIntegerv( openalDevice, ALC_CONNECTED, 1, &connected );
		if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR && connected == ALC_FALSE ) {
			common->Warning( "OpenAL device '%s' disconnected.", openedActiveDeviceName.c_str() );
			return RecoverDeviceOrRequestRestart( openedRequestedDeviceName.c_str(), "active device disconnected", "OpenAL disconnected device recovery requires sound system restart." );
		}
	}
#endif

	idStrList deviceNames;
	idStr defaultDeviceName;
	GetAvailablePlaybackDevices( deviceNames, defaultDeviceName );

	if( openedWithDefaultFallback && DeviceListContains( deviceNames, openedRequestedDeviceName.c_str() ) ) {
		common->Printf( "OpenAL requested device '%s' is available again.\n", openedRequestedDeviceName.c_str() );
		return RecoverDeviceOrRequestRestart( openedRequestedDeviceName.c_str(), "requested device became available", "OpenAL requested device return requires sound system restart." );
	}

	if( ( openedRequestedDeviceName.IsEmpty() || openedWithDefaultFallback ) && defaultDeviceName.Length() > 0 && defaultDeviceName.Icmp( openedDefaultDeviceName ) != 0 ) {
		common->Printf( "OpenAL system default device changed from '%s' to '%s'.\n", openedDefaultDeviceName.c_str(), defaultDeviceName.c_str() );
		return RecoverDeviceOrRequestRestart( openedRequestedDeviceName.c_str(), "system default device changed", "OpenAL default device change requires sound system restart." );
	}

	if( settledDeviceEventFlags != 0 )
	{
		idStr eventNames;
		openQ4_AppendDeviceEventNames( eventNames, settledDeviceEventFlags );
		common->Printf( "OpenAL playback device event settled without recovery: %s\n", eventNames.Length() > 0 ? eventNames.c_str() : "unknown" );
	}

	return false;
}

void idSoundHardware_OpenAL::PrintDeviceList( const char* list )
{
	if( !list || *list == '\0' )
	{
		idLib::Printf( "    !!! none !!!\n" );
	}
	else
	{
		do
		{
			idLib::Printf( "    %s\n", list );
			list += strlen( list ) + 1;
		}
		while( *list != '\0' );
	}
}

void idSoundHardware_OpenAL::PrintALCInfo( ALCdevice* device )
{
	ALCint major, minor;

	if( device )
	{
		idLib::Printf( "\n" );
		const idStr devname = GetActivePlaybackDeviceName( device );
		idLib::Printf( "** Info for device \"%s\" **\n", devname.Length() ? devname.c_str() : "<unknown>" );
	}
	alcGetIntegerv( device, ALC_MAJOR_VERSION, 1, &major );
	alcGetIntegerv( device, ALC_MINOR_VERSION, 1, &minor );

	if( CheckALCErrors( device ) == ALC_NO_ERROR )
	{
		idLib::Printf( "ALC version: %d.%d\n", major, minor );
	}

	if( device )
	{
		idLib::Printf( "OpenAL extensions: %s", alGetString( AL_EXTENSIONS ) );

		//idLib::Printf("ALC extensions:");
		//printList(alcGetString(device, ALC_EXTENSIONS), ' ');
		CheckALCErrors( device );
	}
}

void idSoundHardware_OpenAL::PrintALInfo()
{
	idLib::Printf( "OpenAL vendor string: %s\n", alGetString( AL_VENDOR ) );
	idLib::Printf( "OpenAL renderer string: %s\n", alGetString( AL_RENDERER ) );
	idLib::Printf( "OpenAL version string: %s\n", alGetString( AL_VERSION ) );
	idLib::Printf( "OpenAL extensions: %s", alGetString( AL_EXTENSIONS ) );
	//PrintList(alGetString(AL_EXTENSIONS), ' ');
	CheckALErrors();
}

static const char* openQ4_OpenALSourceStateName( ALint state )
{
	switch( state )
	{
		case AL_INITIAL:
			return "AL_INITIAL";
		case AL_PLAYING:
			return "AL_PLAYING";
		case AL_PAUSED:
			return "AL_PAUSED";
		case AL_STOPPED:
			return "AL_STOPPED";
		default:
			return "AL_UNKNOWN";
	}
}

void openALSourceSelfTest_f( const idCmdArgs& args )
{
	(void)args;

	idSoundHardware_OpenAL& hardware = soundSystemLocal.hardware;
	ALCdevice* device = hardware.GetOpenALDevice();
	ALCcontext* context = hardware.GetOpenALContext();
	if( device == NULL || context == NULL )
	{
		idLib::Warning( "OpenAL source self-test requires an active OpenAL device and context." );
		return;
	}

	ALCcontext* previousContext = alcGetCurrentContext();
	const bool restoreContext = previousContext != context;
	if( restoreContext && alcMakeContextCurrent( context ) == 0 )
	{
		CheckALCErrors( device );
		idLib::Warning( "OpenAL source self-test could not make the sound context current." );
		return;
	}

	static const int SELFTEST_SAMPLE_RATE = 44100;
	static const int SELFTEST_FRAMES = SELFTEST_SAMPLE_RATE / 4;
	static const float SELFTEST_FREQUENCY = 440.0f;
	static const float SELFTEST_AMPLITUDE = 0.12f;
	int16 samples[ SELFTEST_FRAMES ];
	for( int i = 0; i < SELFTEST_FRAMES; ++i )
	{
		const float phase = idMath::TWO_PI * SELFTEST_FREQUENCY * static_cast<float>( i ) / static_cast<float>( SELFTEST_SAMPLE_RATE );
		samples[i] = static_cast<int16>( idMath::Sin( phase ) * SELFTEST_AMPLITUDE * 32767.0f );
	}

	ALuint buffer = 0;
	ALuint source = 0;
	bool passed = false;
	ALint state = AL_INITIAL;

	CheckALErrors();
	alGenBuffers( 1, &buffer );
	if( CheckALErrors() == AL_NO_ERROR && buffer != 0 )
	{
		alBufferData( buffer, AL_FORMAT_MONO16, samples, sizeof( samples ), SELFTEST_SAMPLE_RATE );
		if( CheckALErrors() == AL_NO_ERROR )
		{
			alGenSources( 1, &source );
			if( CheckALErrors() == AL_NO_ERROR && source != 0 )
			{
				hardware.ApplySourceResampler( source );
				hardware.ApplySourceRadius( source, 0.0f );
				alSourcei( source, AL_BUFFER, buffer );
				alSourcef( source, AL_GAIN, 0.1f );
				alSourcei( source, AL_SOURCE_RELATIVE, AL_TRUE );
				alSource3f( source, AL_POSITION, 0.0f, 0.0f, 0.0f );
				alSource3f( source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
				alSourcePlay( source );

				if( CheckALErrors() == AL_NO_ERROR )
				{
					for( int attempt = 0; attempt < 10; ++attempt )
					{
						alGetSourcei( source, AL_SOURCE_STATE, &state );
						if( CheckALErrors() != AL_NO_ERROR || state == AL_PLAYING )
						{
							break;
						}
						Sys_Sleep( 10 );
					}
					passed = state == AL_PLAYING;
					if( passed )
					{
						Sys_Sleep( 100 );
					}
				}
			}
		}
	}

	if( source != 0 )
	{
		alSourceStop( source );
		alSourcei( source, AL_BUFFER, 0 );
		alDeleteSources( 1, &source );
	}
	if( buffer != 0 )
	{
		alDeleteBuffers( 1, &buffer );
	}
	CheckALErrors();

	if( restoreContext )
	{
		alcMakeContextCurrent( previousContext );
		CheckALCErrors( device );
	}

	if( passed )
	{
		idLib::Printf( "OpenAL source self-test: source entered %s with generated PCM16 mono playback.\n", openQ4_OpenALSourceStateName( state ) );
	}
	else
	{
		idLib::Warning( "OpenAL source self-test failed: source state %s.", openQ4_OpenALSourceStateName( state ) );
	}
}

void openALSimulateDeviceEvent_f( const idCmdArgs& args )
{
	int eventFlags = 0;
	if( args.Argc() <= 1 )
	{
		eventFlags = OPENQ4_OPENAL_DEVICE_EVENT_ALL;
	}
	else
	{
		for( int i = 1; i < args.Argc(); ++i )
		{
			const char* eventName = args.Argv( i );
			if( idStr::Icmp( eventName, "all" ) == 0 )
			{
				eventFlags |= OPENQ4_OPENAL_DEVICE_EVENT_ALL;
			}
			else if( idStr::Icmp( eventName, "default" ) == 0 || idStr::Icmp( eventName, "defaultChanged" ) == 0 )
			{
				eventFlags |= OPENQ4_OPENAL_DEVICE_EVENT_DEFAULT_CHANGED;
			}
			else if( idStr::Icmp( eventName, "added" ) == 0 || idStr::Icmp( eventName, "add" ) == 0 )
			{
				eventFlags |= OPENQ4_OPENAL_DEVICE_EVENT_ADDED;
			}
			else if( idStr::Icmp( eventName, "removed" ) == 0 || idStr::Icmp( eventName, "remove" ) == 0 )
			{
				eventFlags |= OPENQ4_OPENAL_DEVICE_EVENT_REMOVED;
			}
			else
			{
				idLib::Printf( "usage: openALSimulateDeviceEvent [default|added|removed|all]\n" );
				return;
			}
		}
	}

	idStr eventNames;
	openQ4_AppendDeviceEventNames( eventNames, eventFlags );
	openQ4_QueuePendingDeviceEventFlags( eventFlags );
	idLib::Printf( "Queued synthetic OpenAL playback device event: %s\n", eventNames.Length() > 0 ? eventNames.c_str() : "unknown" );
}

void openALSimulateDefaultDeviceChange_f( const idCmdArgs& args )
{
	const char* previousDefaultDeviceName = ( args.Argc() > 1 ) ? args.Args( 1, -1 ) : NULL;
	soundSystemLocal.hardware.QueueSyntheticDefaultDeviceChange( previousDefaultDeviceName );
}

void openALSimulateDeviceDisconnect_f( const idCmdArgs& args )
{
	soundSystemLocal.hardware.QueueSyntheticActiveDeviceDisconnect();
}

void listDevices_f( const idCmdArgs& args )
{
	idStrList deviceNames;
	idStr defaultDeviceName;
	idSoundHardware_OpenAL::GetAvailablePlaybackDevices( deviceNames, defaultDeviceName );

	idLib::Printf( "Available playback devices:\n" );
	if( deviceNames.Num() == 0 ) {
		idLib::Printf( "    !!! none !!!\n" );
	} else {
		for( int i = 0; i < deviceNames.Num(); ++i ) {
			idLib::Printf( "    %s\n", deviceNames[i].c_str() );
		}
	}

	//idLib::Printf("Available capture devices:\n");
	//printDeviceList(alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER));

	idLib::Printf( "Default playback device: %s\n", defaultDeviceName.Length() ? defaultDeviceName.c_str() : "<unknown>" );
	const idStr requestedDeviceName = idSoundHardware_OpenAL::IsDefaultDeviceChoiceValue( s_deviceName.GetString() ) ? "" : s_deviceName.GetString();
	idLib::Printf( "Requested playback device: %s\n", requestedDeviceName.Length() ? requestedDeviceName.c_str() : "<system default>" );

	//idLib::Printf("Default capture device: %s\n", alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER));

	idSoundHardware_OpenAL::PrintALCInfo( NULL );

	idSoundHardware_OpenAL::PrintALCInfo( ( ALCdevice* )soundSystem->GetOpenALDevice() );
}

/*
========================
idSoundHardware_OpenAL::Init
========================
*/
void idSoundHardware_OpenAL::Init()
{
	cmdSystem->AddCommand( "listDevices", listDevices_f, 0, "Lists the connected sound devices", NULL );
	cmdSystem->AddCommand( "openALSourceSelfTest", openALSourceSelfTest_f, CMD_FL_SOUND | CMD_FL_CHEAT, "plays a generated tone through a temporary OpenAL source", NULL );
	cmdSystem->AddCommand( "openALSimulateDeviceEvent", openALSimulateDeviceEvent_f, CMD_FL_SOUND | CMD_FL_CHEAT, "queues a synthetic OpenAL playback device event", NULL );
	cmdSystem->AddCommand( "openALSimulateDefaultDeviceChange", openALSimulateDefaultDeviceChange_f, CMD_FL_SOUND | CMD_FL_CHEAT, "queues a synthetic OpenAL system default playback device change", NULL );
	cmdSystem->AddCommand( "openALSimulateDeviceDisconnect", openALSimulateDeviceDisconnect_f, CMD_FL_SOUND | CMD_FL_CHEAT, "queues a synthetic OpenAL active playback device disconnect", NULL );

	common->Printf( "Setup OpenAL device and context... " );

	if( IsDefaultDeviceChoiceValue( s_deviceName.GetString() ) ) {
		s_deviceName.SetString( "" );
	}

	const idStr requestedDeviceName = NormalizeRequestedDeviceName( s_deviceName.GetString() );
	if( requestedDeviceName.Length() > 0 )
	{
		openalDevice = alcOpenDevice( requestedDeviceName.c_str() );
		if( openalDevice == NULL )
		{
			common->Warning( "OpenAL device '%s' unavailable, falling back to the system default device.", requestedDeviceName.c_str() );
		}
	}

	if( openalDevice == NULL )
	{
		openalDevice = alcOpenDevice( NULL );
	}
	if( openalDevice == NULL )
	{
		common->Printf( "Failed.\n" );
		common->Warning( "idSoundHardware_OpenAL::Init: alcOpenDevice() failed; continuing without sound (s_noSound 1)." );
		cvarSystem->SetCVarBool( "s_noSound", true );
		return;
	}

	openQ4_ApplyHrtfPreference( openalDevice );

	ALCint openalContextAttributes[ 3 ];
	const ALCint* requestedContextAttributes = openQ4_BuildOutputModeContextAttributes( openalDevice, openalContextAttributes );
	openalContext = alcCreateContext( openalDevice, requestedContextAttributes );
	if( openalContext == NULL && requestedContextAttributes != NULL )
	{
		CheckALCErrors( openalDevice );
		common->Warning( "idSoundHardware_OpenAL::Init: alcCreateContext() rejected requested OpenAL output mode; retrying with runtime default." );
		openalContext = alcCreateContext( openalDevice, NULL );
	}
	if( openalContext == NULL )
	{
		common->Printf( "Failed.\n" );
		common->Warning( "idSoundHardware_OpenAL::Init: alcCreateContext() failed; continuing without sound (s_noSound 1)." );
		alcCloseDevice( openalDevice );
		openalDevice = NULL;
		cvarSystem->SetCVarBool( "s_noSound", true );
		return;
	}

	if( alcMakeContextCurrent( openalContext ) == 0 )
	{
		common->Printf( "Failed.\n" );
		common->Warning( "idSoundHardware_OpenAL::Init: alcMakeContextCurrent() failed; continuing without sound (s_noSound 1)." );
		alcDestroyContext( openalContext );
		openalContext = NULL;
		alcCloseDevice( openalDevice );
		openalDevice = NULL;
		cvarSystem->SetCVarBool( "s_noSound", true );
		return;
	}

	common->Printf( "Done.\n" );

	ALCint alcMajor = 0;
	ALCint alcMinor = 0;
	bool openALVersionSupported = true;
	alcGetIntegerv( openalDevice, ALC_MAJOR_VERSION, 1, &alcMajor );
	alcGetIntegerv( openalDevice, ALC_MINOR_VERSION, 1, &alcMinor );
	if( CheckALCErrors( openalDevice ) == ALC_NO_ERROR )
	{
		common->Printf( "OpenAL ALC version: %d.%d\n", alcMajor, alcMinor );
		if( alcMajor < 1 || ( alcMajor == 1 && alcMinor < 1 ) )
		{
			openALVersionSupported = false;
			common->Warning( "OpenAL runtime reports unsupported ALC version %d.%d (expected 1.1+).", alcMajor, alcMinor );
		}
	}

	common->Printf( "OpenAL vendor: %s\n", alGetString( AL_VENDOR ) );
	common->Printf( "OpenAL renderer: %s\n", alGetString( AL_RENDERER ) );
	common->Printf( "OpenAL version: %s\n", alGetString( AL_VERSION ) );
	common->Printf( "OpenAL extensions: %s\n", alGetString( AL_EXTENSIONS ) );

	deferredUpdatesAvailable = alIsExtensionPresent( "AL_SOFT_deferred_updates" ) == AL_TRUE && openQ4_LoadDeferredUpdateProcs();
	deferredUpdatesActive = false;
	deferredUpdatesDisableLogged = false;
	if( deferredUpdatesAvailable )
	{
		common->Printf( "OpenAL deferred source updates enabled.\n" );
	}
	if( idSoundVoice_OpenAL::SourceLatencyQueriesAvailable() )
	{
		common->Printf( "OpenAL source latency queries enabled.\n" );
	}
	InitCallbackBufferSupport();
#if OPENQ4_OPENAL_SOURCE_RADIUS_SUPPORTED
	sourceRadiusAvailable = alIsExtensionPresent( "AL_EXT_SOURCE_RADIUS" ) == AL_TRUE;
	sourceRadiusWarningIssued = false;
	if( sourceRadiusAvailable )
	{
		common->Printf( "OpenAL source-radius spatialization enabled.\n" );
	}
#else
	sourceRadiusAvailable = false;
	sourceRadiusWarningIssued = false;
#endif
	InitSourceResampler();

	CaptureOpenedDeviceState( requestedDeviceName.c_str() );
	reopenDeviceAvailable = alcIsExtensionPresent( openalDevice, "ALC_SOFT_reopen_device" ) == ALC_TRUE && openQ4_LoadReopenDeviceProc( openalDevice );
	if( reopenDeviceAvailable )
	{
		common->Printf( "OpenAL in-place device reopen available.\n" );
	}
	EnableDeviceEventMonitoring();
	if( openedRequestedDeviceName.Length() == 0 ) {
		common->Printf( "OpenAL requested device: system default\n" );
	} else {
		common->Printf( "OpenAL requested device: %s\n", openedRequestedDeviceName.c_str() );
	}
	if( openedDefaultDeviceName.Length() > 0 ) {
		common->Printf( "OpenAL default device: %s\n", openedDefaultDeviceName.c_str() );
	}
	if( openedActiveDeviceName.Length() > 0 ) {
		common->Printf( "OpenAL active device: %s\n", openedActiveDeviceName.c_str() );
	}
	if( openedWithDefaultFallback ) {
		common->Warning( "OpenAL requested device '%s' is unavailable; using '%s' until it returns.", openedRequestedDeviceName.c_str(), openedActiveDeviceName.c_str() );
	}
	openQ4_ReportHrtfStatus( openalDevice );
	openQ4_ReportOutputMode( openalDevice );

	efxEnabled = false;
	efxFiltersAvailable = false;
	auxEffectSlot = 0;
	auxReverbEffect = 0;
#if OPENQ4_OPENAL_EFX_SUPPORTED
	const bool efxExtensionPresent = openALVersionSupported && alcIsExtensionPresent( openalDevice, "ALC_EXT_EFX" ) == AL_TRUE;
	efxFiltersAvailable = efxExtensionPresent;
	if( efxFiltersAvailable )
	{
		common->Printf( "OpenAL EFX filters available.\n" );
	}
	if( s_useEAXReverb.GetBool() && efxExtensionPresent && openQ4_LoadHardwareEfxProcs() )
	{
		qalGenEffects( 1, &auxReverbEffect );
		if( CheckALErrors() == AL_NO_ERROR && auxReverbEffect != 0 )
		{
#if defined( AL_EFFECT_EAXREVERB )
			qalEffecti( auxReverbEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
			if( CheckALErrors() != AL_NO_ERROR )
#endif
			{
#if defined( AL_EFFECT_REVERB )
				qalEffecti( auxReverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
				CheckALErrors();
#endif
			}

			qalGenAuxiliaryEffectSlots( 1, &auxEffectSlot );
			if( CheckALErrors() == AL_NO_ERROR && auxEffectSlot != 0 )
			{
				qalAuxiliaryEffectSloti( auxEffectSlot, AL_EFFECTSLOT_EFFECT, auxReverbEffect );
				if( CheckALErrors() == AL_NO_ERROR )
				{
					efxEnabled = true;
					common->Printf( "OpenAL EFX reverb send enabled.\n" );
				}
			}
		}

		if( !efxEnabled )
		{
			if( auxEffectSlot != 0 )
			{
				qalDeleteAuxiliaryEffectSlots( 1, &auxEffectSlot );
				auxEffectSlot = 0;
			}
			if( auxReverbEffect != 0 )
			{
				qalDeleteEffects( 1, &auxReverbEffect );
				auxReverbEffect = 0;
			}
			CheckALErrors();
			common->Warning( "OpenAL EFX requested but unavailable; wet send disabled." );
		}
	}
	else if( s_useEAXReverb.GetBool() && !openALVersionSupported )
	{
		common->Warning( "OpenAL EFX requested, but the runtime OpenAL version is older than 1.1; wet send disabled." );
	}
	else if( s_useEAXReverb.GetBool() && efxExtensionPresent )
	{
		common->Warning( "OpenAL EFX extension reported but required EFX entry points are missing; wet send disabled." );
	}
	else if( s_useEAXReverb.GetBool() )
	{
		common->Warning( "OpenAL EFX extension not reported by device; wet send disabled." );
	}
#else
	if( s_useEAXReverb.GetBool() )
	{
		common->Warning( "OpenAL EFX requested but this build does not expose EFX symbols; wet send disabled." );
	}
#endif

	//outputChannels = deviceDetails.OutputFormat.Format.nChannels;
	//channelMask = deviceDetails.OutputFormat.dwChannelMask;

	//idSoundVoice::InitSurround( outputChannels, channelMask );

#if defined(USE_DOOMCLASSIC)
	// ---------------------
	// Initialize the Doom classic sound system.
	// ---------------------
	I_InitSoundHardware( voices.Max(), 0 );
#endif

	// ---------------------
	// Create VU Meter Effect
	// ---------------------
	/*
	IUnknown* vuMeter = NULL;
	XAudio2CreateVolumeMeter( &vuMeter, 0 );

	XAUDIO2_EFFECT_DESCRIPTOR descriptor;
	descriptor.InitialState = true;
	descriptor.OutputChannels = outputChannels;
	descriptor.pEffect = vuMeter;

	XAUDIO2_EFFECT_CHAIN chain;
	chain.EffectCount = 1;
	chain.pEffectDescriptors = &descriptor;

	pMasterVoice->SetEffectChain( &chain );

	vuMeter->Release();
	*/

	// ---------------------
	// Create VU Meter Graph
	// ---------------------

	/*
	vuMeterRMS = console->CreateGraph( outputChannels );
	vuMeterPeak = console->CreateGraph( outputChannels );
	vuMeterRMS->Enable( false );
	vuMeterPeak->Enable( false );

	memset( vuMeterPeakTimes, 0, sizeof( vuMeterPeakTimes ) );

	vuMeterPeak->SetFillMode( idDebugGraph::GRAPH_LINE );
	vuMeterPeak->SetBackgroundColor( idVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );

	vuMeterRMS->AddGridLine( 0.500f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
	vuMeterRMS->AddGridLine( 0.250f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );
	vuMeterRMS->AddGridLine( 0.125f, idVec4( 0.5f, 0.5f, 0.5f, 1.0f ) );

	const char* channelNames[] = { "L", "R", "C", "S", "Lb", "Rb", "Lf", "Rf", "Cb", "Ls", "Rs" };
	for( int i = 0, ci = 0; ci < sizeof( channelNames ) / sizeof( channelNames[0] ); ci++ )
	{
		if( ( channelMask & BIT( ci ) ) == 0 )
		{
			continue;
		}
		vuMeterRMS->SetLabel( i, channelNames[ ci ] );
		i++;
	}
	*/

	// OpenAL doesn't really impose a maximum number of sources
	voices.SetNum( voices.Max() );
	freeVoices.SetNum( voices.Max() );
	zombieVoices.SetNum( 0 );
	for( int i = 0; i < voices.Num(); i++ )
	{
		freeVoices[i] = &voices[i];
	}
}

/*
========================
idSoundHardware_OpenAL::Shutdown
========================
*/
void idSoundHardware_OpenAL::Shutdown()
{
	EndDeferredUpdates();
	DisableDeviceEventMonitoring();

	for( int i = 0; i < voices.Num(); i++ )
	{
		voices[ i ].DestroyInternal();
	}
	voices.Clear();
	freeVoices.Clear();
	zombieVoices.Clear();

	#if OPENQ4_OPENAL_EFX_SUPPORTED
		if( auxEffectSlot != 0 )
		{
			if ( qalAuxiliaryEffectSloti != NULL ) {
				qalAuxiliaryEffectSloti( auxEffectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
			}
			if ( qalDeleteAuxiliaryEffectSlots != NULL ) {
				qalDeleteAuxiliaryEffectSlots( 1, &auxEffectSlot );
			}
			auxEffectSlot = 0;
		}
		if( auxReverbEffect != 0 )
		{
			if ( qalDeleteEffects != NULL ) {
				qalDeleteEffects( 1, &auxReverbEffect );
			}
			auxReverbEffect = 0;
		}
	#endif
	efxEnabled = false;
	efxFiltersAvailable = false;

#if defined(USE_DOOMCLASSIC)
	// ---------------------
	// Shutdown the Doom classic sound system.
	// ---------------------
	I_ShutdownSoundHardware();
#endif

	alcMakeContextCurrent( NULL );

	alcDestroyContext( openalContext );
	openalContext = NULL;

	alcCloseDevice( openalDevice );
	openalDevice = NULL;
	lastDeviceCheckTime = 0;
	pendingDeviceEventFlags = 0;
	pendingDeviceEventCheckTime = 0;
	lastDeviceRecoveryTime = 0;
	pendingSyntheticActiveDeviceDisconnect = false;
	lastPerfPrintTime = 0;
	reopenDeviceAvailable = false;
	deferredUpdatesAvailable = false;
	deferredUpdatesActive = false;
	deferredUpdatesDisableLogged = false;
	sourceResamplerAvailable = false;
	sourceResamplerWarningIssued = false;
	sourceResamplerCount = 0;
	sourceResamplerDefault = OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT;
	sourceResamplerSelected = OPENQ4_OPENAL_RESAMPLER_RUNTIME_DEFAULT;
	sourceRadiusAvailable = false;
	sourceRadiusWarningIssued = false;
	callbackBufferAvailable = false;
	callbackBufferWarningIssued = false;
	openedWithDefaultFallback = false;
	openedHrtfMode = OPENQ4_OPENAL_HRTF_AUTO;
	openedSpeakerCount = OPENQ4_OPENAL_SPEAKERS_SURROUND;
	openedRequestedDeviceName.Clear();
	openedActiveDeviceName.Clear();
	openedDefaultDeviceName.Clear();

	/*
	if( vuMeterRMS != NULL )
	{
		console->DestroyGraph( vuMeterRMS );
		vuMeterRMS = NULL;
	}
	if( vuMeterPeak != NULL )
	{
		console->DestroyGraph( vuMeterPeak );
		vuMeterPeak = NULL;
	}
	*/
}

/*
========================
idSoundHardware_OpenAL::AllocateVoice
========================
*/
idSoundVoice* idSoundHardware_OpenAL::AllocateVoice( const idSoundSample* leadinSample, const idSoundSample* loopingSample )
{
	if( leadinSample == NULL )
	{
		return NULL;
	}
	if( loopingSample != NULL )
	{
		if( ( leadinSample->format.basic.formatTag != loopingSample->format.basic.formatTag ) || ( leadinSample->format.basic.numChannels != loopingSample->format.basic.numChannels ) )
		{
			idLib::Warning( "Leadin/looping format mismatch: %s & %s", leadinSample->GetName(), loopingSample->GetName() );
			loopingSample = NULL;
		}
	}

	// Try to find a free voice that matches the format
	// But fallback to the last free voice if none match the format
	idSoundVoice* voice = NULL;
	for( int i = 0; i < freeVoices.Num(); i++ )
	{
		if( freeVoices[i]->IsPlaying() )
		{
			continue;
		}
		voice = ( idSoundVoice* )freeVoices[i];
		if( voice->CompatibleFormat( ( idSoundSample_OpenAL* )leadinSample ) )
		{
			break;
		}
	}
	if( voice != NULL )
	{
		voice->Create( leadinSample, loopingSample );
		freeVoices.Remove( voice );
		return voice;
	}

	return NULL;
}

/*
========================
idSoundHardware_OpenAL::FreeVoice
========================
*/
void idSoundHardware_OpenAL::FreeVoice( idSoundVoice* voice )
{
	voice->Stop();

	// Stop() is asyncronous, so we won't flush bufferes until the
	// voice on the zombie channel actually returns !IsPlaying()
	zombieVoices.Append( voice );
}

/*
========================
idSoundHardware_OpenAL::PrintPerformanceData
========================
*/
void idSoundHardware_OpenAL::PrintPerformanceData()
{
	if( !s_showPerfData.GetBool() )
	{
		return;
	}

	const int nowTime = Sys_Milliseconds();
	if( lastPerfPrintTime + 1000 > nowTime )
	{
		return;
	}
	lastPerfPrintTime = nowTime;

	int activeVoices = 0;
	const int leasedVoices = Max( 0, voices.Num() - freeVoices.Num() - zombieVoices.Num() );
	int openSources = 0;
	int playingSources = 0;
	int pausedSources = 0;
	int initialSources = 0;
	int stoppedSources = 0;
	int otherSources = 0;
	int staticPlaybackVoices = 0;
	int streamingPlaybackVoices = 0;
	int pendingPlaybackVoices = 0;
	int latencyVoices = 0;
	float totalLatencyMS = 0.0f;
	float maxLatencyMS = 0.0f;

	for( int i = 0; i < voices.Num(); i++ )
	{
		idSoundVoice_OpenAL& voice = voices[i];
		if( voice.playbackMode == idSoundVoice_OpenAL::OPENQ4_OPENAL_PLAYBACK_STREAMING )
		{
			streamingPlaybackVoices++;
		}
		else if( voice.playbackMode == idSoundVoice_OpenAL::OPENQ4_OPENAL_PLAYBACK_STATIC_ONESHOT ||
				 voice.playbackMode == idSoundVoice_OpenAL::OPENQ4_OPENAL_PLAYBACK_STATIC_LEADIN ||
				 voice.playbackMode == idSoundVoice_OpenAL::OPENQ4_OPENAL_PLAYBACK_STATIC_LOOP )
		{
			staticPlaybackVoices++;
		}
		else if( voice.currentSample != NULL || voice.leadinSample != NULL )
		{
			pendingPlaybackVoices++;
		}

		if( !alIsSource( voice.openalSource ) )
		{
			continue;
		}

		openSources++;
		ALint state = AL_INITIAL;
		alGetSourcei( voice.openalSource, AL_SOURCE_STATE, &state );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			otherSources++;
			continue;
		}

		switch( state )
		{
			case AL_PLAYING:
				playingSources++;
				activeVoices++;
				break;
			case AL_PAUSED:
				pausedSources++;
				activeVoices++;
				break;
			case AL_INITIAL:
				initialSources++;
				break;
			case AL_STOPPED:
				stoppedSources++;
				break;
			default:
				otherSources++;
				break;
		}

		float offsetMS = 0.0f;
		float latencyMS = 0.0f;
		if( ( state == AL_PLAYING || state == AL_PAUSED ) && voice.GetPlaybackLatencyMS( offsetMS, latencyMS ) )
		{
			latencyVoices++;
			totalLatencyMS += latencyMS;
			maxLatencyMS = Max( maxLatencyMS, latencyMS );
		}
	}

	if( latencyVoices > 0 )
	{
		common->Printf( "OpenAL perf: active voices %d/%d, leased %d, free %d, zombies %d, sources %d states playing %d paused %d initial %d stopped %d other %d modes static %d streaming %d pending %d, output latency avg %.2f ms max %.2f ms\n",
			activeVoices,
			voices.Num(),
			leasedVoices,
			freeVoices.Num(),
			zombieVoices.Num(),
			openSources,
			playingSources,
			pausedSources,
			initialSources,
			stoppedSources,
			otherSources,
			staticPlaybackVoices,
			streamingPlaybackVoices,
			pendingPlaybackVoices,
			totalLatencyMS / latencyVoices,
			maxLatencyMS );
	}
	else
	{
		common->Printf( "OpenAL perf: active voices %d/%d, leased %d, free %d, zombies %d, sources %d states playing %d paused %d initial %d stopped %d other %d modes static %d streaming %d pending %d, source latency unavailable\n",
			activeVoices,
			voices.Num(),
			leasedVoices,
			freeVoices.Num(),
			zombieVoices.Num(),
			openSources,
			playingSources,
			pausedSources,
			initialSources,
			stoppedSources,
			otherSources,
			staticPlaybackVoices,
			streamingPlaybackVoices,
			pendingPlaybackVoices );
	}

	PrintDiagnosticCounters();
}

/*
========================
idSoundHardware_OpenAL::Update
========================
*/
void idSoundHardware_OpenAL::Update()
{
	if( openalDevice == NULL )
	{
		int nowTime = Sys_Milliseconds();
		if( lastResetTime + 1000 < nowTime )
		{
			lastResetTime = nowTime;
			Init();
		}
		return;
	}

	if( UpdateDeviceMonitoring() ) {
		return;
	}

	if( soundSystem->IsMuted() )
	{
		alListenerf( AL_GAIN, 0.0f );
	}
	else
	{
		alListenerf( AL_GAIN, 1.0f );
	}

	// IXAudio2SourceVoice::Stop() has been called for every sound on the
	// zombie list, but it is documented as asyncronous, so we have to wait
	// until it actually reports that it is no longer playing.
	for( int i = 0; i < zombieVoices.Num(); i++ )
	{
		zombieVoices[i]->FlushSourceBuffers();
		if( !zombieVoices[i]->IsPlaying() )
		{
			freeVoices.Append( zombieVoices[i] );
			zombieVoices.RemoveIndex( i );
			i--;
		}
		else
		{
			static int playingZombies;
			playingZombies++;
		}
	}

	PrintPerformanceData();

	/*
	if( s_showPerfData.GetBool() )
	{
		XAUDIO2_PERFORMANCE_DATA perfData;
		pXAudio2->GetPerformanceData( &perfData );
		idLib::Printf( "Voices: %d/%d CPU: %.2f%% Mem: %dkb\n", perfData.ActiveSourceVoiceCount, perfData.TotalSourceVoiceCount, perfData.AudioCyclesSinceLastQuery / ( float )perfData.TotalCyclesSinceLastQuery, perfData.MemoryUsageInBytes / 1024 );
	}
	*/

	/*
	if( vuMeterRMS == NULL )
	{
		// Init probably hasn't been called yet
		return;
	}

	vuMeterRMS->Enable( s_showLevelMeter.GetBool() );
	vuMeterPeak->Enable( s_showLevelMeter.GetBool() );

	if( !s_showLevelMeter.GetBool() )
	{
		pMasterVoice->DisableEffect( 0 );
		return;
	}
	else
	{
		pMasterVoice->EnableEffect( 0 );
	}

	float peakLevels[ 8 ];
	float rmsLevels[ 8 ];

	XAUDIO2FX_VOLUMEMETER_LEVELS levels;
	levels.ChannelCount = outputChannels;
	levels.pPeakLevels = peakLevels;
	levels.pRMSLevels = rmsLevels;

	if( levels.ChannelCount > 8 )
	{
		levels.ChannelCount = 8;
	}

	pMasterVoice->GetEffectParameters( 0, &levels, sizeof( levels ) );

	int currentTime = Sys_Milliseconds();
	for( int i = 0; i < outputChannels; i++ )
	{
		if( vuMeterPeakTimes[i] < currentTime )
		{
			vuMeterPeak->SetValue( i, vuMeterPeak->GetValue( i ) * 0.9f, colorRed );
		}
	}

	float width = 20.0f;
	float height = 200.0f;
	float left = 100.0f;
	float top = 100.0f;

	sscanf( s_meterPosition.GetString(), "%f %f %f %f", &left, &top, &width, &height );

	vuMeterRMS->SetPosition( left, top, width * levels.ChannelCount, height );
	vuMeterPeak->SetPosition( left, top, width * levels.ChannelCount, height );

	for( uint32 i = 0; i < levels.ChannelCount; i++ )
	{
		vuMeterRMS->SetValue( i, rmsLevels[ i ], idVec4( 0.5f, 1.0f, 0.0f, 1.00f ) );
		if( peakLevels[ i ] >= vuMeterPeak->GetValue( i ) )
		{
			vuMeterPeak->SetValue( i, peakLevels[ i ], colorRed );
			vuMeterPeakTimes[i] = currentTime + s_meterTopTime.GetInteger();
		}
	}
	*/
}



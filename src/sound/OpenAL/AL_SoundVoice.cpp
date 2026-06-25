/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013 Robert Beckebans

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

extern idCVar s_warnOnMissingSamples;
extern idCVar s_openALEfxDebugMode;

#if defined( AL_AUXILIARY_SEND_FILTER ) && defined( AL_DIRECT_FILTER ) && defined( AL_FILTER_LOWPASS ) && defined( AL_EFFECTSLOT_NULL ) && defined( AL_FILTER_NULL )
	#define OPENQ4_OPENAL_EFX_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_EFX_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_EFX_SUPPORTED
typedef void ( AL_APIENTRY *openq4_alGenFilters_t )( ALsizei n, ALuint *filters );
typedef void ( AL_APIENTRY *openq4_alDeleteFilters_t )( ALsizei n, const ALuint *filters );
typedef void ( AL_APIENTRY *openq4_alFilteri_t )( ALuint filter, ALenum param, ALint iValue );
typedef void ( AL_APIENTRY *openq4_alFilterf_t )( ALuint filter, ALenum param, ALfloat flValue );

static openq4_alGenFilters_t qalGenFilters = NULL;
static openq4_alDeleteFilters_t qalDeleteFilters = NULL;
static openq4_alFilteri_t qalFilteri = NULL;
static openq4_alFilterf_t qalFilterf = NULL;

static bool openQ4_LoadVoiceEfxProcs() {
	static bool initialized = false;
	static bool available = false;

	if ( initialized ) {
		return available;
	}
	initialized = true;

	qalGenFilters = reinterpret_cast<openq4_alGenFilters_t>( alGetProcAddress( "alGenFilters" ) );
	qalDeleteFilters = reinterpret_cast<openq4_alDeleteFilters_t>( alGetProcAddress( "alDeleteFilters" ) );
	qalFilteri = reinterpret_cast<openq4_alFilteri_t>( alGetProcAddress( "alFilteri" ) );
	qalFilterf = reinterpret_cast<openq4_alFilterf_t>( alGetProcAddress( "alFilterf" ) );

	available =
		( qalGenFilters != NULL ) &&
		( qalDeleteFilters != NULL ) &&
		( qalFilteri != NULL ) &&
		( qalFilterf != NULL );
	return available;
}
#endif

#if defined( AL_SEC_OFFSET_LATENCY_SOFT )
	#define OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED 1
#else
	#define OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED 0
#endif

#if OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED
static LPALGETSOURCEDVSOFT qalGetSourcedvSOFT = NULL;
#endif

idCVar s_skipHardwareSets( "s_skipHardwareSets", "0", CVAR_BOOL, "Do all calculation, but skip XA2 calls" );
idCVar s_debugHardware( "s_debugHardware", "0", CVAR_BOOL, "Print a message any time a hardware voice changes" );
static idCVar s_openALForcePCMQueue( "s_openALForcePCMQueue", "0", CVAR_BOOL, "force decoded PCM samples through queued OpenAL buffers for diagnostics" );

// The whole system runs at this sample rate
static int SYSTEM_SAMPLE_RATE = 44100;
static const int OPENQ4_OPENAL_STREAMING_CHUNK_MSEC = 100;
static float ONE_OVER_SYSTEM_SAMPLE_RATE = 1.0f / SYSTEM_SAMPLE_RATE;

static const float OPENQ4_OPENAL_PORTAL_DIRECT_ATTENUATION_DB = -8.0f;
static const float OPENQ4_OPENAL_PORTAL_DIRECT_HF_ATTENUATION_DB = -24.0f;
static const float OPENQ4_OPENAL_PORTAL_WET_ATTENUATION_DB = -3.0f;
static const float OPENQ4_OPENAL_PORTAL_WET_HF_ATTENUATION_DB = -10.0f;
static const float OPENQ4_OPENAL_ENV_DIRECT_ATTENUATION_DB = -2.0f;
static const float OPENQ4_OPENAL_ENV_DIRECT_HF_ATTENUATION_DB = -16.0f;
static const float OPENQ4_OPENAL_ENV_WET_ATTENUATION_DB = -1.0f;
static const float OPENQ4_OPENAL_ENV_WET_HF_ATTENUATION_DB = -12.0f;

struct openQ4OcclusionFilter_t
{
	float directGain;
	float directGainHF;
	float wetGain;
	float wetGainHF;
};

static float OpenQ4_DBToClampedGain( const float db )
{
	return idMath::ClampFloat( 0.0f, 1.0f, DBtoLinear( db ) );
}

static openQ4OcclusionFilter_t OpenQ4_BuildOcclusionFilter( const float occlusion, const float environmentMuffle )
{
	const float clampedOcclusion = idMath::ClampFloat( 0.0f, 1.0f, occlusion );
	const float clampedEnvironmentMuffle = idMath::ClampFloat( 0.0f, 1.0f, environmentMuffle );

	openQ4OcclusionFilter_t filter;
	filter.directGain = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_DIRECT_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_DIRECT_ATTENUATION_DB * clampedEnvironmentMuffle );
	filter.directGainHF = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_DIRECT_HF_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_DIRECT_HF_ATTENUATION_DB * clampedEnvironmentMuffle );
	filter.wetGain = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_WET_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_WET_ATTENUATION_DB * clampedEnvironmentMuffle );
	filter.wetGainHF = OpenQ4_DBToClampedGain(
		OPENQ4_OPENAL_PORTAL_WET_HF_ATTENUATION_DB * clampedOcclusion +
		OPENQ4_OPENAL_ENV_WET_HF_ATTENUATION_DB * clampedEnvironmentMuffle );
	return filter;
}


/*
========================
idSoundVoice_OpenAL::idSoundVoice_OpenAL
========================
*/
idSoundVoice_OpenAL::idSoundVoice_OpenAL()
	:
	triggered( false ),
	openalSource( 0 ),
	openalStreamingOffset( 0 ),
	openalDirectFilter( 0 ),
	openalAuxFilter( 0 ),
	efxRoutingAttached( false ),
	leadinSample( NULL ),
	loopingSample( NULL ),
	currentSample( NULL ),
	playbackMode( OPENQ4_OPENAL_PLAYBACK_NONE ),
	streamingSample( NULL ),
	streamingBufferNumber( 0 ),
	streamingBufferOffset( 0 ),
	streamingEndOfStream( false ),
	formatTag( 0 ),
	numChannels( 0 ),
	sourceVoiceRate( 0 ),
	sampleRate( 0 ),
	hasVUMeter( false ),
	paused( true )
{
	memset( openalStreamingBuffer, 0, sizeof( openalStreamingBuffer ) );
	memset( lastopenalStreamingBuffer, 0, sizeof( lastopenalStreamingBuffer ) );
}

/*
========================
idSoundVoice_OpenAL::~idSoundVoice_OpenAL
========================
*/
idSoundVoice_OpenAL::~idSoundVoice_OpenAL()
{
	DestroyInternal();
}

/*
========================
idSoundVoice_OpenAL::SetInnerRadius
========================
*/
void idSoundVoice_OpenAL::SetInnerRadius( float r )
{
	idSoundVoice_Base::SetInnerRadius( r );
	soundSystemLocal.hardware.ApplySourceRadius( openalSource, r );
}

/*
========================
idSoundVoice_OpenAL::CompatibleFormat
========================
*/
bool idSoundVoice_OpenAL::CompatibleFormat( idSoundSample_OpenAL* s )
{
	if( alIsSource( openalSource ) )
	{
		// If this voice has never been allocated, then it's compatible with everything
		return true;
	}

	return false;
}

/*
========================
idSoundVoice_OpenAL::Create
========================
*/
void idSoundVoice_OpenAL::Create( const idSoundSample* leadinSample_, const idSoundSample* loopingSample_ )
{
	if( IsPlaying() )
	{
		// This should never hit
		Stop();
		return;
	}

	triggered = true;

	leadinSample = ( idSoundSample_OpenAL* )leadinSample_;
	loopingSample = ( idSoundSample_OpenAL* )loopingSample_;
	currentSample = NULL;
	playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
	ResetStreamingState();

	if( alIsSource( openalSource ) )
	{
		FlushSourceBuffers();
	}

	if( leadinSample != NULL && leadinSample->openalBuffer == 0 )
	{
		leadinSample->CreateOpenALBuffer();
	}
	if( loopingSample != NULL && loopingSample != leadinSample && loopingSample->openalBuffer == 0 )
	{
		loopingSample->CreateOpenALBuffer();
	}

	if( alIsSource( openalSource ) && CompatibleFormat( leadinSample ) )
	{
		sampleRate = leadinSample->format.basic.samplesPerSec;
	}
	else
	{
		DestroyInternal();
		formatTag = leadinSample->format.basic.formatTag;
		numChannels = leadinSample->format.basic.numChannels;
		sampleRate = leadinSample->format.basic.samplesPerSec;

		//soundSystemLocal.hardware.pXAudio2->CreateSourceVoice( &pSourceVoice, ( const WAVEFORMATEX* )&leadinSample->format, XAUDIO2_VOICE_USEFILTER, 4.0f, &streamContext );

		CheckALErrors();

		alGenSources( 1, &openalSource );
		if( CheckALErrors() != AL_NO_ERROR )
			//if( pSourceVoice == NULL )
		{
			// If this hits, then we are most likely passing an invalid sample format, which should have been caught by the loader (and the sample defaulted)
			return;
		}

		alSourcef( openalSource, AL_ROLLOFF_FACTOR, 0.0f );

		// Static buffers are attached by SubmitBuffer(). Queued fallback buffers
		// are allocated lazily by EnsureStreamingBuffers() so stale buffer names
		// from an old context cannot poison source creation.
		alSourcei( openalSource, AL_BUFFER, 0 );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			DestroyInternal();
			return;
		}

		if( s_debugHardware.GetBool() )
		{
			if( loopingSample == NULL || loopingSample == leadinSample )
			{
				idLib::Printf( "%dms: %i created for %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
			}
			else
			{
				idLib::Printf( "%dms: %i created for %s and %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>", loopingSample ? loopingSample->GetName() : "<null>" );
			}
		}
	}

	sourceVoiceRate = sampleRate;
	//pSourceVoice->SetSourceSampleRate( sampleRate );
	//pSourceVoice->SetVolume( 0.0f );

	soundSystemLocal.hardware.ApplySourceResampler( openalSource );
	alSourcei( openalSource, AL_SOURCE_RELATIVE, AL_TRUE );
	alSource3f( openalSource, AL_POSITION, 0.0f, 0.0f, 0.0f );
	alSource3f( openalSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f );

	// RB: FIXME 0.0f ?
	alSourcef( openalSource, AL_GAIN, 1.0f );
	CreateWetDryFilters();
	ApplyWetDryRouting();

	//OnBufferStart( leadinSample, 0 );
}

/*
========================
idSoundVoice_OpenAL::DestroyInternal
========================
*/
void idSoundVoice_OpenAL::DestroyInternal()
{
	if( alIsSource( openalSource ) )
	{
		if( s_debugHardware.GetBool() )
		{
			idLib::Printf( "%dms: %i destroyed\n", Sys_Milliseconds(), openalSource );
		}

		// Detach buffers before deleting the source to avoid AL_INVALID_NAME on shutdown.
		FlushSourceBuffers();
		alDeleteSources( 1, &openalSource );
		openalSource = 0;

		for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
		{
			if( openalStreamingBuffer[i] != 0 && alIsBuffer( openalStreamingBuffer[i] ) )
			{
				CheckALErrors();
				alDeleteBuffers( 1, &openalStreamingBuffer[i] );
				if( CheckALErrors() == AL_NO_ERROR )
				{
					openalStreamingBuffer[i] = 0;
				}
			}
		}

		for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
		{
			if( lastopenalStreamingBuffer[i] != 0 && alIsBuffer( lastopenalStreamingBuffer[i] ) )
			{
				CheckALErrors();
				alDeleteBuffers( 1, &lastopenalStreamingBuffer[i] );
				if( CheckALErrors() == AL_NO_ERROR )
				{
					lastopenalStreamingBuffer[i] = 0;
				}
			}
		}

		openalStreamingOffset = 0;

		hasVUMeter = false;
	}
	DestroyWetDryFilters();
	currentSample = NULL;
	playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
	ResetStreamingState();
}

/*
========================
idSoundVoice_OpenAL::Start
========================
*/
void idSoundVoice_OpenAL::Start( int offsetMS, int ssFlags )
{
	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i starting %s @ %dms\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>", offsetMS );
	}

	if( !leadinSample )
	{
		return;
	}

	if( !alIsSource( openalSource ) )
	{
		return;
	}

	if( leadinSample->IsDefault() )
	{
		if ( s_warnOnMissingSamples.GetBool() ) {
			idLib::Warning( "Starting defaulted sound sample %s", leadinSample->GetName() );
		}
	}

	bool flicker = ( ssFlags & SSF_NO_FLICKER ) == 0;

	if( flicker != hasVUMeter )
	{
		hasVUMeter = flicker;

		/*
		if( flicker )
		{
			IUnknown* vuMeter = NULL;

			if( XAudio2CreateVolumeMeter( &vuMeter, 0 ) == S_OK )
			{

				XAUDIO2_EFFECT_DESCRIPTOR descriptor;
				descriptor.InitialState = true;
				descriptor.OutputChannels = leadinSample->NumChannels();
				descriptor.pEffect = vuMeter;

				XAUDIO2_EFFECT_CHAIN chain;
				chain.EffectCount = 1;
				chain.pEffectDescriptors = &descriptor;

				pSourceVoice->SetEffectChain( &chain );

				vuMeter->Release();
			}
		}
		else
		{
			pSourceVoice->SetEffectChain( NULL );
		}
		*/
	}

	assert( offsetMS >= 0 );
	int offsetSamples = MsecToSamples( offsetMS, leadinSample->SampleRate() );
	if( loopingSample == NULL && offsetSamples >= leadinSample->playLength )
	{
		return;
	}

	const int submittedBytes = RestartAt( offsetSamples );
	if( submittedBytes <= 0 )
	{
		return;
	}
	Update();
	UnPause();
}

/*
========================
idSoundVoice_OpenAL::RestartAt
========================
*/
int idSoundVoice_OpenAL::RestartAt( int offsetSamples )
{
	offsetSamples &= ~127;

	idSoundSample_OpenAL* sample = leadinSample;
	if( sample == NULL )
	{
		return 0;
	}

	if( offsetSamples >= leadinSample->playLength )
	{
		if( loopingSample != NULL && loopingSample->playLength > 0)
		{
			offsetSamples %= loopingSample->playLength;
			sample = loopingSample;
		}
		else
		{
			return 0;
		}
	}

	if( sample == NULL || sample->playLength <= 0 )
	{
		return 0;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = sample->buffers.Ptr();
	const int numBuffers = sample->buffers.Num();
	if( sampleBuffers == NULL || numBuffers <= 0 || numBuffers > 16384 )
	{
		return 0;
	}

	const size_t sampleBuffersAddress = reinterpret_cast<size_t>( sampleBuffers );
	if( sampleBuffersAddress < 4096 || ( sampleBuffersAddress & ( sizeof( void* ) - 1 ) ) != 0 )
	{
		return 0;
	}

	int previousNumSamples = 0;
	for( int i = 0; i < numBuffers; i++ )
	{
		const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sampleBuffers[i];
		if( sampleBuffer.numSamples > sample->playBegin + offsetSamples )
		{
			return SubmitBuffer( sample, i, sample->playBegin + offsetSamples - previousNumSamples );
		}
		previousNumSamples = sampleBuffer.numSamples;
	}

	return 0;
}

/*
========================
idSoundVoice_OpenAL::ResetStreamingState
========================
*/
void idSoundVoice_OpenAL::ResetStreamingState()
{
	openalStreamingOffset = 0;
	streamingSample = NULL;
	streamingBufferNumber = 0;
	streamingBufferOffset = 0;
	streamingEndOfStream = false;
}

/*
========================
idSoundVoice_OpenAL::EnsureStreamingBuffers
========================
*/
bool idSoundVoice_OpenAL::EnsureStreamingBuffers()
{
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		if( openalStreamingBuffer[i] != 0 && alIsBuffer( openalStreamingBuffer[i] ) )
		{
			continue;
		}

		openalStreamingBuffer[i] = 0;
		CheckALErrors();
		alGenBuffers( 1, &openalStreamingBuffer[i] );
		if( CheckALErrors() != AL_NO_ERROR || openalStreamingBuffer[i] == 0 )
		{
			common->Warning( "OpenAL could not allocate queued PCM fallback buffer." );
			return false;
		}
	}

	return true;
}

/*
========================
idSoundVoice_OpenAL::ValidateStreamingSample
========================
*/
bool idSoundVoice_OpenAL::ValidateStreamingSample( idSoundSample_OpenAL* sample, ALenum& alFormat, int& blockBytes )
{
	alFormat = AL_NONE;
	blockBytes = 0;

	if( sample == NULL || sample->buffers.Num() <= 0 || sample->playLength <= 0 )
	{
		return false;
	}

	if( sample->format.basic.formatTag != idWaveFile::FORMAT_PCM ||
			sample->format.basic.bitsPerSample != 16 ||
			( sample->NumChannels() != 1 && sample->NumChannels() != 2 ) )
	{
		idSoundHardware_OpenAL::CountDiagnosticEvent( idSoundHardware_OpenAL::OPENAL_DIAG_QUEUE_FALLBACK_REFUSALS );
		if( !sample->openalFallbackWarningIssued )
		{
			common->Warning( "OpenAL refused queued PCM fallback for '%s': formatTag=0x%x channels=%d bits=%d is not decoded mono/stereo PCM16.",
				sample->GetName(),
				sample->format.basic.formatTag,
				sample->NumChannels(),
				sample->format.basic.bitsPerSample );
			sample->openalFallbackWarningIssued = true;
		}
		return false;
	}

	blockBytes = sample->format.basic.blockSize;
	if( blockBytes != sample->NumChannels() * ( int )sizeof( int16 ) )
	{
		idSoundHardware_OpenAL::CountDiagnosticEvent( idSoundHardware_OpenAL::OPENAL_DIAG_QUEUE_FALLBACK_REFUSALS );
		if( !sample->openalFallbackWarningIssued )
		{
			common->Warning( "OpenAL refused queued PCM fallback for '%s': invalid PCM block size %d.",
				sample->GetName(),
				blockBytes );
			sample->openalFallbackWarningIssued = true;
		}
		return false;
	}

	alFormat = sample->NumChannels() == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	return true;
}

/*
========================
idSoundVoice_OpenAL::SetStreamingCursor
========================
*/
bool idSoundVoice_OpenAL::SetStreamingCursor( idSoundSample_OpenAL* sample, int absoluteSampleFrame )
{
	if( sample == NULL || sample->buffers.Num() <= 0 || sample->playLength <= 0 )
	{
		streamingEndOfStream = true;
		streamingSample = NULL;
		return false;
	}

	const int beginFrame = sample->playBegin;
	const int endFrame = sample->playBegin + sample->playLength;
	if( absoluteSampleFrame < beginFrame )
	{
		absoluteSampleFrame = beginFrame;
	}
	if( absoluteSampleFrame >= endFrame )
	{
		streamingEndOfStream = true;
		streamingSample = NULL;
		return false;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = sample->buffers.Ptr();
	const int numBuffers = sample->buffers.Num();
	if( sampleBuffers == NULL || numBuffers <= 0 || numBuffers > 16384 )
	{
		streamingEndOfStream = true;
		streamingSample = NULL;
		return false;
	}

	int previousNumSamples = 0;
	for( int i = 0; i < numBuffers; i++ )
	{
		const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sampleBuffers[i];
		if( sampleBuffer.buffer == NULL || sampleBuffer.bufferSize <= 0 || sampleBuffer.numSamples <= previousNumSamples )
		{
			previousNumSamples = sampleBuffer.numSamples;
			continue;
		}
		if( sampleBuffer.numSamples > absoluteSampleFrame )
		{
			streamingSample = sample;
			streamingBufferNumber = i;
			streamingBufferOffset = absoluteSampleFrame - previousNumSamples;
			streamingEndOfStream = false;
			openalStreamingOffset = absoluteSampleFrame;
			return true;
		}
		previousNumSamples = sampleBuffer.numSamples;
	}

	streamingEndOfStream = true;
	streamingSample = NULL;
	return false;
}

/*
========================
idSoundVoice_OpenAL::AdvanceStreamingCursor
========================
*/
bool idSoundVoice_OpenAL::AdvanceStreamingCursor( int queuedFrames )
{
	if( streamingSample == NULL || queuedFrames < 0 )
	{
		streamingEndOfStream = true;
		streamingSample = NULL;
		return false;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = streamingSample->buffers.Ptr();
	const int numBuffers = streamingSample->buffers.Num();
	if( sampleBuffers == NULL || streamingBufferNumber < 0 || streamingBufferNumber >= numBuffers )
	{
		streamingEndOfStream = true;
		streamingSample = NULL;
		return false;
	}

	const int previousNumSamples = streamingBufferNumber > 0 ? sampleBuffers[streamingBufferNumber - 1].numSamples : 0;
	const int nextFrame = previousNumSamples + streamingBufferOffset + queuedFrames;
	const int endFrame = streamingSample->playBegin + streamingSample->playLength;

	if( nextFrame >= endFrame )
	{
		if( streamingSample == leadinSample && loopingSample != NULL )
		{
			return SetStreamingCursor( loopingSample, loopingSample->playBegin );
		}
		if( streamingSample == loopingSample && loopingSample != NULL )
		{
			return SetStreamingCursor( loopingSample, loopingSample->playBegin );
		}

		streamingEndOfStream = true;
		streamingSample = NULL;
		return false;
	}

	if( nextFrame >= sampleBuffers[streamingBufferNumber].numSamples )
	{
		return SetStreamingCursor( streamingSample, nextFrame );
	}

	streamingBufferOffset += queuedFrames;
	openalStreamingOffset = nextFrame;
	return true;
}

/*
========================
idSoundVoice_OpenAL::QueueNextStreamingBuffer
========================
*/
bool idSoundVoice_OpenAL::QueueNextStreamingBuffer( ALuint buffer, int& queuedBytes )
{
	queuedBytes = 0;

	for( int cursorAttempts = 0; cursorAttempts < 16384; cursorAttempts++ )
	{
		if( streamingEndOfStream || streamingSample == NULL )
		{
			return false;
		}

		ALenum alFormat = AL_NONE;
		int blockBytes = 0;
		if( !ValidateStreamingSample( streamingSample, alFormat, blockBytes ) )
		{
			streamingEndOfStream = true;
			streamingSample = NULL;
			return false;
		}

		const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = streamingSample->buffers.Ptr();
		const int numBuffers = streamingSample->buffers.Num();
		if( sampleBuffers == NULL || streamingBufferNumber < 0 || streamingBufferNumber >= numBuffers )
		{
			streamingEndOfStream = true;
			streamingSample = NULL;
			return false;
		}

		const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sampleBuffers[streamingBufferNumber];
		const int previousNumSamples = streamingBufferNumber > 0 ? sampleBuffers[streamingBufferNumber - 1].numSamples : 0;
		const int framesInBuffer = sampleBuffer.numSamples - previousNumSamples;
		const int absoluteFrame = previousNumSamples + streamingBufferOffset;
		const int endFrame = streamingSample->playBegin + streamingSample->playLength;

		if( sampleBuffer.buffer == NULL || sampleBuffer.bufferSize <= 0 || framesInBuffer <= 0 )
		{
			const int nextFrame = framesInBuffer > 0 ? sampleBuffer.numSamples : absoluteFrame + 1;
			if( !SetStreamingCursor( streamingSample, nextFrame ) )
			{
				return false;
			}
			continue;
		}

		if( absoluteFrame < streamingSample->playBegin )
		{
			if( !SetStreamingCursor( streamingSample, streamingSample->playBegin ) )
			{
				return false;
			}
			continue;
		}

		if( absoluteFrame >= endFrame || streamingBufferOffset >= framesInBuffer )
		{
			if( !AdvanceStreamingCursor( 0 ) )
			{
				return false;
			}
			continue;
		}

		const int chunkFrames = Max( 1, ( int )MsecToSamples( OPENQ4_OPENAL_STREAMING_CHUNK_MSEC, streamingSample->SampleRate() ) );
		const int framesToQueue = Min( Min( framesInBuffer - streamingBufferOffset, endFrame - absoluteFrame ), chunkFrames );
		const int bytesToQueue = framesToQueue * blockBytes;
		if( framesToQueue <= 0 || bytesToQueue <= 0 || streamingBufferOffset * blockBytes > sampleBuffer.bufferSize )
		{
			if( !AdvanceStreamingCursor( 0 ) )
			{
				return false;
			}
			continue;
		}

		const byte* const start = reinterpret_cast<const byte*>( sampleBuffer.buffer ) + streamingBufferOffset * blockBytes;
		const int maxBytesFromOffset = sampleBuffer.bufferSize - streamingBufferOffset * blockBytes;
		int clampedBytesToQueue = Min( bytesToQueue, maxBytesFromOffset );
		clampedBytesToQueue -= clampedBytesToQueue % blockBytes;
		if( clampedBytesToQueue <= 0 )
		{
			if( !AdvanceStreamingCursor( 0 ) )
			{
				return false;
			}
			continue;
		}

		CheckALErrors();
		alBufferData( buffer, alFormat, start, clampedBytesToQueue, streamingSample->SampleRate() );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			common->Warning( "OpenAL queued PCM fallback could not fill buffer for '%s'.", streamingSample->GetName() );
			streamingEndOfStream = true;
			streamingSample = NULL;
			return false;
		}

		alSourceQueueBuffers( openalSource, 1, &buffer );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			common->Warning( "OpenAL queued PCM fallback could not queue buffer for '%s'.", streamingSample->GetName() );
			streamingEndOfStream = true;
			streamingSample = NULL;
			return false;
		}

		SetSampleRate( streamingSample->SampleRate(), 0 );
		currentSample = streamingSample;
		queuedBytes = clampedBytesToQueue;
		idSoundHardware_OpenAL::CountDiagnosticEvent( idSoundHardware_OpenAL::OPENAL_DIAG_QUEUE_BUFFERS_SUBMITTED );
		const int queuedFrames = clampedBytesToQueue / blockBytes;
		AdvanceStreamingCursor( queuedFrames );
		return true;
	}

	streamingEndOfStream = true;
	streamingSample = NULL;
	return false;
}

/*
========================
idSoundVoice_OpenAL::BeginStreaming
========================
*/
int idSoundVoice_OpenAL::BeginStreaming( idSoundSample_OpenAL* sample, int bufferNumber, int offset )
{
	ALenum alFormat = AL_NONE;
	int blockBytes = 0;
	if( !ValidateStreamingSample( sample, alFormat, blockBytes ) )
	{
		return 0;
	}

	if( !EnsureStreamingBuffers() )
	{
		return 0;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = sample->buffers.Ptr();
	if( sampleBuffers == NULL || bufferNumber < 0 || bufferNumber >= sample->buffers.Num() )
	{
		return 0;
	}

	const int previousNumSamples = bufferNumber > 0 ? sampleBuffers[bufferNumber - 1].numSamples : 0;
	const int absoluteSampleFrame = previousNumSamples + offset;

	FlushSourceBuffers();
	CheckALErrors();
	alSourcei( openalSource, AL_LOOPING, AL_FALSE );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return 0;
	}
	if( !SetStreamingCursor( sample, absoluteSampleFrame ) )
	{
		return 0;
	}

	playbackMode = OPENQ4_OPENAL_PLAYBACK_STREAMING;
	currentSample = sample;

	int totalQueuedBytes = 0;
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		int queuedBytes = 0;
		if( !QueueNextStreamingBuffer( openalStreamingBuffer[i], queuedBytes ) )
		{
			break;
		}
		totalQueuedBytes += queuedBytes;
	}

	if( totalQueuedBytes <= 0 )
	{
		currentSample = NULL;
		playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
		ResetStreamingState();
		return 0;
	}

	return totalQueuedBytes;
}

/*
========================
idSoundVoice_OpenAL::PumpStreamingBuffers
========================
*/
bool idSoundVoice_OpenAL::PumpStreamingBuffers()
{
	if( !alIsSource( openalSource ) || playbackMode != OPENQ4_OPENAL_PLAYBACK_STREAMING )
	{
		return false;
	}

	ALint processedBuffers = 0;
	alGetSourcei( openalSource, AL_BUFFERS_PROCESSED, &processedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	while( processedBuffers > 0 )
	{
		ALuint buffer = 0;
		alSourceUnqueueBuffers( openalSource, 1, &buffer );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return false;
		}

		int queuedBytes = 0;
		if( QueueNextStreamingBuffer( buffer, queuedBytes ) )
		{
			idSoundHardware_OpenAL::CountDiagnosticEvent( idSoundHardware_OpenAL::OPENAL_DIAG_QUEUE_BUFFERS_REFILLED );
		}
		processedBuffers--;
	}

	ALint queuedBuffers = 0;
	alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	ALint state = AL_INITIAL;
	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( queuedBuffers > 0 )
	{
		if( ( state == AL_STOPPED || state == AL_INITIAL ) && !paused )
		{
			CheckALErrors();
			alSourcePlay( openalSource );
			if( CheckALErrors() != AL_NO_ERROR )
			{
				return false;
			}
			idSoundHardware_OpenAL::CountDiagnosticEvent( idSoundHardware_OpenAL::OPENAL_DIAG_QUEUE_UNDERRUN_RESTARTS );
			if( s_debugHardware.GetBool() )
			{
				idLib::Printf( "%dms: %i restarted queued PCM fallback for %s after underrun\n",
					Sys_Milliseconds(),
					openalSource,
					currentSample ? currentSample->GetName() : "<null>" );
			}
		}
		return true;
	}

	if( state == AL_PLAYING || state == AL_PAUSED )
	{
		return true;
	}

	currentSample = NULL;
	playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
	ResetStreamingState();
	return false;
}

/*
========================
idSoundVoice_OpenAL::SubmitBuffer
========================
*/
int idSoundVoice_OpenAL::SubmitBuffer( idSoundSample_OpenAL* sample, int bufferNumber, int offset )
{
	if( sample == NULL )
	{
		return 0;
	}

	const idSoundSample_OpenAL::sampleBuffer_t* sampleBuffers = sample->buffers.Ptr();
	const int numBuffers = sample->buffers.Num();
	if( sampleBuffers == NULL || numBuffers <= 0 || numBuffers > 16384 )
	{
		return 0;
	}

	const size_t sampleBuffersAddress = reinterpret_cast<size_t>( sampleBuffers );
	if( sampleBuffersAddress < 4096 || ( sampleBuffersAddress & ( sizeof( void* ) - 1 ) ) != 0 )
	{
		return 0;
	}

	if( ( bufferNumber < 0 ) || ( bufferNumber >= numBuffers ) )
	{
		return 0;
	}

#if 0
	idSoundSystemLocal::bufferContext_t* bufferContext = soundSystemLocal.ObtainStreamBufferContext();
	if( bufferContext == NULL )
	{
		idLib::Warning( "No free buffer contexts!" );
		return 0;
	}

	bufferContext->voice = this;
	bufferContext->sample = sample;
	bufferContext->bufferNumber = bufferNumber;
#endif

	const bool forceQueuedPCM = s_openALForcePCMQueue.GetBool();
	if( sample->openalBuffer == 0 && !forceQueuedPCM )
	{
		sample->CreateOpenALBuffer();
	}

	if( sample->openalBuffer != 0 && !forceQueuedPCM )
	{
		CheckALErrors();
		alSourcei( openalSource, AL_BUFFER, 0 );
		alSourcei( openalSource, AL_BUFFER, sample->openalBuffer );
		const bool shouldLoop = ( sample == loopingSample && loopingSample != NULL );
		alSourcei( openalSource, AL_LOOPING, shouldLoop ? AL_TRUE : AL_FALSE );
		if( offset > 0 )
		{
			alSourcei( openalSource, AL_SAMPLE_OFFSET, offset );
		}
		if( CheckALErrors() != AL_NO_ERROR )
		{
			currentSample = NULL;
			playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
			return 0;
		}

		SetSampleRate( sample->SampleRate(), 0 );
		currentSample = sample;
		if( shouldLoop )
		{
			playbackMode = OPENQ4_OPENAL_PLAYBACK_STATIC_LOOP;
		}
		else if( sample == leadinSample && loopingSample != NULL && loopingSample != sample )
		{
			playbackMode = OPENQ4_OPENAL_PLAYBACK_STATIC_LEADIN;
		}
		else
		{
			playbackMode = OPENQ4_OPENAL_PLAYBACK_STATIC_ONESHOT;
		}

		return sample->totalBufferSize;
	}
	else
	{
		idSoundHardware_OpenAL::CountDiagnosticEvent( idSoundHardware_OpenAL::OPENAL_DIAG_QUEUE_FALLBACK_ENTRIES );
		if( !sample->openalFallbackWarningIssued )
		{
			common->Warning( "OpenAL using queued PCM fallback for '%s' because %s.",
				sample->GetName(),
				forceQueuedPCM ? "s_openALForcePCMQueue is enabled" : "static upload is unavailable" );
			sample->openalFallbackWarningIssued = true;
		}

		return BeginStreaming( sample, bufferNumber, offset );
	}

	// should never happen
	return 0;

	/*

	XAUDIO2_BUFFER buffer = { 0 };
	if( offset > 0 )
	{
		int previousNumSamples = 0;
		if( bufferNumber > 0 )
		{
			previousNumSamples = sample->buffers[bufferNumber - 1].numSamples;
		}
		buffer.PlayBegin = offset;
		buffer.PlayLength = sample->buffers[bufferNumber].numSamples - previousNumSamples - offset;
	}
	buffer.AudioBytes = sample->buffers[bufferNumber].bufferSize;
	buffer.pAudioData = ( BYTE* )sample->buffers[bufferNumber].buffer;
	buffer.pContext = bufferContext;
	if( ( loopingSample == NULL ) && ( bufferNumber == sample->buffers.Num() - 1 ) )
	{
		buffer.Flags = XAUDIO2_END_OF_STREAM;
	}
	pSourceVoice->SubmitSourceBuffer( &buffer );

	return buffer.AudioBytes;

	*/
}

/*
========================
idSoundVoice_OpenAL::Update
========================
*/
bool idSoundVoice_OpenAL::Update()
{
	if( !alIsSource( openalSource ) || leadinSample == NULL )
	{
		return false;
	}

	ALint state = AL_INITIAL;
	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( playbackMode == OPENQ4_OPENAL_PLAYBACK_STATIC_LEADIN )
	{
		if( state == AL_STOPPED )
		{
			if( loopingSample == NULL || loopingSample == leadinSample )
			{
				currentSample = NULL;
				playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
				return false;
			}

			if( loopingSample->openalBuffer == 0 && !loopingSample->CreateOpenALBuffer() )
			{
				currentSample = NULL;
				playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
				return false;
			}

			alSourceStop( openalSource );
			alSourcei( openalSource, AL_BUFFER, 0 );

			if( SubmitBuffer( loopingSample, 0, 0 ) <= 0 )
			{
				currentSample = NULL;
				playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
				return false;
			}

			alSourcePlay( openalSource );
			paused = false;

			if( s_debugHardware.GetBool() )
			{
				idLib::Printf( "%dms: %i transitioned %s to loop %s\n",
					Sys_Milliseconds(),
					openalSource,
					leadinSample ? leadinSample->GetName() : "<null>",
					loopingSample ? loopingSample->GetName() : "<null>" );
			}
		}
		return true;
	}

	if( playbackMode == OPENQ4_OPENAL_PLAYBACK_STATIC_LOOP )
	{
		if( ( state == AL_STOPPED || state == AL_INITIAL ) && !paused )
		{
			alSourcePlay( openalSource );
			return true;
		}
		return state == AL_PLAYING || state == AL_PAUSED;
	}

	if( playbackMode == OPENQ4_OPENAL_PLAYBACK_STATIC_ONESHOT )
	{
		return state == AL_PLAYING || state == AL_PAUSED;
	}

	if( playbackMode == OPENQ4_OPENAL_PLAYBACK_STREAMING )
	{
		return PumpStreamingBuffers();
	}

	/*
	if( pSourceVoice == NULL || leadinSample == NULL )
	{
		return false;
	}

	XAUDIO2_VOICE_STATE state;
	pSourceVoice->GetState( &state );

	const int srcChannels = leadinSample->NumChannels();

	float pLevelMatrix[ MAX_CHANNELS_PER_VOICE * MAX_CHANNELS_PER_VOICE ] = { 0 };
	CalculateSurround( srcChannels, pLevelMatrix, 1.0f );

	if( s_skipHardwareSets.GetBool() )
	{
		return true;
	}

	pSourceVoice->SetOutputMatrix( soundSystemLocal.hardware.pMasterVoice, srcChannels, dstChannels, pLevelMatrix, OPERATION_SET );

	assert( idMath::Fabs( gain ) <= XAUDIO2_MAX_VOLUME_LEVEL );
	pSourceVoice->SetVolume( gain, OPERATION_SET );

	SetSampleRate( sampleRate, OPERATION_SET );

	// we don't do this any longer because we pause and unpause explicitly when the soundworld is paused or unpaused
	// UnPause();
	*/
	return false;
}

/*
========================
idSoundVoice_OpenAL::SourceLatencyQueriesAvailable
========================
*/
bool idSoundVoice_OpenAL::SourceLatencyQueriesAvailable()
{
#if OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED
	if( alIsExtensionPresent( "AL_SOFT_source_latency" ) != AL_TRUE )
	{
		return false;
	}
	if( qalGetSourcedvSOFT == NULL )
	{
		qalGetSourcedvSOFT = reinterpret_cast<LPALGETSOURCEDVSOFT>( alGetProcAddress( "alGetSourcedvSOFT" ) );
	}
	return qalGetSourcedvSOFT != NULL;
#else
	return false;
#endif
}

/*
========================
idSoundVoice_OpenAL::GetPlaybackLatencyMS
========================
*/
bool idSoundVoice_OpenAL::GetPlaybackLatencyMS( float& offsetMS, float& latencyMS ) const
{
	offsetMS = 0.0f;
	latencyMS = 0.0f;

#if OPENQ4_OPENAL_SOURCE_LATENCY_SUPPORTED
	if( !alIsSource( openalSource ) || !SourceLatencyQueriesAvailable() )
	{
		return false;
	}

	ALdouble offsetAndLatencySeconds[2] = { 0.0, 0.0 };
	(void)alGetError();
	qalGetSourcedvSOFT( openalSource, AL_SEC_OFFSET_LATENCY_SOFT, offsetAndLatencySeconds );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	offsetMS = Max( 0.0f, static_cast<float>( offsetAndLatencySeconds[0] * 1000.0 ) );
	latencyMS = Max( 0.0f, static_cast<float>( offsetAndLatencySeconds[1] * 1000.0 ) );
	return true;
#else
	return false;
#endif
}

/*
========================
idSoundVoice_OpenAL::IsPlaying
========================
*/
bool idSoundVoice_OpenAL::IsPlaying()
{
	if( !alIsSource( openalSource ) )
	{
		return false;
	}

	ALint state = AL_INITIAL;

	alGetSourcei( openalSource, AL_SOURCE_STATE, &state );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	return ( state == AL_PLAYING || state == AL_PAUSED );

	//XAUDIO2_VOICE_STATE state;
	//pSourceVoice->GetState( &state );

	//return ( state.BuffersQueued != 0 );
}

/*
========================
idSoundVoice_OpenAL::FlushSourceBuffers
========================
*/
void idSoundVoice_OpenAL::FlushSourceBuffers()
{
	if( alIsSource( openalSource ) )
	{
		alSourceStop( openalSource );

		ALint sourceType = AL_UNDETERMINED;
		alGetSourcei( openalSource, AL_SOURCE_TYPE, &sourceType );
		if( sourceType == AL_STREAMING )
		{
			ALint queuedBuffers = 0;
			alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
			while( queuedBuffers > 0 )
			{
				ALuint buffer = 0;
				CheckALErrors();
				alSourceUnqueueBuffers( openalSource, 1, &buffer );
				if( CheckALErrors() != AL_NO_ERROR )
				{
					break;
				}
				queuedBuffers--;
			}
		}

		CheckALErrors();
		alSourcei( openalSource, AL_BUFFER, 0 );
		alSourcei( openalSource, AL_LOOPING, AL_FALSE );
		DetachWetDryRouting();
		CheckALErrors();

		ResetStreamingState();
		currentSample = NULL;
		playbackMode = OPENQ4_OPENAL_PLAYBACK_NONE;
	}
}

/*
========================
idSoundVoice_OpenAL::Pause
========================
*/
void idSoundVoice_OpenAL::Pause()
{
	if( !alIsSource( openalSource ) || paused )
	{
		return;
	}

	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i pausing %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	alSourcePause( openalSource );
	//pSourceVoice->Stop( 0, OPERATION_SET );
	paused = true;
}

/*
========================
idSoundVoice_OpenAL::UnPause
========================
*/
void idSoundVoice_OpenAL::UnPause()
{
	if( !alIsSource( openalSource ) || !paused )
	{
		return;
	}

	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i unpausing %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	alSourcePlay( openalSource );
	//pSourceVoice->Start( 0, OPERATION_SET );
	paused = false;
}

/*
========================
idSoundVoice_OpenAL::Stop
========================
*/
void idSoundVoice_OpenAL::Stop()
{
	if( !alIsSource( openalSource ) )
	{
		return;
	}

	if( s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i stopping %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	FlushSourceBuffers();
	//pSourceVoice->Stop( 0, OPERATION_SET );
	paused = true;
}

/*
========================
idSoundVoice_OpenAL::GetAmplitude
========================
*/
float idSoundVoice_OpenAL::GetAmplitude()
{
	// TODO
	return 1.0f;

	/*
	if( !hasVUMeter )
	{
		return 1.0f;
	}

	float peakLevels[ MAX_CHANNELS_PER_VOICE ];
	float rmsLevels[ MAX_CHANNELS_PER_VOICE ];

	XAUDIO2FX_VOLUMEMETER_LEVELS levels;
	levels.ChannelCount = leadinSample->NumChannels();
	levels.pPeakLevels = peakLevels;
	levels.pRMSLevels = rmsLevels;

	if( levels.ChannelCount > MAX_CHANNELS_PER_VOICE )
	{
		levels.ChannelCount = MAX_CHANNELS_PER_VOICE;
	}

	if( pSourceVoice->GetEffectParameters( 0, &levels, sizeof( levels ) ) != S_OK )
	{
		return 0.0f;
	}

	if( levels.ChannelCount == 1 )
	{
		return rmsLevels[0];
	}

	float rms = 0.0f;
	for( uint32 i = 0; i < levels.ChannelCount; i++ )
	{
		rms += rmsLevels[i];
	}

	return rms / ( float )levels.ChannelCount;
	*/
}

/*
========================
idSoundVoice_OpenAL::ResetSampleRate
========================
*/
void idSoundVoice_OpenAL::SetSampleRate( uint32 newSampleRate, uint32 operationSet )
{
	sampleRate = newSampleRate;

	/*
	if( pSourceVoice == NULL || leadinSample == NULL )
	{
		return;
	}

	sampleRate = newSampleRate;

	XAUDIO2_FILTER_PARAMETERS filter;
	filter.Type = LowPassFilter;
	filter.OneOverQ = 1.0f;			// [0.0f, XAUDIO2_MAX_FILTER_ONEOVERQ]
	float cutoffFrequency = 1000.0f / Max( 0.01f, occlusion );
	if( cutoffFrequency * 6.0f >= ( float )sampleRate )
	{
		filter.Frequency = XAUDIO2_MAX_FILTER_FREQUENCY;
	}
	else
	{
		filter.Frequency = 2.0f * idMath::Sin( idMath::PI * cutoffFrequency / ( float )sampleRate );
	}
	assert( filter.Frequency >= 0.0f && filter.Frequency <= XAUDIO2_MAX_FILTER_FREQUENCY );
	filter.Frequency = idMath::ClampFloat( 0.0f, XAUDIO2_MAX_FILTER_FREQUENCY, filter.Frequency );

	pSourceVoice->SetFilterParameters( &filter, operationSet );

	float freqRatio = pitch * ( float )sampleRate / ( float )sourceVoiceRate;
	assert( freqRatio >= XAUDIO2_MIN_FREQ_RATIO && freqRatio <= XAUDIO2_MAX_FREQ_RATIO );
	freqRatio = idMath::ClampFloat( XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO, freqRatio );

	// if the value specified for maxFreqRatio is too high for the specified format, the call to CreateSourceVoice will fail
	if( numChannels == 1 )
	{
		assert( freqRatio * ( float )SYSTEM_SAMPLE_RATE <= XAUDIO2_MAX_RATIO_TIMES_RATE_XMA_MONO );
	}
	else
	{
		assert( freqRatio * ( float )SYSTEM_SAMPLE_RATE <= XAUDIO2_MAX_RATIO_TIMES_RATE_XMA_MULTICHANNEL );
	}

	pSourceVoice->SetFrequencyRatio( freqRatio, operationSet );
	*/
}

/*
========================
idSoundVoice_OpenAL::CreateWetDryFilters
========================
*/
bool idSoundVoice_OpenAL::CreateWetDryFilters()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !soundSystemLocal.hardware.HasEFXFilters() || !openQ4_LoadVoiceEfxProcs() || !alIsSource( openalSource ) )
	{
		return false;
	}

	const char* failureReason = NULL;
	if( openalDirectFilter == 0 )
	{
		CheckALErrors();
		qalGenFilters( 1, &openalDirectFilter );
		if( CheckALErrors() != AL_NO_ERROR || openalDirectFilter == 0 )
		{
			openalDirectFilter = 0;
			failureReason = "direct low-pass filter generation failed";
		}
		else
		{
			qalFilteri( openalDirectFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalDirectFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalDirectFilter, AL_LOWPASS_GAINHF, 1.0f );
			if( CheckALErrors() != AL_NO_ERROR )
			{
				failureReason = "direct low-pass filter initialization failed";
			}
		}
	}
	if( failureReason == NULL && openalAuxFilter == 0 )
	{
		CheckALErrors();
		qalGenFilters( 1, &openalAuxFilter );
		if( CheckALErrors() != AL_NO_ERROR || openalAuxFilter == 0 )
		{
			openalAuxFilter = 0;
			failureReason = "auxiliary low-pass filter generation failed";
		}
		else
		{
			qalFilteri( openalAuxFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalAuxFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalAuxFilter, AL_LOWPASS_GAINHF, 1.0f );
			if( CheckALErrors() != AL_NO_ERROR )
			{
				failureReason = "auxiliary low-pass filter initialization failed";
			}
		}
	}

	if( failureReason != NULL )
	{
		soundSystemLocal.hardware.DisableEFXFilters( failureReason );
		DestroyWetDryFilters();
		return false;
	}

	return openalDirectFilter != 0 && openalAuxFilter != 0;
#else
	return false;
#endif
}

/*
========================
idSoundVoice_OpenAL::DetachWetDryRouting
========================
*/
bool idSoundVoice_OpenAL::DetachWetDryRouting()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !alIsSource( openalSource ) )
	{
		efxRoutingAttached = false;
		return true;
	}
	if( !efxRoutingAttached && openalDirectFilter == 0 && openalAuxFilter == 0 )
	{
		return true;
	}

	CheckALErrors();
	alSourcei( openalSource, AL_DIRECT_FILTER, AL_FILTER_NULL );
	alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		soundSystemLocal.hardware.DisableEFXFilters( "filter detach failed" );
		return false;
	}
#endif
	efxRoutingAttached = false;
	return true;
}

/*
========================
idSoundVoice_OpenAL::DestroyWetDryFilters
========================
*/
void idSoundVoice_OpenAL::DestroyWetDryFilters()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !DetachWetDryRouting() )
	{
		return;
	}
	if( openalDirectFilter != 0 )
	{
		if( qalDeleteFilters != NULL )
		{
			qalDeleteFilters( 1, &openalDirectFilter );
		}
		openalDirectFilter = 0;
	}
	if( openalAuxFilter != 0 )
	{
		if( qalDeleteFilters != NULL )
		{
			qalDeleteFilters( 1, &openalAuxFilter );
		}
		openalAuxFilter = 0;
	}
#endif
	efxRoutingAttached = false;
}

/*
========================
idSoundVoice_OpenAL::ApplyWetDryRouting
========================
*/
void idSoundVoice_OpenAL::ApplyWetDryRouting()
{
	if( !alIsSource( openalSource ) )
	{
		return;
	}

	float effectiveDry = Max( 0.0f, dryLevel );
	float effectiveWet = Max( 0.0f, wetLevel );

	switch( s_openALEfxDebugMode.GetInteger() )
	{
		case 1:
			effectiveDry = 0.0f;
			effectiveWet = 1.0f;
			break;
		case 2:
			effectiveDry = 1.0f;
			effectiveWet = 0.0f;
			break;
		default:
			break;
	}

	effectiveDry = idMath::ClampFloat( 0.0f, 1.0f, effectiveDry );
	effectiveWet = idMath::ClampFloat( 0.0f, 1.0f, effectiveWet );
	const float effectiveGain = Max( 0.0f, gain );
	const openQ4OcclusionFilter_t occlusionFilter = OpenQ4_BuildOcclusionFilter( occlusion, environmentMuffle );
	const float directFilterGain = effectiveDry * occlusionFilter.directGain;
	const float wetFilterGain = effectiveWet * occlusionFilter.wetGain;

#if OPENQ4_OPENAL_EFX_SUPPORTED
	bool hasEfxFilters = soundSystemLocal.hardware.HasEFXFilters() && openQ4_LoadVoiceEfxProcs();
	if( hasEfxFilters && !CreateWetDryFilters() )
	{
		hasEfxFilters = false;
	}

	if( hasEfxFilters && openalDirectFilter != 0 )
	{
		CheckALErrors();
		qalFilterf( openalDirectFilter, AL_LOWPASS_GAIN, directFilterGain );
		qalFilterf( openalDirectFilter, AL_LOWPASS_GAINHF, occlusionFilter.directGainHF );
		alSourcei( openalSource, AL_DIRECT_FILTER, openalDirectFilter );
		alSourcef( openalSource, AL_GAIN, effectiveGain );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			soundSystemLocal.hardware.DisableEFXFilters( "direct low-pass filter routing failed" );
			DetachWetDryRouting();
			DestroyWetDryFilters();
			alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
			CheckALErrors();
			return;
		}
		efxRoutingAttached = true;
	}
	else
	{
		if( efxRoutingAttached || openalDirectFilter != 0 || openalAuxFilter != 0 )
		{
			DetachWetDryRouting();
			DestroyWetDryFilters();
		}
		alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
		CheckALErrors();
	}

	if( hasEfxFilters && openalAuxFilter != 0 && soundSystemLocal.hardware.HasEFX() && soundSystemLocal.hardware.GetAuxEffectSlot() != 0 && wetFilterGain > 0.0f )
	{
		CheckALErrors();
		qalFilterf( openalAuxFilter, AL_LOWPASS_GAIN, wetFilterGain );
		qalFilterf( openalAuxFilter, AL_LOWPASS_GAINHF, occlusionFilter.wetGainHF );
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, soundSystemLocal.hardware.GetAuxEffectSlot(), 0, openalAuxFilter );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			soundSystemLocal.hardware.DisableEFXFilters( "auxiliary send filter routing failed" );
			DetachWetDryRouting();
			DestroyWetDryFilters();
			alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
			CheckALErrors();
			return;
		}
		efxRoutingAttached = true;
	}
	else if( hasEfxFilters )
	{
		CheckALErrors();
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			soundSystemLocal.hardware.DisableEFXFilters( "auxiliary send detach failed" );
			DetachWetDryRouting();
			DestroyWetDryFilters();
			alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
			CheckALErrors();
			return;
		}
	}
#else
	alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
	CheckALErrors();
#endif
}

/*
========================
idSoundVoice_OpenAL::OnBufferStart
========================
*/
void idSoundVoice_OpenAL::OnBufferStart( idSoundSample_OpenAL* sample, int bufferNumber )
{
	//SetSampleRate( sample->SampleRate(), XAUDIO2_COMMIT_NOW );

	idSoundSample_OpenAL* nextSample = sample;
	int nextBuffer = bufferNumber + 1;
	if( nextBuffer == sample->buffers.Num() )
	{
		if( sample == leadinSample )
		{
			if( loopingSample == NULL )
			{
				return;
			}
			nextSample = loopingSample;
		}
		nextBuffer = 0;
	}

	SubmitBuffer( nextSample, nextBuffer, 0 );
}

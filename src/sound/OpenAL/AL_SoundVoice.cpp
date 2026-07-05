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

// The whole system runs at this sample rate
static int SYSTEM_SAMPLE_RATE = 44100;
static float ONE_OVER_SYSTEM_SAMPLE_RATE = 1.0f / SYSTEM_SAMPLE_RATE;

static const float OPENQ4_OPENAL_PORTAL_DIRECT_ATTENUATION_DB = -8.0f;
static const float OPENQ4_OPENAL_PORTAL_DIRECT_HF_ATTENUATION_DB = -24.0f;
static const float OPENQ4_OPENAL_PORTAL_WET_ATTENUATION_DB = -3.0f;
static const float OPENQ4_OPENAL_PORTAL_WET_HF_ATTENUATION_DB = -10.0f;
static const float OPENQ4_OPENAL_ENV_DIRECT_ATTENUATION_DB = -2.0f;
static const float OPENQ4_OPENAL_ENV_DIRECT_HF_ATTENUATION_DB = -16.0f;
static const float OPENQ4_OPENAL_ENV_WET_ATTENUATION_DB = -1.0f;
static const float OPENQ4_OPENAL_ENV_WET_HF_ATTENUATION_DB = -12.0f;
static const float OPENQ4_OPENAL_MAX_SOURCE_GAIN = 16.0f;

struct openQ4OcclusionFilter_t
{
	float directGain;
	float directGainHF;
	float wetGain;
	float wetGainHF;
};

static float OpenQ4_DBToClampedGain( const float db )
{
	if( FLOAT_IS_NAN( db ) )
	{
		return 0.0f;
	}
	return idMath::ClampFloat( 0.0f, 1.0f, DBtoLinear( db ) );
}

static float OpenQ4_SanitizeUnitValue( const float value )
{
	if( FLOAT_IS_NAN( value ) || value <= 0.0f )
	{
		return 0.0f;
	}
	if( value >= 1.0f )
	{
		return 1.0f;
	}
	return value;
}

static float OpenQ4_SanitizeSourceGain( const float sourceGain )
{
	if( FLOAT_IS_NAN( sourceGain ) || sourceGain <= 0.0f )
	{
		return 0.0f;
	}
	return idMath::ClampFloat( 0.0f, OPENQ4_OPENAL_MAX_SOURCE_GAIN, sourceGain );
}

static openQ4OcclusionFilter_t OpenQ4_BuildOcclusionFilter( const float occlusion, const float environmentMuffle )
{
	const float clampedOcclusion = OpenQ4_SanitizeUnitValue( occlusion );
	const float clampedEnvironmentMuffle = OpenQ4_SanitizeUnitValue( environmentMuffle );

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
	openalSource( 0 ),
	openalDirectFilter( 0 ),
	openalAuxFilter( 0 ),
	nextQueuedSample( NULL ),
	nextQueuedBuffer( 0 ),
	nextQueuedOffset( 0 ),
	queuedBufferPlaybackActive( false ),
	leadinSample( NULL ),
	loopingSample( NULL ),
	formatTag( 0 ),
	numChannels( 0 ),
	sampleRate( 0 ),
	hasVUMeter( false ),
	paused( true )
{
	openalStreamingBuffer[0] = 0;
	openalStreamingBuffer[1] = 0;
	openalStreamingBuffer[2] = 0;
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
idSoundVoice_OpenAL::CompatibleFormat
========================
*/
bool idSoundVoice_OpenAL::CompatibleFormat( idSoundSample_OpenAL* s )
{
	if( s == NULL )
	{
		return false;
	}

	return !IsPlaying();
}

bool idSoundVoice_OpenAL::UsesStreamingBuffers() const
{
	return openalStreamingBuffer[0] != 0 || openalStreamingBuffer[1] != 0 || openalStreamingBuffer[2] != 0;
}

bool idSoundVoice_OpenAL::HasQueuedBufferState() const
{
	return queuedBufferPlaybackActive || nextQueuedSample != NULL;
}

void idSoundVoice_OpenAL::ResetQueuedBufferState()
{
	nextQueuedSample = NULL;
	nextQueuedBuffer = 0;
	nextQueuedOffset = 0;
	queuedBufferPlaybackActive = false;
}

void idSoundVoice_OpenAL::DeleteStreamingBuffers()
{
	ALuint buffersToDelete[MAX_QUEUED_BUFFERS];
	ALsizei numBuffersToDelete = 0;
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		if( openalStreamingBuffer[i] != 0 )
		{
			buffersToDelete[numBuffersToDelete++] = openalStreamingBuffer[i];
		}
	}
	if( numBuffersToDelete > 0 )
	{
		CheckALErrors();
		alDeleteBuffers( numBuffersToDelete, buffersToDelete );
		CheckALErrors();
	}
	for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
	{
		openalStreamingBuffer[i] = 0;
	}
}

bool idSoundVoice_OpenAL::EnsureStreamingBuffers()
{
	if( UsesStreamingBuffers() )
	{
		return true;
	}

	CheckALErrors();
	alGenBuffers( MAX_QUEUED_BUFFERS, openalStreamingBuffer );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		openalStreamingBuffer[0] = openalStreamingBuffer[1] = openalStreamingBuffer[2] = 0;
		return false;
	}
	return true;
}

void idSoundVoice_OpenAL::AdvanceQueuedBuffer()
{
	if( nextQueuedSample == NULL )
	{
		return;
	}

	nextQueuedOffset = 0;
	nextQueuedBuffer++;
	if( nextQueuedBuffer < nextQueuedSample->buffers.Num() )
	{
		return;
	}

	if( nextQueuedSample == leadinSample && loopingSample != NULL )
	{
		SetNextQueuedSampleStart( loopingSample );
		return;
	}

	if( nextQueuedSample == loopingSample && loopingSample != NULL )
	{
		SetNextQueuedSampleStart( loopingSample );
		return;
	}

	nextQueuedSample = NULL;
	nextQueuedBuffer = 0;
	nextQueuedOffset = 0;
}

bool idSoundVoice_OpenAL::SetNextQueuedSampleStart( idSoundSample_OpenAL* sample )
{
	if( sample == NULL || sample->buffers.Num() <= 0 )
	{
		nextQueuedSample = NULL;
		nextQueuedBuffer = 0;
		nextQueuedOffset = 0;
		return false;
	}

	const int startSample = Max( 0, sample->playBegin );
	int previousNumSamples = 0;
	for( int i = 0; i < sample->buffers.Num(); i++ )
	{
		const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sample->buffers[i];
		if( sampleBuffer.numSamples > startSample )
		{
			nextQueuedSample = sample;
			nextQueuedBuffer = i;
			nextQueuedOffset = startSample - previousNumSamples;
			return true;
		}
		previousNumSamples = sampleBuffer.numSamples;
	}

	nextQueuedSample = NULL;
	nextQueuedBuffer = 0;
	nextQueuedOffset = 0;
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
		Stop();
	}

	leadinSample = ( idSoundSample_OpenAL* )leadinSample_;
	loopingSample = ( idSoundSample_OpenAL* )loopingSample_;
	ResetQueuedBufferState();

	if( leadinSample == NULL )
	{
		return;
	}

	if( leadinSample->openalBuffer == 0 && leadinSample->format.basic.formatTag != idWaveFile::FORMAT_XMA2 )
	{
		leadinSample->CreateOpenALBuffer();
	}
	if( loopingSample != NULL && loopingSample->openalBuffer == 0 && loopingSample->format.basic.formatTag != idWaveFile::FORMAT_XMA2 )
	{
		loopingSample->CreateOpenALBuffer();
	}

	if( alIsSource( openalSource ) )
	{
		FlushSourceBuffers();
	}
	else
	{
		DestroyInternal();

		CheckALErrors();

		alGenSources( 1, &openalSource );
		if( CheckALErrors() != AL_NO_ERROR )
			//if( pSourceVoice == NULL )
		{
			// If this hits, then we are most likely passing an invalid sample format, which should have been caught by the loader (and the sample defaulted)
			return;
		}

		alSourcef( openalSource, AL_ROLLOFF_FACTOR, 0.0f );
	}

	formatTag = leadinSample->format.basic.formatTag;
	numChannels = leadinSample->format.basic.numChannels;
	sampleRate = leadinSample->format.basic.samplesPerSec;

	const bool needsStreamingBuffers = leadinSample->openalBuffer == 0 || ( loopingSample != NULL && loopingSample->openalBuffer == 0 );
	if( needsStreamingBuffers )
	{
		EnsureStreamingBuffers();
	}
	else
	{
		DeleteStreamingBuffers();
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

	sourceVoiceRate = sampleRate;
	//pSourceVoice->SetSourceSampleRate( sampleRate );
	//pSourceVoice->SetVolume( 0.0f );

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

		FlushSourceBuffers();
		alDeleteSources( 1, &openalSource );
		openalSource = 0;
		hasVUMeter = false;
	}
	DeleteStreamingBuffers();
	ResetQueuedBufferState();
	DestroyWetDryFilters();
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

	if( RestartAt( offsetSamples ) <= 0 )
	{
		return;
	}
	if( !Update() )
	{
		return;
	}
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
	ResetQueuedBufferState();

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
			const int bufferOffset = sample->playBegin + offsetSamples - previousNumSamples;
			FlushSourceBuffers();
			if( sample->openalBuffer != 0 && ( loopingSample == NULL || sample == loopingSample ) )
			{
				alSourcei( openalSource, AL_BUFFER, sample->openalBuffer );
				alSourcei( openalSource, AL_LOOPING, ( sample == loopingSample && loopingSample != NULL ? AL_TRUE : AL_FALSE ) );
				if( bufferOffset > 0 )
				{
					alSourcei( openalSource, AL_SAMPLE_OFFSET, sample->playBegin + offsetSamples );
				}
				if( CheckALErrors() != AL_NO_ERROR )
				{
					return 0;
				}
				return sample->totalBufferSize;
			}

			if( sample->openalBuffer == 0 && !EnsureStreamingBuffers() )
			{
				return 0;
			}
			nextQueuedSample = sample;
			nextQueuedBuffer = i;
			nextQueuedOffset = bufferOffset;
			return 1;
		}
		previousNumSamples = sampleBuffer.numSamples;
	}

	return 0;
}

/*
========================
idSoundVoice_OpenAL::SubmitBuffer
========================
*/
int idSoundVoice_OpenAL::SubmitBuffer( idSoundSample_OpenAL* sample, int bufferNumber, int offset, ALuint streamBuffer )
{
	if( sample == NULL || !alIsSource( openalSource ) )
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

	if( sample->openalBuffer != 0 )
	{
		ALuint staticBuffer = sample->openalBuffer;
		alSourceQueueBuffers( openalSource, 1, &staticBuffer );
		if( offset > 0 )
		{
			alSourcei( openalSource, AL_SAMPLE_OFFSET, offset );
		}
		if( CheckALErrors() != AL_NO_ERROR )
		{
			return 0;
		}

		return sample->totalBufferSize;
	}

	if( streamBuffer == 0 || sample->format.basic.blockSize <= 0 )
	{
		return 0;
	}

	if( sample->format.basic.formatTag != idWaveFile::FORMAT_PCM )
	{
		return 0;
	}

	const idSoundSample_OpenAL::sampleBuffer_t& sampleBuffer = sampleBuffers[bufferNumber];
	if( sampleBuffer.buffer == NULL || sampleBuffer.bufferSize <= 0 )
	{
		return 0;
	}

	const int bytesPerSample = sample->format.basic.blockSize;
	if( bytesPerSample <= 0 || sampleBuffer.bufferSize % bytesPerSample != 0 )
	{
		return 0;
	}

	const int previousNumSamples = bufferNumber > 0 ? sampleBuffers[bufferNumber - 1].numSamples : 0;
	const int bufferSamples = sampleBuffer.numSamples - previousNumSamples;
	if( bufferSamples <= 0 )
	{
		return 0;
	}

	if( offset < 0 )
	{
		offset = 0;
	}
	if( offset >= bufferSamples || offset >= sampleBuffer.bufferSize / bytesPerSample )
	{
		return 0;
	}

	const int byteOffset = offset * bytesPerSample;
	if( byteOffset < 0 || byteOffset >= sampleBuffer.bufferSize )
	{
		return 0;
	}

	const byte* audioData = reinterpret_cast<const byte*>( sampleBuffer.buffer ) + byteOffset;
	const int audioBytes = sampleBuffer.bufferSize - byteOffset;
	if( audioBytes <= 0 )
	{
		return 0;
	}

	alBufferData( streamBuffer, sample->GetOpenALBufferFormat(), audioData, audioBytes, sample->SampleRate() );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return 0;
	}

	alSourceQueueBuffers( openalSource, 1, &streamBuffer );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return 0;
	}

	return audioBytes;
}

bool idSoundVoice_OpenAL::QueueNextBuffer( ALuint streamBuffer )
{
	if( nextQueuedSample == NULL )
	{
		return false;
	}

	const int submittedBytes = SubmitBuffer( nextQueuedSample, nextQueuedBuffer, nextQueuedOffset, streamBuffer );
	if( submittedBytes <= 0 )
	{
		nextQueuedSample = NULL;
		nextQueuedBuffer = 0;
		nextQueuedOffset = 0;
		return false;
	}

	queuedBufferPlaybackActive = true;
	AdvanceQueuedBuffer();
	return true;
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

	if( !HasQueuedBufferState() )
	{
		return true;
	}

	const bool hadQueuedState = HasQueuedBufferState();
	bool submittedAny = false;

	ALint processedBuffers = 0;
	alGetSourcei( openalSource, AL_BUFFERS_PROCESSED, &processedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	ALuint recycledBuffers[MAX_QUEUED_BUFFERS];
	int recycledCount = 0;
	while( processedBuffers > 0 )
	{
		const int unqueueCount = Min( processedBuffers, MAX_QUEUED_BUFFERS - recycledCount );
		if( unqueueCount <= 0 )
		{
			break;
		}
		alSourceUnqueueBuffers( openalSource, unqueueCount, &recycledBuffers[recycledCount] );
		if( CheckALErrors() != AL_NO_ERROR )
		{
			ResetQueuedBufferState();
			return false;
		}
		recycledCount += unqueueCount;
		processedBuffers -= unqueueCount;
	}

	ALint queuedBuffers = 0;
	alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
	if( CheckALErrors() != AL_NO_ERROR )
	{
		return false;
	}

	if( UsesStreamingBuffers() )
	{
		ALuint buffersToFill[MAX_QUEUED_BUFFERS];
		int buffersToFillCount = 0;

		if( queuedBuffers == 0 && recycledCount == 0 )
		{
			for( int i = 0; i < MAX_QUEUED_BUFFERS; i++ )
			{
				if( openalStreamingBuffer[i] != 0 )
				{
					buffersToFill[buffersToFillCount++] = openalStreamingBuffer[i];
				}
			}
		}
		else
		{
			for( int i = 0; i < recycledCount; i++ )
			{
				buffersToFill[buffersToFillCount++] = recycledBuffers[i];
			}
		}

		for( int i = 0; i < buffersToFillCount && nextQueuedSample != NULL; i++ )
		{
			if( QueueNextBuffer( buffersToFill[i] ) )
			{
				submittedAny = true;
				queuedBuffers++;
			}
		}
	}
	else
	{
		while( queuedBuffers < MAX_QUEUED_BUFFERS && nextQueuedSample != NULL )
		{
			if( !QueueNextBuffer( 0 ) )
			{
				break;
			}
			submittedAny = true;
			queuedBuffers++;
		}
	}

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

	if( !paused && queuedBuffers > 0 && state != AL_PLAYING )
	{
		alSourcePlay( openalSource );
		CheckALErrors();
	}

	if( queuedBuffers <= 0 && nextQueuedSample == NULL && state != AL_PLAYING )
	{
		queuedBufferPlaybackActive = false;
	}

	return queuedBuffers > 0 || state == AL_PLAYING || !hadQueuedState || submittedAny;
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

	return ( state == AL_PLAYING );

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
	if( !alIsSource( openalSource ) )
	{
		ResetQueuedBufferState();
		return;
	}

	alSourceStop( openalSource );

	ALint sourceType = AL_UNDETERMINED;
	alGetSourcei( openalSource, AL_SOURCE_TYPE, &sourceType );
	CheckALErrors();
	if( sourceType == AL_STREAMING )
	{
		ALint queuedBuffers = 0;
		alGetSourcei( openalSource, AL_BUFFERS_QUEUED, &queuedBuffers );
		CheckALErrors();
		while( queuedBuffers > 0 )
		{
			ALuint unqueuedBuffers[MAX_QUEUED_BUFFERS];
			const int unqueueCount = Min( queuedBuffers, MAX_QUEUED_BUFFERS );
			alSourceUnqueueBuffers( openalSource, unqueueCount, unqueuedBuffers );
			if( CheckALErrors() != AL_NO_ERROR )
			{
				break;
			}
			queuedBuffers -= unqueueCount;
		}
	}

	alSourcei( openalSource, AL_BUFFER, 0 );
	alSourcei( openalSource, AL_LOOPING, AL_FALSE );
	CheckALErrors();

	paused = true;
	ResetQueuedBufferState();
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

	if( !paused && s_debugHardware.GetBool() )
	{
		idLib::Printf( "%dms: %i stopping %s\n", Sys_Milliseconds(), openalSource, leadinSample ? leadinSample->GetName() : "<null>" );
	}

	FlushSourceBuffers();
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
void idSoundVoice_OpenAL::CreateWetDryFilters()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
	if( !soundSystemLocal.hardware.HasEFXFilters() || !openQ4_LoadVoiceEfxProcs() || !alIsSource( openalSource ) )
	{
		return;
	}
	if( openalDirectFilter == 0 )
	{
		qalGenFilters( 1, &openalDirectFilter );
		if( CheckALErrors() == AL_NO_ERROR && openalDirectFilter != 0 )
		{
			qalFilteri( openalDirectFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalDirectFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalDirectFilter, AL_LOWPASS_GAINHF, 1.0f );
			CheckALErrors();
		}
		else
		{
			openalDirectFilter = 0;
		}
	}
	if( openalAuxFilter == 0 )
	{
		qalGenFilters( 1, &openalAuxFilter );
		if( CheckALErrors() == AL_NO_ERROR && openalAuxFilter != 0 )
		{
			qalFilteri( openalAuxFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
			qalFilterf( openalAuxFilter, AL_LOWPASS_GAIN, 1.0f );
			qalFilterf( openalAuxFilter, AL_LOWPASS_GAINHF, 1.0f );
			CheckALErrors();
		}
		else
		{
			openalAuxFilter = 0;
		}
	}
#endif
}

/*
========================
idSoundVoice_OpenAL::DestroyWetDryFilters
========================
*/
void idSoundVoice_OpenAL::DestroyWetDryFilters()
{
#if OPENQ4_OPENAL_EFX_SUPPORTED
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

	float effectiveDry = OpenQ4_SanitizeUnitValue( dryLevel );
	float effectiveWet = OpenQ4_SanitizeUnitValue( wetLevel );

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

	const float effectiveGain = OpenQ4_SanitizeSourceGain( gain );
	const openQ4OcclusionFilter_t occlusionFilter = OpenQ4_BuildOcclusionFilter( occlusion, environmentMuffle );
	const float directFilterGain = effectiveDry * occlusionFilter.directGain;
	const float wetFilterGain = effectiveWet * occlusionFilter.wetGain;

#if OPENQ4_OPENAL_EFX_SUPPORTED
	const bool hasEfxFilters = soundSystemLocal.hardware.HasEFXFilters() && openQ4_LoadVoiceEfxProcs();
	if( hasEfxFilters )
	{
		CreateWetDryFilters();
	}

	if( hasEfxFilters && openalDirectFilter != 0 )
	{
		qalFilterf( openalDirectFilter, AL_LOWPASS_GAIN, directFilterGain );
		qalFilterf( openalDirectFilter, AL_LOWPASS_GAINHF, occlusionFilter.directGainHF );
		alSourcei( openalSource, AL_DIRECT_FILTER, openalDirectFilter );
		alSourcef( openalSource, AL_GAIN, effectiveGain );
	}
	else
	{
		alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
	}

	if( hasEfxFilters && openalAuxFilter != 0 && soundSystemLocal.hardware.HasEFX() && soundSystemLocal.hardware.GetAuxEffectSlot() != 0 && wetFilterGain > 0.0f )
	{
		qalFilterf( openalAuxFilter, AL_LOWPASS_GAIN, wetFilterGain );
		qalFilterf( openalAuxFilter, AL_LOWPASS_GAINHF, occlusionFilter.wetGainHF );
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, soundSystemLocal.hardware.GetAuxEffectSlot(), 0, openalAuxFilter );
	}
	else if( hasEfxFilters )
	{
		alSource3i( openalSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	}
#else
	alSourcef( openalSource, AL_GAIN, effectiveGain * directFilterGain );
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

	nextQueuedSample = nextSample;
	nextQueuedBuffer = nextBuffer;
	nextQueuedOffset = 0;
	Update();
}

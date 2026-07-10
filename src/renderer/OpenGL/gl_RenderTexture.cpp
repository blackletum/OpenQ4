// RenderTexture.cpp
//



#include "../tr_local.h"
#include "../GLDebugScope.h"

static const GLuint INVALID_RENDER_TEXTURE_HANDLE = static_cast<GLuint>( -1 );

static GLenum R_CubeFaceTarget( int cubeFace ) {
	const int clampedFace = idMath::ClampInt( 0, 5, cubeFace );
	return GL_TEXTURE_CUBE_MAP_POSITIVE_X + clampedFace;
}

static bool R_IsRenderTextureHandleCurrent( GLuint handle, int handleGeneration ) {
	if ( !glConfig.isInitialized ) {
		return false;
	}
	// Never query a name from a destroyed context. GL object namespaces are
	// recycled, so the same numeric name may now identify an unrelated FBO.
	if ( handleGeneration != tr.glContextGeneration ) {
		return false;
	}
	return handle != 0 && handle != INVALID_RENDER_TEXTURE_HANDLE;
}

static GLuint R_GetAttachmentHandle( idImage *image ) {
	return ( image != nullptr ) ? image->GetDeviceHandle() : INVALID_RENDER_TEXTURE_HANDLE;
}

static void R_SetRenderTextureDrawBuffers( int colorImageCount ) {
	if ( colorImageCount > 0 ) {
		GLenum drawBuffers[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
		if ( colorImageCount > static_cast<int>( sizeof( drawBuffers ) / sizeof( drawBuffers[0] ) ) ) {
			common->FatalError( "idRenderTexture: Too many render targets!" );
		}
		glDrawBuffers( colorImageCount, drawBuffers );
		glReadBuffer( GL_COLOR_ATTACHMENT0 );
	} else {
		glDrawBuffer( GL_NONE );
		glReadBuffer( GL_NONE );
	}
}

static uint64_t R_GetAttachmentGeneration( idImage *image ) {
	return ( image != nullptr ) ? image->GetStorageGeneration() : 0;
}

/*
========================
idRenderTexture::idRenderTexture
========================
*/
idRenderTexture::idRenderTexture(idImage *colorImage, idImage *depthImage) {
	deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
	cachedDepthHandle = INVALID_RENDER_TEXTURE_HANDLE;
	cachedDepthGeneration = 0;
	if (colorImage != nullptr)
	{
		AddRenderImage(colorImage);
	}
	this->depthImage = depthImage;
}

/*
================
idRenderTexture::SetDebugLabel
================
*/
void idRenderTexture::SetDebugLabel( const char *label ) {
	debugLabel = ( label != NULL ) ? label : "";
	if ( glConfig.isInitialized && debugLabel.Length() > 0 ) {
		EnsureDeviceHandle();
		ApplyDebugLabel();
	}
}

/*
================
idRenderTexture::ApplyDebugLabel
================
*/
void idRenderTexture::ApplyDebugLabel( void ) const {
	if ( debugLabel.Length() <= 0 || !HasCurrentDeviceHandle() ) {
		return;
	}

	R_GLDebug_LabelFramebuffer( deviceHandle, debugLabel.c_str() );

	for ( int i = 0; i < colorImages.Num(); i++ ) {
		if ( colorImages[i] == NULL || !colorImages[i]->IsLoaded() ) {
			continue;
		}
		char label[256];
		idStr::snPrintf(
			label,
			sizeof( label ),
			"%s color%d %s",
			debugLabel.c_str(),
			i,
			colorImages[i]->GetName() );
		R_GLDebug_LabelTexture( colorImages[i]->GetDeviceHandle(), label );
	}

	if ( depthImage != NULL && depthImage->IsLoaded() ) {
		char label[256];
		idStr::snPrintf(
			label,
			sizeof( label ),
			"%s depth %s",
			debugLabel.c_str(),
			depthImage->GetName() );
		R_GLDebug_LabelTexture( depthImage->GetDeviceHandle(), label );
	}
}

/*
========================
idRenderTexture::~idRenderTexture
========================
*/
idRenderTexture::~idRenderTexture() {
	ReleaseDeviceHandle();
}

/*
================
idRenderTexture::HasCurrentDeviceHandle
================
*/
bool idRenderTexture::HasCurrentDeviceHandle( void ) const {
	// The render texture owns this name.  Generation validation prevents stale
	// context aliases without putting a synchronous glIsFramebuffer query on
	// every bind and handle lookup.
	return R_IsRenderTextureHandleCurrent( deviceHandle, deviceHandleGeneration );
}

/*
================
idRenderTexture::ReleaseDeviceHandle

Stale-generation names are forgotten without querying or deleting them. The
old context already owned their lifetime, and the numeric name may alias an
unrelated object in the current context.
================
*/
void idRenderTexture::ReleaseDeviceHandle( void ) {
	if ( glConfig.isInitialized &&
		deviceHandleGeneration == tr.glContextGeneration &&
		deviceHandle != 0 && deviceHandle != INVALID_RENDER_TEXTURE_HANDLE &&
		glDeleteFramebuffers != NULL ) {
		glDeleteFramebuffers( 1, &deviceHandle );
	}
	deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
	deviceHandleGeneration = -1;
	validatedCubeFaces = 0;
}
/*
================
idRenderTexture::AddRenderImage
================
*/
void idRenderTexture::AddRenderImage(idImage *image) {
	if (deviceHandle != INVALID_RENDER_TEXTURE_HANDLE) {
		common->FatalError("idRenderTexture::AddRenderImage: Can't add render image after FBO has been created!");
	}

	colorImages.Append(image);
	cachedColorHandles.Append( INVALID_RENDER_TEXTURE_HANDLE );
	cachedColorGenerations.Append( 0 );
}

/*
================
idRenderTexture::NeedsAttachmentRefresh
================
*/
bool idRenderTexture::NeedsAttachmentRefresh( void ) const {
	if ( cachedColorHandles.Num() != colorImages.Num() || cachedColorGenerations.Num() != colorImages.Num() ) {
		return true;
	}

	for ( int i = 0; i < colorImages.Num(); i++ ) {
		if ( cachedColorHandles[i] != R_GetAttachmentHandle( colorImages[i] ) ||
			cachedColorGenerations[i] != R_GetAttachmentGeneration( colorImages[i] ) ) {
			return true;
		}
	}

	return cachedDepthHandle != R_GetAttachmentHandle( depthImage ) ||
		cachedDepthGeneration != R_GetAttachmentGeneration( depthImage );
}

/*
================
idRenderTexture::CaptureAttachmentHandles
================
*/
void idRenderTexture::CaptureAttachmentHandles( void ) {
	cachedColorHandles.SetNum( colorImages.Num() );
	cachedColorGenerations.SetNum( colorImages.Num() );
	for ( int i = 0; i < colorImages.Num(); i++ ) {
		cachedColorHandles[i] = R_GetAttachmentHandle( colorImages[i] );
		cachedColorGenerations[i] = R_GetAttachmentGeneration( colorImages[i] );
	}
	cachedDepthHandle = R_GetAttachmentHandle( depthImage );
	cachedDepthGeneration = R_GetAttachmentGeneration( depthImage );
}

/*
================
idRenderTexture::EnsureDeviceHandle
================
*/
void idRenderTexture::EnsureDeviceHandle( void ) {
	if ( deviceHandle != INVALID_RENDER_TEXTURE_HANDLE && deviceHandleGeneration != tr.glContextGeneration ) {
		// Context loss invalidated the object. Do not call glIsFramebuffer or
		// glDeleteFramebuffers on the old numeric name in the new namespace.
		deviceHandle = INVALID_RENDER_TEXTURE_HANDLE;
		deviceHandleGeneration = -1;
	}

	// Scratch images can be purged/reallocated in place when runtime renderer settings
	// change. Rebuild the FBO attachments when that happens, even if the dimensions match.
	if ( HasCurrentDeviceHandle() && !NeedsAttachmentRefresh() ) {
		return;
	}

	ReleaseDeviceHandle();
	InitRenderTexture();
}

/*
================
idRenderTexture::InitRenderTexture
================
*/
void idRenderTexture::InitRenderTexture(void) {
	if ( !glConfig.isInitialized ) {
		return;
	}
	if ( glGenFramebuffers == NULL || glBindFramebuffer == NULL || glCheckFramebufferStatus == NULL ) {
		if ( !knownIncomplete ) {
			common->Warning( "idRenderTexture::InitRenderTexture: framebuffer entry points are unavailable for '%s'", debugLabel.c_str() );
		}
		knownIncomplete = true;
		ReleaseDeviceHandle();
		return;
	}

	ReleaseDeviceHandle();

	GLuint generatedHandle = 0;
	glGenFramebuffers( 1, &generatedHandle );
	if ( generatedHandle == 0 || generatedHandle == INVALID_RENDER_TEXTURE_HANDLE ) {
		if ( generatedHandle == INVALID_RENDER_TEXTURE_HANDLE && glDeleteFramebuffers != NULL ) {
			glDeleteFramebuffers( 1, &generatedHandle );
		}
		if ( !knownIncomplete ) {
			common->Warning( "idRenderTexture::InitRenderTexture: failed to allocate framebuffer '%s'", debugLabel.c_str() );
		}
		knownIncomplete = true;
		return;
	}
	deviceHandle = generatedHandle;
	deviceHandleGeneration = tr.glContextGeneration;
	glBindFramebuffer(GL_FRAMEBUFFER, deviceHandle);

	bool isTexture3D = false;
	if ((colorImages.Num() > 0 && colorImages[0]->GetOpts().textureType == TT_CUBIC) || ((depthImage != nullptr) && depthImage->GetOpts().textureType == TT_CUBIC))
	{
		isTexture3D = true;
	}
	
	if (!isTexture3D)
	{
		for (int i = 0; i < colorImages.Num(); i++) {
			if (colorImages[i]->GetOpts().numMSAASamples == 0)
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorImages[i]->GetDeviceHandle(), 0);
			}
			else
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D_MULTISAMPLE, colorImages[i]->GetDeviceHandle(), 0);
			}
		}

		if (depthImage != nullptr) {
			if (depthImage->GetOpts().numMSAASamples == 0)
			{
				if (depthImage->GetOpts().format == FMT_DEPTH) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthImage->GetDeviceHandle(), 0);
				}
				else if (depthImage->GetOpts().format == FMT_DEPTH_STENCIL) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthImage->GetDeviceHandle(), 0);
				}
				else {
					common->FatalError("idRenderTexture::InitRenderTexture: Unknown depth buffer format!");
				}
			}
			else
			{
				if (depthImage->GetOpts().format == FMT_DEPTH) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthImage->GetDeviceHandle(), 0);
				}
				else if (depthImage->GetOpts().format == FMT_DEPTH_STENCIL) {
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, depthImage->GetDeviceHandle(), 0);
				}
				else {
					common->FatalError("idRenderTexture::InitRenderTexture: Unknown depth buffer format!");
				}
			}
		}

		R_SetRenderTextureDrawBuffers( colorImages.Num() );
	}
	else
	{
		if (colorImages.Num() > 0)
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, colorImages[0]->GetDeviceHandle(), 0);
			glDrawBuffer( GL_COLOR_ATTACHMENT0 );
			glReadBuffer( GL_COLOR_ATTACHMENT0 );
		}
		else
		{
			glDrawBuffer( GL_NONE );
			glReadBuffer( GL_NONE );
		}

		if (depthImage != nullptr) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X, depthImage->GetDeviceHandle(), 0);
		}
	}


	const GLenum framebufferStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( framebufferStatus != GL_FRAMEBUFFER_COMPLETE ) {
		if ( allowIncomplete ) {
			if ( !knownIncomplete ) {
				common->Warning( "idRenderTexture::InitRenderTexture: framebuffer '%s' is incomplete; callers will fall back", debugLabel.c_str() );
			}
			knownIncomplete = true;
		} else {
			common->FatalError("idRenderTexture::InitRenderTexture: Failed to create rendertexture!");
		}
	} else {
		knownIncomplete = false;
		if ( isTexture3D ) {
			validatedCubeFaces |= 1u;
		}
	}

	CaptureAttachmentHandles();
	ApplyDebugLabel();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
================
idRenderTexture::GetDeviceHandle
================
*/
GLuint idRenderTexture::GetDeviceHandle(void) {
	EnsureDeviceHandle();
	return HasCurrentDeviceHandle() ? deviceHandle : 0;
}

/*
================
idRenderTexture::MakeCurrent
================
*/
void idRenderTexture::MakeCurrent(void) {
	if ( !glConfig.isInitialized || glBindFramebuffer == NULL ) {
		return;
	}
	EnsureDeviceHandle();
	if ( !HasCurrentDeviceHandle() ) {
		return;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, deviceHandle);
	R_SetRenderTextureDrawBuffers( colorImages.Num() );
}

/*
================
idRenderTexture::MakeCurrent
================
*/
void idRenderTexture::MakeCurrent( int cubeFace ) {
	if ( !glConfig.isInitialized || glBindFramebuffer == NULL ) {
		return;
	}
	EnsureDeviceHandle();
	if ( !HasCurrentDeviceHandle() ) {
		return;
	}
	glBindFramebuffer( GL_FRAMEBUFFER, deviceHandle );

	const int clampedCubeFace = idMath::ClampInt( 0, 5, cubeFace );
	const GLenum faceTarget = R_CubeFaceTarget( clampedCubeFace );
	if ( colorImages.Num() > 0 && colorImages[0]->GetOpts().textureType == TT_CUBIC ) {
		for ( int i = 0; i < colorImages.Num(); i++ ) {
			if ( colorImages[i]->GetOpts().textureType != TT_CUBIC ) {
				common->FatalError( "idRenderTexture::MakeCurrent: Mixed cubemap/non-cubemap color targets are unsupported!" );
			}
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, faceTarget, colorImages[i]->GetDeviceHandle(), 0 );
		}
		R_SetRenderTextureDrawBuffers( colorImages.Num() );
	} else {
		R_SetRenderTextureDrawBuffers( 0 );
	}

	if ( depthImage != nullptr && depthImage->GetOpts().textureType == TT_CUBIC ) {
		if ( depthImage->GetOpts().format == FMT_DEPTH ) {
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, faceTarget, depthImage->GetDeviceHandle(), 0 );
		} else if ( depthImage->GetOpts().format == FMT_DEPTH_STENCIL ) {
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, faceTarget, depthImage->GetDeviceHandle(), 0 );
		} else {
			common->FatalError( "idRenderTexture::MakeCurrent: Unknown depth buffer format for cubemap target!" );
		}
	}

	const unsigned int faceBit = 1u << clampedCubeFace;
	if ( ( validatedCubeFaces & faceBit ) != 0 ) {
		knownIncomplete = false;
		return;
	}

	if ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE ) {
		if ( allowIncomplete ) {
			if ( !knownIncomplete ) {
				common->Warning( "idRenderTexture::MakeCurrent: cubemap framebuffer face '%s' is incomplete; callers will fall back", debugLabel.c_str() );
			}
			knownIncomplete = true;
		} else {
			common->FatalError( "idRenderTexture::MakeCurrent: Cubemap framebuffer face is incomplete!" );
		}
	} else {
		knownIncomplete = false;
		validatedCubeFaces |= faceBit;
	}
}

/*
================
idRenderTexture::BindNull
================
*/
void idRenderTexture::BindNull(void) {
	if ( glConfig.isInitialized && glBindFramebuffer != NULL ) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

/*
================
idRenderTexture::Resize
================
*/
void idRenderTexture::Resize(int width, int height) {
	idImage *target = nullptr;

	if (colorImages.Num() > 0) {
		target = colorImages[0];
	}
	else {
		target = depthImage;
	}
	if ( target == nullptr ) {
		common->Warning( "idRenderTexture::Resize: render texture has no attachments" );
		return;
	}

	if (target->GetOpts().width == width && target->GetOpts().height == height) {
		EnsureDeviceHandle();
		return;
	}

	for(int i = 0; i < colorImages.Num(); i++) {
		colorImages[i]->Resize(width, height);
	}

	if (depthImage != nullptr) {
		depthImage->Resize(width, height);
	}

	InitRenderTexture();
}

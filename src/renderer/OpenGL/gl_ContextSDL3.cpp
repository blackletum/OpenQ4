// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	SDL3 GL context half of the window/context seam (Phase B5b,
	docs/dev/plans/2026-07-16-vulkan-renderer-phase-b.md).

	This file owns the rendering context: candidate negotiation, context
	creation/teardown, current-ness, buffer swaps, swap interval, and
	extension lookup. The engine owns the window/display/input layer and is
	reached exclusively through renderWindowServices_t, so the Phase B8
	module split only has to change who compiles this file.

	Until B8 it is compiled as part of the SDL3 platform backend translation
	unit (sdl3_backend.cpp defines OPENQ4_SDL3_CONTEXT_IMPL and includes it
	at its tail), which supplies the SDL/QGL/wgl declarations; standalone
	glob compilation produces an empty TU.

===============================================================================
*/

#ifdef OPENQ4_SDL3_CONTEXT_IMPL

static SDL_Window *s_glWindow = NULL;
static SDL_GLContext s_glContext = NULL;
static void *s_glHDC = NULL;
static const renderWindowServices_t *s_glWindowServices = NULL;

#if defined(OPENQ4_SDL3_POSIX_HOST)
// The SDL3 POSIX backends reuse this translation unit but do not provide the
// legacy platform GL logging hooks from the native GLX/Cocoa paths.
void GLimp_EnableLogging(bool enable) {
	static bool loggingEnabled = false;
	if (enable != loggingEnabled) {
		common->DPrintf("GLimp_EnableLogging - unavailable for SDL3 POSIX backend\n");
		loggingEnabled = enable;
	}
}
#endif

static void SDL3_WindowParmsFromGlimpParms(const glimpParms_t &src, renderWindowParms_t &dst) {
	memset(&dst, 0, sizeof(dst));
	dst.width = src.width;
	dst.height = src.height;
	dst.fullScreen = src.fullScreen;
	dst.borderless = src.borderless;
	dst.hiddenWindow = src.hiddenWindow;
	dst.stereo = src.stereo;
	dst.displayHz = src.displayHz;
	dst.multiSamples = src.multiSamples;
}

static bool SDL3_EnsureGLContextCurrent(const char *operation) {
	if (!s_glWindow || !s_glContext) {
		return false;
	}

	if (SDL_GL_GetCurrentWindow() == s_glWindow && SDL_GL_GetCurrentContext() == s_glContext) {
		return true;
	}

	if (!SDL_GL_MakeCurrent(s_glWindow, s_glContext)) {
		common->Printf("SDL3: failed to make GL context current for %s: %s\n", operation ? operation : "operation", SDL_GetError());
		if (s_glWindowServices != NULL && s_glWindowServices->CountContextError != NULL) {
			s_glWindowServices->CountContextError();
		}
		return false;
	}

	return true;
}

static bool SDL3_ApplySwapInterval(void) {
	if (!s_glWindow || !s_glContext) {
		return false;
	}

	const int requestedInterval = R_GetEffectiveSwapInterval();
	if (!SDL_GL_SetSwapInterval(requestedInterval)) {
		common->Printf("SDL3: failed to set swap interval %d: %s\n", requestedInterval, SDL_GetError());
		return false;
	}

	int actualInterval = 0;
	if (!SDL_GL_GetSwapInterval(&actualInterval)) {
		common->Printf("SDL3: swap interval set to %d, but query failed: %s\n", requestedInterval, SDL_GetError());
	} else if (actualInterval == requestedInterval) {
		common->Printf("SDL3: swap interval set to %d\n", actualInterval);
	} else {
		common->Printf("SDL3: requested swap interval %d, driver reports %d\n", requestedInterval, actualInterval);
	}

	return true;
}

static void SDL3_LoadWGLExtensions(void) {
#if defined(OPENQ4_SDL3_POSIX_HOST)
	glConfig.wgl_extensions_string = "";
	r_swapInterval.SetModified();
#else
	wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)GLimp_ExtensionPointer("wglGetExtensionsStringARB");
	if (wglGetExtensionsStringARB && s_glHDC) {
		glConfig.wgl_extensions_string = (const char *)wglGetExtensionsStringARB((HDC)s_glHDC);
	} else {
		glConfig.wgl_extensions_string = "";
	}

	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)GLimp_ExtensionPointer("wglSwapIntervalEXT");
	r_swapInterval.SetModified();

	wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)GLimp_ExtensionPointer("wglGetPixelFormatAttribivARB");
	wglGetPixelFormatAttribfvARB = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)GLimp_ExtensionPointer("wglGetPixelFormatAttribfvARB");
	wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)GLimp_ExtensionPointer("wglChoosePixelFormatARB");

	wglCreatePbufferARB = (PFNWGLCREATEPBUFFERARBPROC)GLimp_ExtensionPointer("wglCreatePbufferARB");
	wglGetPbufferDCARB = (PFNWGLGETPBUFFERDCARBPROC)GLimp_ExtensionPointer("wglGetPbufferDCARB");
	wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)GLimp_ExtensionPointer("wglReleasePbufferDCARB");
	wglDestroyPbufferARB = (PFNWGLDESTROYPBUFFERARBPROC)GLimp_ExtensionPointer("wglDestroyPbufferARB");
	wglQueryPbufferARB = (PFNWGLQUERYPBUFFERARBPROC)GLimp_ExtensionPointer("wglQueryPbufferARB");

	wglBindTexImageARB = (PFNWGLBINDTEXIMAGEARBPROC)GLimp_ExtensionPointer("wglBindTexImageARB");
	wglReleaseTexImageARB = (PFNWGLRELEASETEXIMAGEARBPROC)GLimp_ExtensionPointer("wglReleaseTexImageARB");
	wglSetPbufferAttribARB = (PFNWGLSETPBUFFERATTRIBARBPROC)GLimp_ExtensionPointer("wglSetPbufferAttribARB");
#endif
}

void GLimp_SetGamma(unsigned short red[256], unsigned short green[256], unsigned short blue[256]) {
	(void)red;
	(void)green;
	(void)blue;
}

bool GLimp_UseNativeGammaRamps(void) {
	return false;
}

static void SDL3_MoveCompatibilityFallbacksToFront(rendererContextCandidate_t *candidates, int candidateCount) {
	int insertIndex = 0;

	for (int i = 0; i < candidateCount; ++i) {
		if (candidates[i].explicitVersion || candidates[i].profile != RENDERER_CONTEXT_PROFILE_COMPATIBILITY) {
			continue;
		}

		if (i != insertIndex) {
			rendererContextCandidate_t fallback = candidates[i];
			memmove(&candidates[insertIndex + 1], &candidates[insertIndex], (i - insertIndex) * sizeof(candidates[0]));
			candidates[insertIndex] = fallback;
		}
		++insertIndex;
	}
}

static int SDL3_BuildGLContextCandidates(rendererContextCandidate_t *candidates, int maxCandidates) {
	const rendererTierPreference_t preference = RendererTierPreference_FromString(r_glTier.GetString());
	const bool keepAutoCompatibility = preference == RENDERER_TIER_PREF_AUTO;
	const int candidateCount = RendererContextLadder_Build(
		candidates,
		maxCandidates,
		preference,
		r_glDebugContext.GetBool(),
		keepAutoCompatibility);

	if (candidateCount > 0 && preference == RENDERER_TIER_PREF_AUTO
			&& s_glWindowServices != NULL && s_glWindowServices->PreferCompatibilityFallbackFirst != NULL) {
		const char *quirkMessage = NULL;
		if (s_glWindowServices->PreferCompatibilityFallbackFirst(&quirkMessage)) {
			SDL3_MoveCompatibilityFallbacksToFront(candidates, candidateCount);
			if (quirkMessage != NULL) {
				common->Printf("%s", quirkMessage);
			}
		}
	}

	return candidateCount;
}

static int SDL3_NormalizeMSAASampleFallback(const int samples) {
	if (samples <= 1) {
		return 0;
	}
	if (samples <= 2) {
		return 2;
	}
	if (samples <= 4) {
		return 4;
	}
	if (samples <= 8) {
		return 8;
	}
	return 16;
}

static int SDL3_BuildMSAASampleFallbacks(const int requestedSamples, int *fallbacks, const int maxFallbacks) {
	static const int sampleSteps[] = {16, 8, 4, 2, 0};
	const int normalizedSamples = SDL3_NormalizeMSAASampleFallback(requestedSamples);
	int fallbackCount = 0;

	if (fallbacks == NULL || maxFallbacks <= 0) {
		return 0;
	}

	for (int i = 0; i < static_cast<int>(sizeof(sampleSteps) / sizeof(sampleSteps[0])) && fallbackCount < maxFallbacks; ++i) {
		if (sampleSteps[i] <= normalizedSamples) {
			fallbacks[fallbackCount++] = sampleSteps[i];
		}
	}

	return fallbackCount > 0 ? fallbackCount : 1;
}

static void SDL3_BuildFramebufferDesc(const glimpParms_t &parms, const rendererContextCandidate_t &candidate, const int multiSamples, renderFramebufferDesc_t &desc) {
	memset(&desc, 0, sizeof(desc));
	desc.redBits = 8;
	desc.greenBits = 8;
	desc.blueBits = 8;
	desc.alphaBits = 8;
	desc.depthBits = 24;
	desc.stencilBits = 8;
	desc.doubleBuffer = true;
	desc.stereo = parms.stereo;
	desc.multiSamples = multiSamples;
	desc.explicitGLVersion = candidate.explicitVersion;
	desc.glMajor = candidate.major;
	desc.glMinor = candidate.minor;
	desc.glCoreProfile = candidate.profile == RENDERER_CONTEXT_PROFILE_CORE;
	desc.glDebugContext = candidate.debugContext;
}

static void SDL3_RecordGLContextCandidate(const rendererContextCandidate_t &candidate) {
	memset(&glConfig.contextRequest, 0, sizeof(glConfig.contextRequest));
	glConfig.contextRequest = candidate;
}

static const char *SDL3_GLProfileMaskName(int profileMask) {
	switch (profileMask) {
		case SDL_GL_CONTEXT_PROFILE_COMPATIBILITY:
			return "compatibility";
		case SDL_GL_CONTEXT_PROFILE_CORE:
			return "core";
		case SDL_GL_CONTEXT_PROFILE_ES:
			return "es";
		default:
			return "default";
	}
}

static void SDL3_LogGLContextAttributes(const int requestedMultiSamples, const int selectedMultiSamples) {
	int major = 0;
	int minor = 0;
	int profileMask = 0;
	int flags = 0;
	int multisampleBuffers = 0;
	int multisampleSamples = 0;

	const bool gotMajor = SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
	const bool gotMinor = SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
	const bool gotProfile = SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profileMask);
	const bool gotFlags = SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &flags);
	const bool gotMultisampleBuffers = SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &multisampleBuffers);
	const bool gotMultisampleSamples = SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &multisampleSamples);

	common->Printf(
		"SDL3: reported OpenGL context attributes: version=%s%d.%d profile=%s flags=%s0x%x\n",
		(gotMajor && gotMinor) ? "" : "<unreported> ",
		major,
		minor,
		gotProfile ? SDL3_GLProfileMaskName(profileMask) : "unreported",
		gotFlags ? "" : "<unreported> ",
		flags);
	common->Printf(
		"SDL3: reported OpenGL multisample attributes: requested=%d selected=%d actualBuffers=%s%d actualSamples=%s%d\n",
		requestedMultiSamples,
		selectedMultiSamples,
		gotMultisampleBuffers ? "" : "<unreported> ",
		multisampleBuffers,
		gotMultisampleSamples ? "" : "<unreported> ",
		multisampleSamples);
}

bool GLimp_Init(glimpParms_t parms) {
	const char *driverName;

	s_glWindowServices = Sys_GetRenderWindowServices();
	if (s_glWindowServices == NULL) {
		common->Printf("SDL3: no window services available for GL context creation\n");
		return false;
	}

	common->Printf("Initializing OpenGL subsystem (SDL3 backend)\n");
	if (!s_glWindowServices->PrepareWindowSystem()) {
		return false;
	}

	rendererContextCandidate_t contextCandidates[RENDERER_CONTEXT_LADDER_MAX_CANDIDATES];
	const int contextCandidateCount = SDL3_BuildGLContextCandidates(contextCandidates, static_cast<int>(sizeof(contextCandidates) / sizeof(contextCandidates[0])));
	if (contextCandidateCount <= 0) {
		common->Printf("SDL3: no OpenGL context candidates were generated for r_glTier %s\n", r_glTier.GetString());
		return false;
	}

	if (parms.hiddenWindow) {
		parms.fullScreen = false;
		parms.borderless = false;
		common->Printf("SDL3: creating hidden OpenGL render window\n");
	}

	const int requestedMultiSamples = SDL3_NormalizeMSAASampleFallback(parms.multiSamples);
	parms.multiSamples = requestedMultiSamples;
	int multiSampleFallbacks[5];
	const int multiSampleFallbackCount = SDL3_BuildMSAASampleFallbacks(
		requestedMultiSamples,
		multiSampleFallbacks,
		static_cast<int>(sizeof(multiSampleFallbacks) / sizeof(multiSampleFallbacks[0])));
	int selectedMultiSamples = 0;

	renderWindowParms_t windowParms;
	SDL3_WindowParmsFromGlimpParms(parms, windowParms);
	renderModuleWindowInfo_t windowInfo;
	memset(&windowInfo, 0, sizeof(windowInfo));
	bool reusedPreservedWindow = false;

	idStr lastContextError;
	s_glContext = NULL;
	for (int candidateIndex = 0; candidateIndex < contextCandidateCount; ++candidateIndex) {
		const rendererContextCandidate_t &candidate = contextCandidates[candidateIndex];
		for (int sampleIndex = 0; sampleIndex < multiSampleFallbackCount; ++sampleIndex) {
			const int candidateMultiSamples = multiSampleFallbacks[sampleIndex];
			renderFramebufferDesc_t framebufferDesc;
			SDL3_BuildFramebufferDesc(parms, candidate, candidateMultiSamples, framebufferDesc);

			if (!s_glWindowServices->CreateWindowForFramebuffer(&framebufferDesc, &windowParms, &windowInfo, &reusedPreservedWindow)) {
				lastContextError = SDL_GetError();
				common->Printf("SDL3: could not create window for OpenGL context %s with MSAA samples=%d: %s\n", candidate.label, candidateMultiSamples, lastContextError.c_str());
				continue;
			}
			s_glWindow = (SDL_Window *)windowInfo.sdlWindow;

			common->Printf("SDL3: trying OpenGL context %s with MSAA samples=%d\n", candidate.label, candidateMultiSamples);
			s_glContext = SDL_GL_CreateContext(s_glWindow);
			if (s_glContext && SDL_GL_MakeCurrent(s_glWindow, s_glContext)) {
				SDL3_RecordGLContextCandidate(candidate);
				selectedMultiSamples = candidateMultiSamples;
				common->Printf("SDL3: created OpenGL context %s with MSAA samples=%d\n", glConfig.contextRequest.label, selectedMultiSamples);
				break;
			}

			lastContextError = SDL_GetError();
			common->Printf("SDL3: OpenGL context %s with MSAA samples=%d failed: %s\n", candidate.label, candidateMultiSamples, lastContextError.c_str());
			if (s_glContext) {
				(void)SDL_GL_MakeCurrent(s_glWindow, NULL);
				(void)SDL_GL_DestroyContext(s_glContext);
				s_glContext = NULL;
			}
			if (!reusedPreservedWindow) {
				s_glWindowServices->DestroyAttemptWindow();
				s_glWindow = NULL;
			}
		}
		if (s_glContext) {
			break;
		}
	}
	if (!s_glContext) {
		common->Printf("SDL3: could not create OpenGL context: %s\n", lastContextError.Length() > 0 ? lastContextError.c_str() : SDL_GetError());
		return false;
	}

	if (!SDL_GL_MakeCurrent(s_glWindow, s_glContext)) {
		common->Printf("SDL3: could not make context current: %s\n", SDL_GetError());
		(void)SDL_GL_DestroyContext(s_glContext);
		s_glContext = NULL;
		if (!reusedPreservedWindow) {
			s_glWindowServices->DestroyAttemptWindow();
			s_glWindow = NULL;
		}
		return false;
	}
	if (selectedMultiSamples != requestedMultiSamples) {
		common->Printf("SDL3: r_multiSamples requested %d, using %d after context creation fallback\n", requestedMultiSamples, selectedMultiSamples);
		r_multiSamples.SetInteger(selectedMultiSamples);
		r_multiSamples.ClearModified();
		parms.multiSamples = selectedMultiSamples;
	}
	SDL3_LogGLContextAttributes(requestedMultiSamples, selectedMultiSamples);

#if defined(__linux__)
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "libGL.so.1";
#elif defined(__APPLE__)
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "OpenGL.framework";
#else
	driverName = r_glDriver.GetString()[0] ? r_glDriver.GetString() : "opengl32";
#endif
	if (!QGL_Init(driverName)) {
		common->Printf("^3GLimp_Init() could not load r_glDriver \"%s\"^0\n", driverName);
		GLimp_Shutdown();
		return false;
	}

	SDL3_WindowParmsFromGlimpParms(parms, windowParms);
	if (!s_glWindowServices->ApplyScreenParms(&windowParms)) {
		GLimp_Shutdown();
		return false;
	}

	s_glWindowServices->RefreshNativeWindowHandles(&windowInfo);
	s_glHDC = windowInfo.nativeDisplayHandle;
	SDL3_LoadWGLExtensions();
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		(void)SDL3_ApplySwapInterval();
	}

	s_glWindowServices->NotifyWindowReady();
	GLimp_EnableLogging((r_logFile.GetInteger() != 0));

	return true;
}

bool GLimp_SetScreenParms(glimpParms_t parms) {
	const renderWindowServices_t *windowServices = s_glWindowServices != NULL ? s_glWindowServices : Sys_GetRenderWindowServices();
	if (windowServices == NULL) {
		return false;
	}

	if (parms.hiddenWindow) {
		parms.fullScreen = false;
		parms.borderless = false;
	}

	renderWindowParms_t windowParms;
	SDL3_WindowParmsFromGlimpParms(parms, windowParms);
	if (!windowServices->ApplyScreenParms(&windowParms)) {
		return false;
	}

	if (s_glWindow && s_glContext && !SDL3_EnsureGLContextCurrent("screen parm change")) {
		return false;
	}

	renderModuleWindowInfo_t windowInfo;
	windowServices->RefreshNativeWindowHandles(&windowInfo);
	s_glHDC = windowInfo.nativeDisplayHandle;
	r_swapInterval.SetModified();
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		(void)SDL3_ApplySwapInterval();
	}

	return true;
}

void GLimp_Shutdown(void) {
	const renderWindowServices_t *windowServices = s_glWindowServices != NULL ? s_glWindowServices : Sys_GetRenderWindowServices();

	common->Printf("Shutting down OpenGL subsystem (SDL3 backend)\n");

	if (windowServices != NULL) {
		windowServices->BeginWindowTeardown();
	}

	if (s_glContext) {
		if (s_glWindow) {
			(void)SDL_GL_MakeCurrent(s_glWindow, NULL);
		}
		(void)SDL_GL_DestroyContext(s_glContext);
		s_glContext = NULL;
	}
	s_glHDC = NULL;

	if (windowServices != NULL) {
		windowServices->FinishWindowTeardown();
	}
	// the engine owns the window; whether it survived depends on the
	// preserve-window flag, so drop the cached pointer either way
	s_glWindow = NULL;

	QGL_Shutdown();
}

void GLimp_SwapBuffers(void) {
	if (r_swapInterval.IsModified()) {
		r_swapInterval.ClearModified();
		(void)SDL3_ApplySwapInterval();
	}

	if (SDL3_EnsureGLContextCurrent("swap buffers") && !SDL_GL_SwapWindow(s_glWindow)) {
		common->Printf("SDL3: failed to swap window buffers: %s\n", SDL_GetError());
	}
}

void GLimp_ActivateContext(void) {
	(void)SDL3_EnsureGLContextCurrent("activate context");
}

bool GLimp_EnsureActiveContext(const char *operation) {
	return SDL3_EnsureGLContextCurrent(operation);
}

void GLimp_DeactivateContext(void) {
	if (!SDL3_EnsureGLContextCurrent("deactivate context")) {
		return;
	}

	glFinish();
	if (!SDL_GL_MakeCurrent(s_glWindow, NULL)) {
		if (s_glWindowServices != NULL && s_glWindowServices->CountContextError != NULL) {
			s_glWindowServices->CountContextError();
		}
	}
}

#if defined(OPENQ4_SDL3_LINUX_HOST)
typedef void ( *openQ4GlewProcAddress_t ) (void);

extern "C" openQ4GlewProcAddress_t OpenQ4_GlewGetProcAddress(const unsigned char *name) {
	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	if (!SDL3_EnsureGLContextCurrent("GLEW proc lookup")) {
		return NULL;
	}

	return reinterpret_cast<openQ4GlewProcAddress_t>(
		SDL_GL_GetProcAddress(reinterpret_cast<const char *>(name)));
}
#endif

void *GLimp_ExtensionPointer(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	if (!SDL3_EnsureGLContextCurrent("extension proc lookup")) {
		return NULL;
	}

	void *proc = (void *)SDL_GL_GetProcAddress(name);
#if !defined(OPENQ4_SDL3_POSIX_HOST)
	if (!proc && qwglGetProcAddress) {
		proc = (void *)qwglGetProcAddress(name);
	}
#endif

	if (!proc) {
		common->Printf("Couldn't find proc address for: %s\n", name);
	}
	return proc;
}

#endif /* OPENQ4_SDL3_CONTEXT_IMPL */

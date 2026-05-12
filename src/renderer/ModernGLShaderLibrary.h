// Copyright (C) 2004 Id Software, Inc.
//

#ifndef __MODERN_GL_SHADER_LIBRARY_H__
#define __MODERN_GL_SHADER_LIBRARY_H__

#include "RendererCaps.h"

enum modernGLShaderProgramKind_t {
	MODERN_GL_SHADER_DEPTH = 0,
	MODERN_GL_SHADER_FLAT_MATERIAL,
	MODERN_GL_SHADER_PROGRAM_KIND_COUNT
};

const int MODERN_GL_SHADER_MAX_PROGRAMS = 8;

typedef struct modernGLShaderProgramInfo_s {
	modernGLShaderProgramKind_t	kind;
	int							glslVersion;
	unsigned int				program;
	int							frameBlockIndex;
	int							modelViewProjectionLocation;
	int							debugColorLocation;
	bool						linked;
	char						name[64];
} modernGLShaderProgramInfo_t;

typedef struct modernGLShaderLibraryStats_s {
	bool	available;
	bool	initialized;
	bool	frameConstantsReady;
	bool	depthProgramReady;
	bool	flatMaterialProgramReady;
	int		programCount;
	int		failedProgramCount;
	int		highestGLSLVersion;
	char	status[96];
} modernGLShaderLibraryStats_t;

void R_ModernGLShaderLibrary_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features );
void R_ModernGLShaderLibrary_Shutdown( void );
const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void );
const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion );
void R_ModernGLShaderLibrary_PrintGfxInfo( void );
bool RendererModernGLShaderLibrary_RunSelfTest( void );

#endif /* !__MODERN_GL_SHADER_LIBRARY_H__ */

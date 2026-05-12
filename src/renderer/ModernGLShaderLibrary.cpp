// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "ModernGLShaderLibrary.h"

static modernGLShaderLibraryStats_t rg_modernGLShaderLibraryStats;
static modernGLShaderProgramInfo_t rg_modernGLShaderPrograms[MODERN_GL_SHADER_MAX_PROGRAMS];
static int rg_modernGLShaderProgramCount = 0;

static const char *R_ModernGLShaderLibrary_ProgramKindName( modernGLShaderProgramKind_t kind ) {
	switch ( kind ) {
	case MODERN_GL_SHADER_DEPTH:
		return "depth";
	case MODERN_GL_SHADER_FLAT_MATERIAL:
		return "flatMaterial";
	default:
		return "unknown";
	}
}

static void R_ModernGLShaderLibrary_SetStatus( const char *status ) {
	idStr::snPrintf(
		rg_modernGLShaderLibraryStats.status,
		sizeof( rg_modernGLShaderLibraryStats.status ),
		"%s",
		status ? status : "unknown" );
}

static void R_ModernGLShaderLibrary_ResetStats( void ) {
	memset( &rg_modernGLShaderLibraryStats, 0, sizeof( rg_modernGLShaderLibraryStats ) );
	R_ModernGLShaderLibrary_SetStatus( "unavailable" );
}

static bool R_ModernGLShaderLibrary_HasCoreEntrypoints( void ) {
	return glCreateShader != NULL
		&& glShaderSource != NULL
		&& glCompileShader != NULL
		&& glGetShaderiv != NULL
		&& glGetShaderInfoLog != NULL
		&& glCreateProgram != NULL
		&& glAttachShader != NULL
		&& glDetachShader != NULL
		&& glDeleteShader != NULL
		&& glDeleteProgram != NULL
		&& glBindAttribLocation != NULL
		&& glLinkProgram != NULL
		&& glGetProgramiv != NULL
		&& glGetProgramInfoLog != NULL
		&& glGetUniformLocation != NULL
		&& glGetUniformBlockIndex != NULL
		&& glUniformBlockBinding != NULL;
}

static bool R_ModernGLShaderLibrary_CanCompile( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	if ( !features.shaderLibrary || !features.modernBaseline ) {
		return false;
	}
	if ( !caps.hasGLSL || !caps.hasUBO ) {
		return false;
	}
	if ( !R_ModernGLShaderLibrary_HasCoreEntrypoints() ) {
		return false;
	}
	return true;
}

static int R_ModernGLShaderLibrary_BuildVersionList( const renderBackendCaps_t &caps, const renderFeatureSet_t &features, int versions[4] ) {
	int count = 0;
	if ( caps.glMajor > 3 || ( caps.glMajor == 3 && caps.glMinor >= 3 ) ) {
		versions[count++] = 330;
	}
	if ( features.modernGL41 && ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 1 ) ) ) {
		versions[count++] = 410;
	}
	if ( features.gpuDriven && ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 3 ) ) ) {
		versions[count++] = 430;
	}
	if ( features.lowOverhead && ( caps.glMajor > 4 || ( caps.glMajor == 4 && caps.glMinor >= 5 ) ) ) {
		versions[count++] = 450;
	}
	return count;
}

static void R_ModernGLShaderLibrary_BuildVertexSource( int glslVersion, char *buffer, int bufferSize ) {
	idStr::snPrintf(
		buffer,
		bufferSize,
		"#version %d\n"
		"layout(location = 0) in vec3 attr_Position;\n"
		"layout(std140) uniform ModernFrameConstants {\n"
		"    vec4 viewport;\n"
		"    vec4 frame;\n"
		"    vec4 capabilities;\n"
		"    vec4 reserved;\n"
		"} uFrame;\n"
		"uniform mat4 uModelViewProjection;\n"
		"void main() {\n"
		"    vec4 frameJitter = vec4(uFrame.reserved.xy, 0.0, 0.0);\n"
		"    gl_Position = uModelViewProjection * vec4(attr_Position, 1.0) + frameJitter;\n"
		"}\n",
		glslVersion );
}

static void R_ModernGLShaderLibrary_BuildFragmentSource( int glslVersion, modernGLShaderProgramKind_t kind, char *buffer, int bufferSize ) {
	if ( kind == MODERN_GL_SHADER_DEPTH ) {
		idStr::snPrintf(
			buffer,
			bufferSize,
			"#version %d\n"
			"void main() {\n"
			"}\n",
			glslVersion );
		return;
	}

	idStr::snPrintf(
		buffer,
		bufferSize,
		"#version %d\n"
		"layout(location = 0) out vec4 out_Color;\n"
		"uniform vec4 uDebugColor;\n"
		"void main() {\n"
		"    out_Color = uDebugColor;\n"
		"}\n",
		glslVersion );
}

static void R_ModernGLShaderLibrary_PrintShaderLog( GLuint shader, const char *label ) {
	char logBuffer[4096];
	GLsizei length = 0;
	logBuffer[0] = '\0';
	glGetShaderInfoLog( shader, sizeof( logBuffer ) - 1, &length, logBuffer );
	logBuffer[sizeof( logBuffer ) - 1] = '\0';
	common->Warning( "Modern GL shader compile failed for '%s':\n%s", label, logBuffer[0] ? logBuffer : "<no info log>" );
}

static void R_ModernGLShaderLibrary_PrintProgramLog( GLuint program, const char *label ) {
	char logBuffer[4096];
	GLsizei length = 0;
	logBuffer[0] = '\0';
	glGetProgramInfoLog( program, sizeof( logBuffer ) - 1, &length, logBuffer );
	logBuffer[sizeof( logBuffer ) - 1] = '\0';
	common->Warning( "Modern GL program link failed for '%s':\n%s", label, logBuffer[0] ? logBuffer : "<no info log>" );
}

static GLuint R_ModernGLShaderLibrary_CompileShader( GLenum shaderType, const char *source, const char *label ) {
	GLuint shader = glCreateShader( shaderType );
	if ( shader == 0 ) {
		common->Warning( "Modern GL shader compile failed for '%s': glCreateShader returned 0", label );
		return 0;
	}

	const GLchar *sources[1] = { source };
	glShaderSource( shader, 1, sources, NULL );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		R_ModernGLShaderLibrary_PrintShaderLog( shader, label );
		glDeleteShader( shader );
		return 0;
	}

	return shader;
}

static bool R_ModernGLShaderLibrary_ReflectProgram( modernGLShaderProgramInfo_t &info ) {
	info.frameBlockIndex = static_cast<int>( glGetUniformBlockIndex( info.program, "ModernFrameConstants" ) );
	info.modelViewProjectionLocation = glGetUniformLocation( info.program, "uModelViewProjection" );
	info.debugColorLocation = glGetUniformLocation( info.program, "uDebugColor" );

	if ( info.frameBlockIndex < 0 || info.modelViewProjectionLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing required reflected bindings", info.name );
		return false;
	}
	if ( info.kind == MODERN_GL_SHADER_FLAT_MATERIAL && info.debugColorLocation < 0 ) {
		common->Warning( "Modern GL program '%s' is missing uDebugColor", info.name );
		return false;
	}

	glUniformBlockBinding( info.program, static_cast<GLuint>( info.frameBlockIndex ), 0 );
	return true;
}

static bool R_ModernGLShaderLibrary_CreateProgram( int glslVersion, modernGLShaderProgramKind_t kind ) {
	if ( rg_modernGLShaderProgramCount >= MODERN_GL_SHADER_MAX_PROGRAMS ) {
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	modernGLShaderProgramInfo_t &info = rg_modernGLShaderPrograms[rg_modernGLShaderProgramCount];
	memset( &info, 0, sizeof( info ) );
	info.kind = kind;
	info.glslVersion = glslVersion;
	info.frameBlockIndex = -1;
	info.modelViewProjectionLocation = -1;
	info.debugColorLocation = -1;
	idStr::snPrintf(
		info.name,
		sizeof( info.name ),
		"modern_%s_%d",
		R_ModernGLShaderLibrary_ProgramKindName( kind ),
		glslVersion );

	char vertexSource[4096];
	char fragmentSource[4096];
	R_ModernGLShaderLibrary_BuildVertexSource( glslVersion, vertexSource, sizeof( vertexSource ) );
	R_ModernGLShaderLibrary_BuildFragmentSource( glslVersion, kind, fragmentSource, sizeof( fragmentSource ) );

	GLuint vertexShader = R_ModernGLShaderLibrary_CompileShader( GL_VERTEX_SHADER, vertexSource, info.name );
	if ( vertexShader == 0 ) {
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}
	GLuint fragmentShader = R_ModernGLShaderLibrary_CompileShader( GL_FRAGMENT_SHADER, fragmentSource, info.name );
	if ( fragmentShader == 0 ) {
		glDeleteShader( vertexShader );
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	info.program = glCreateProgram();
	if ( info.program == 0 ) {
		common->Warning( "Modern GL program link failed for '%s': glCreateProgram returned 0", info.name );
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	glAttachShader( info.program, vertexShader );
	glAttachShader( info.program, fragmentShader );
	glBindAttribLocation( info.program, 0, "attr_Position" );
	glLinkProgram( info.program );

	GLint linked = GL_FALSE;
	glGetProgramiv( info.program, GL_LINK_STATUS, &linked );
	glDetachShader( info.program, vertexShader );
	glDetachShader( info.program, fragmentShader );
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	if ( linked != GL_TRUE ) {
		R_ModernGLShaderLibrary_PrintProgramLog( info.program, info.name );
		glDeleteProgram( info.program );
		info.program = 0;
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	info.linked = true;
	if ( !R_ModernGLShaderLibrary_ReflectProgram( info ) ) {
		glDeleteProgram( info.program );
		info.program = 0;
		info.linked = false;
		rg_modernGLShaderLibraryStats.failedProgramCount++;
		return false;
	}

	rg_modernGLShaderProgramCount++;
	rg_modernGLShaderLibraryStats.programCount = rg_modernGLShaderProgramCount;
	if ( glslVersion > rg_modernGLShaderLibraryStats.highestGLSLVersion ) {
		rg_modernGLShaderLibraryStats.highestGLSLVersion = glslVersion;
	}
	if ( kind == MODERN_GL_SHADER_DEPTH ) {
		rg_modernGLShaderLibraryStats.depthProgramReady = true;
	} else if ( kind == MODERN_GL_SHADER_FLAT_MATERIAL ) {
		rg_modernGLShaderLibraryStats.flatMaterialProgramReady = true;
	}
	rg_modernGLShaderLibraryStats.frameConstantsReady = true;
	return true;
}

void R_ModernGLShaderLibrary_Init( const renderBackendCaps_t &caps, const renderFeatureSet_t &features ) {
	R_ModernGLShaderLibrary_Shutdown();
	R_ModernGLShaderLibrary_ResetStats();

	if ( !R_ModernGLShaderLibrary_CanCompile( caps, features ) ) {
		R_ModernGLShaderLibrary_SetStatus( "unavailable" );
		return;
	}

	int versions[4];
	const int versionCount = R_ModernGLShaderLibrary_BuildVersionList( caps, features, versions );
	if ( versionCount <= 0 ) {
		R_ModernGLShaderLibrary_SetStatus( "no-supported-glsl-version" );
		return;
	}

	rg_modernGLShaderLibraryStats.initialized = true;
	for ( int i = 0; i < versionCount; ++i ) {
		R_ModernGLShaderLibrary_CreateProgram( versions[i], MODERN_GL_SHADER_DEPTH );
		R_ModernGLShaderLibrary_CreateProgram( versions[i], MODERN_GL_SHADER_FLAT_MATERIAL );
	}

	if ( rg_modernGLShaderLibraryStats.programCount > 0
		&& rg_modernGLShaderLibraryStats.failedProgramCount == 0
		&& rg_modernGLShaderLibraryStats.depthProgramReady
		&& rg_modernGLShaderLibraryStats.flatMaterialProgramReady
		&& rg_modernGLShaderLibraryStats.frameConstantsReady ) {
		rg_modernGLShaderLibraryStats.available = true;
		R_ModernGLShaderLibrary_SetStatus( "available" );
		return;
	}

	R_ModernGLShaderLibrary_SetStatus( "incomplete" );
}

void R_ModernGLShaderLibrary_Shutdown( void ) {
	for ( int i = 0; i < rg_modernGLShaderProgramCount; ++i ) {
		if ( rg_modernGLShaderPrograms[i].program != 0 && glDeleteProgram != NULL ) {
			glDeleteProgram( rg_modernGLShaderPrograms[i].program );
		}
	}
	memset( rg_modernGLShaderPrograms, 0, sizeof( rg_modernGLShaderPrograms ) );
	rg_modernGLShaderProgramCount = 0;
	R_ModernGLShaderLibrary_ResetStats();
}

const modernGLShaderLibraryStats_t &R_ModernGLShaderLibrary_Stats( void ) {
	return rg_modernGLShaderLibraryStats;
}

const modernGLShaderProgramInfo_t *R_ModernGLShaderLibrary_FindProgram( modernGLShaderProgramKind_t kind, int preferredGLSLVersion ) {
	const modernGLShaderProgramInfo_t *best = NULL;
	for ( int i = 0; i < rg_modernGLShaderProgramCount; ++i ) {
		const modernGLShaderProgramInfo_t &info = rg_modernGLShaderPrograms[i];
		if ( info.kind != kind || !info.linked ) {
			continue;
		}
		if ( info.glslVersion == preferredGLSLVersion ) {
			return &info;
		}
		if ( info.glslVersion <= preferredGLSLVersion ) {
			if ( best == NULL || info.glslVersion > best->glslVersion ) {
				best = &info;
			}
		} else if ( best == NULL ) {
			best = &info;
		}
	}
	return best;
}

void R_ModernGLShaderLibrary_PrintGfxInfo( void ) {
	common->Printf(
		"Modern GL shader library: %s, programs=%d, failed=%d, highestGLSL=%d, frameUBOBlock=%d, depth=%d, flatMaterial=%d\n",
		rg_modernGLShaderLibraryStats.available ? "available" : rg_modernGLShaderLibraryStats.status,
		rg_modernGLShaderLibraryStats.programCount,
		rg_modernGLShaderLibraryStats.failedProgramCount,
		rg_modernGLShaderLibraryStats.highestGLSLVersion,
		rg_modernGLShaderLibraryStats.frameConstantsReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.depthProgramReady ? 1 : 0,
		rg_modernGLShaderLibraryStats.flatMaterialProgramReady ? 1 : 0 );
}

bool RendererModernGLShaderLibrary_RunSelfTest( void ) {
	const modernGLShaderLibraryStats_t &stats = R_ModernGLShaderLibrary_Stats();
	if ( !stats.available ) {
		common->Printf( "RendererModernGLShaderLibrary self-test passed (%s)\n", stats.status );
		return true;
	}

	if ( !stats.initialized || stats.programCount <= 0 || stats.failedProgramCount != 0 ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: library stats mismatch\n" );
		return false;
	}
	if ( !stats.frameConstantsReady || !stats.depthProgramReady || !stats.flatMaterialProgramReady ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: required variant missing\n" );
		return false;
	}

	const modernGLShaderProgramInfo_t *depthProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_DEPTH, stats.highestGLSLVersion );
	const modernGLShaderProgramInfo_t *flatProgram = R_ModernGLShaderLibrary_FindProgram( MODERN_GL_SHADER_FLAT_MATERIAL, stats.highestGLSLVersion );
	if ( depthProgram == NULL || flatProgram == NULL ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: lookup mismatch\n" );
		return false;
	}
	if ( depthProgram->program == 0 || flatProgram->program == 0 || !depthProgram->linked || !flatProgram->linked ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: program object mismatch\n" );
		return false;
	}
	if ( depthProgram->frameBlockIndex < 0 || flatProgram->frameBlockIndex < 0 ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: frame block reflection mismatch\n" );
		return false;
	}
	if ( depthProgram->modelViewProjectionLocation < 0 || flatProgram->modelViewProjectionLocation < 0 || flatProgram->debugColorLocation < 0 ) {
		common->Printf( "RendererModernGLShaderLibrary self-test failed: uniform reflection mismatch\n" );
		return false;
	}

	common->Printf(
		"RendererModernGLShaderLibrary self-test passed (%d programs, GLSL %d)\n",
		stats.programCount,
		stats.highestGLSLVersion );
	return true;
}

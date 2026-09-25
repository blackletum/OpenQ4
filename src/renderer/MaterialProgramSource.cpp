// Copyright (C) 2026 DarkMatter Productions
// Shared authored GLSL source lookup; keep the same search order on all backends.
#include "../idlib/precompiled.h"
#pragma hdrstop
#include "tr_local.h"

static bool RB_PathHasGlprogsPrefix( const idStr &path ) {
	return idStr::Icmpn( path.c_str(), "glprogs/", 8 ) == 0;
}

static idStr RB_NormalizeGLSLPath( const idStr &path ) {
	idStr result = path;
	result.BackSlashesToSlashes();
	if ( !RB_PathHasGlprogsPrefix( result ) ) {
		idStr prefixed = "glprogs/";
		prefixed += result;
		return prefixed;
	}
	return result;
}

static bool RB_ReadGLSLSourcePair( const idStr &vertexPath, const idStr &fragmentPath, char **vertexBuffer, char **fragmentBuffer,
        int *vertexBytes, int *fragmentBytes, bool *foundAnySource ) {
	*vertexBuffer = NULL;
	*fragmentBuffer = NULL;

	const int vpBytes = fileSystem->ReadFile( vertexPath.c_str(), (void **)vertexBuffer, NULL );
	const int fpBytes = fileSystem->ReadFile( fragmentPath.c_str(), (void **)fragmentBuffer, NULL );
	if ( foundAnySource != NULL && ( vpBytes >= 0 || fpBytes >= 0 ) ) { *foundAnySource = true; }
	if ( *vertexBuffer == NULL || *fragmentBuffer == NULL ) {
		if ( *vertexBuffer != NULL ) { fileSystem->FreeFile( *vertexBuffer ); }
		if ( *fragmentBuffer != NULL ) { fileSystem->FreeFile( *fragmentBuffer ); }
		*vertexBuffer = NULL;
		*fragmentBuffer = NULL;
		return false;
	}

	if ( vertexBytes != NULL ) { *vertexBytes = vpBytes; }
	if ( fragmentBytes != NULL ) { *fragmentBytes = fpBytes; }
	return true;
}

bool R_FindGLSLSourcePair( const char *programName, idStr &vertexPath, idStr &fragmentPath, char **vertexBuffer, char **fragmentBuffer,
        int *vertexBytes, int *fragmentBytes, bool *foundAnySource ) {
	if ( vertexBytes != NULL ) { *vertexBytes = 0; }
	if ( fragmentBytes != NULL ) { *fragmentBytes = 0; }
	if ( foundAnySource != NULL ) { *foundAnySource = false; }
	idStr name = programName;
	name.BackSlashesToSlashes();

	idStr stripped = name;
	stripped.StripFileExtension();

	idStr ext;
	const char *dot = strrchr( name.c_str(), '.' );
	if ( dot != NULL ) {
		ext = dot + 1;
		ext.ToLower();
	}

	idStr vertexCandidates[10];
	idStr fragmentCandidates[10];
	int numCandidates = 0;

	if ( ext.Length() > 0 ) {
		if ( ext == "glsl" ) {
			vertexCandidates[numCandidates] = stripped + ".glslvp";
			fragmentCandidates[numCandidates++] = stripped + ".glslfp";
			vertexCandidates[numCandidates] = stripped + ".vs";
			fragmentCandidates[numCandidates++] = stripped + ".fs";
		} else if ( ext == "fs" ) {
			vertexCandidates[numCandidates] = stripped + ".vs";
			fragmentCandidates[numCandidates++] = name;
		} else if ( ext == "vs" ) {
			vertexCandidates[numCandidates] = name;
			fragmentCandidates[numCandidates++] = stripped + ".fs";
		} else if ( ext == "fp" ) {
			vertexCandidates[numCandidates] = stripped + ".vp";
			fragmentCandidates[numCandidates++] = name;
		} else if ( ext == "vp" ) {
			vertexCandidates[numCandidates] = name;
			fragmentCandidates[numCandidates++] = stripped + ".fp";
		}
	}

	vertexCandidates[numCandidates] = name + ".vs";
	fragmentCandidates[numCandidates++] = name + ".fs";
	vertexCandidates[numCandidates] = name + ".glslvp";
	fragmentCandidates[numCandidates++] = name + ".glslfp";
	vertexCandidates[numCandidates] = name + ".vp";
	fragmentCandidates[numCandidates++] = name + ".fp";
	vertexCandidates[numCandidates] = stripped + ".vs";
	fragmentCandidates[numCandidates++] = stripped + ".fs";
	vertexCandidates[numCandidates] = stripped + ".glslvp";
	fragmentCandidates[numCandidates++] = stripped + ".glslfp";
	vertexCandidates[numCandidates] = stripped + ".vp";
	fragmentCandidates[numCandidates++] = stripped + ".fp";

	for ( int i = 0; i < numCandidates; i++ ) {
		const idStr candidateVertex = RB_NormalizeGLSLPath( vertexCandidates[i] );
		const idStr candidateFragment = RB_NormalizeGLSLPath( fragmentCandidates[i] );
		if ( RB_ReadGLSLSourcePair( candidateVertex, candidateFragment, vertexBuffer, fragmentBuffer, vertexBytes, fragmentBytes, foundAnySource ) ) {
			vertexPath = candidateVertex;
			fragmentPath = candidateFragment;
			return true;
		}
	}

	return false;
}

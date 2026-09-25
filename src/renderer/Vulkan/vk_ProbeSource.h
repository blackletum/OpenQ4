// Copyright (C) 2026 DarkMatter Productions
#ifndef __VK_PROBE_SOURCE_H__
#define __VK_PROBE_SOURCE_H__

#include "../ModernSpecularProbeAtlas.h"
#include "../PBREnvironment.h"

struct vkProbeSourceStamp_t {
	std::uint64_t storageGeneration;
	std::uint64_t uploadGeneration;
	unsigned int handle;
	unsigned int imageGeneration;
	int faceSize;
};

// Immutable, fully uploaded authored cubemaps only. Reads mip zero in native
// +X,-X,+Y,-Y,+Z,-Z order, decodes ordinary sRGB radiance once, preserves
// finite RGBA16F radiance, and clamps negative HDR to zero like the GL atlas.
// Output and stamp are cleared on every failure. A cache miss synchronously
// submits/waits the upload queue; this never submits or resets a scene frame.
modernSpecularProbeAtlasReject_t VK_ProbeSource_Read( const idImage *image,
	openq4PBR::Cube &cube, vkProbeSourceStamp_t &stamp );
bool VK_ProbeSource_Matches( const idImage *image, const vkProbeSourceStamp_t &stamp );
bool VK_ProbeSource_RunSelfTest( void );

#endif

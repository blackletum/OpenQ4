// Copyright (C) 2026 DarkMatter Productions
#ifndef OPENQ4_VK_MATERIALPROGRAMS_H
#define OPENQ4_VK_MATERIALPROGRAMS_H
#include "../materialprogram/GLSLCompiler.h"

vkGLSLProgramFamily_t VK_MaterialPrograms_NativeFamily( const char *program );
bool VK_MaterialPrograms_Validate( const newShaderStage_t *stage );
bool VK_MaterialPrograms_NeedsStencil( const idMaterial *material, const float *registers );
bool VK_Exec_BuildAuthoredMaterialUniforms( const viewDef_t *viewDef, const drawSurf_t *surf,
    const shaderStage_t *stage, const drawInteraction_t *interaction, oq4material::UniformBlock &block );
bool VK_MaterialPrograms_Bind( const newShaderStage_t *stage,
    const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor,
    const drawInteraction_t *interaction = NULL );
void VK_MaterialPrograms_Reload();
void VK_MaterialPrograms_Report();
void VK_MaterialPrograms_BeginFrame( int slot );
void VK_MaterialPrograms_Shutdown();

#endif

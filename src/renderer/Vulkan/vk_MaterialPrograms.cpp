// Copyright (C) 2026 DarkMatter Productions
#ifdef OPENQ4_RENDERER_VK_MODULE
#include "../../idlib/precompiled.h"
#pragma hdrstop
#include "../tr_local.h"
#include "../../idlib/CryptoHash.h"
#include "VulkanDevice.h"
#include "vk_Image.h"
#include "vk_ExecutorHooks.h"
#include "vk_MaterialPrograms.h"
#include "vk_MaterialSourceSignatures.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int MaxPrograms = 256;
constexpr int MaxDraws = 2048;
constexpr size_t MaxCompilerBytes = 32 * 1024 * 1024;
struct Program {
    std::string name, key, vertexSource, fragmentSource;
    oq4material::CompileResult compiled;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    bool sourcePairFound = false, valid = false, warnedResource = false, loggedDraw = false, loggedLightingDraw = false;
    int identity = 0;
};
std::vector<std::unique_ptr<Program>> programs;
std::map<std::string, int> currentPrograms;
std::map<std::string, vkGLSLProgramFamily_t> nativeFamilies;
size_t compilerBytes = 0;
bool warnedCapacity = false;
VkDescriptorSetLayout uniformLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkDescriptorPool pools[VK_FRAMES_IN_FLIGHT] = {};
int drawCounts[VK_FRAMES_IN_FLIGHT] = {};
bool poolReady[VK_FRAMES_IN_FLIGHT] = {};

std::string SourceDigest( const char *source, int bytes ) {
    if ( source == NULL || bytes <= 0 || static_cast<size_t>( bytes ) > oq4material::MaxSourceBytes ) { return ""; }
    std::string normalized;
    normalized.reserve( bytes );
    for ( int i = 0; i < bytes; ++i ) {
        if ( source[i] == '\r' && i + 1 < bytes && source[i + 1] == '\n' ) { continue; }
        normalized += source[i];
    }
    std::uint8_t digest[idCrypto::SHA256_DIGEST_BYTES];
    idCrypto::SHA256( normalized.data(), normalized.size(), digest );
    static const char hex[] = "0123456789abcdef";
    std::string result;
    for ( std::uint8_t value : digest ) { result += hex[value >> 4]; result += hex[value & 15]; }
    return result;
}

bool CacheFull() {
    if ( programs.size() < MaxPrograms ) { return false; }
    if ( !warnedCapacity ) { common->Warning( "Vulkan: authored material program cache exhausted" ); warnedCapacity = true; }
    return true;
}

bool Fail( Program &program, const char *reason ) {
    if ( !program.warnedResource ) {
        common->Warning( "Vulkan authored GLSL '%s': %s", program.name.c_str(), reason );
        program.warnedResource = true;
    }
    return false;
}

int ParameterComponents( const newShaderStage_t *stage, int slot ) {
    switch ( stage->shaderParmBindings[slot] ) {
    case GLSL_SHADERPARM_REGISTERS: return stage->shaderParmNumRegisters[slot];
    case GLSL_SHADERPARM_POSTPROCESS_INV_TEX_SIZE:
    case GLSL_SHADERPARM_POSTPROCESS_TEX_SIZE:
    case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_ORIGIN:
    case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_SIZE:
    case GLSL_SHADERPARM_CURRENT_RENDER_TEXTURE_SCALE: return 2;
    default: return 4;
    }
}

Program *Find( const newShaderStage_t *stage ) {
    if ( stage == NULL || !stage->glslProgram ) { return NULL; }
    // Binding names, order and semantic types distinguish shared program names.
    // Numeric register values are per draw and must not cause recompilation.
    std::string key = stage->glslProgramName;
    for ( int i = 0; i < stage->numShaderParms; ++i ) {
        key += "|p:" + std::string( stage->shaderParmNames[i] ) + ":" + std::to_string( stage->shaderParmBindings[i] )
            + ":" + std::to_string( stage->shaderParmNumRegisters[i] );
    }
    for ( int i = 0; i < stage->numShaderTextures; ++i ) { key += "|t:" + std::string( stage->shaderTextureNames[i] ); }
    auto found = currentPrograms.find( key );
    if ( found != currentPrograms.end() ) { return programs[found->second].get(); }
    oq4material::CompileRequest request;
    char *vertex = NULL, *fragment = NULL;
    int vertexBytes = 0, fragmentBytes = 0;
    idStr vertexPath, fragmentPath;
    const bool sourcePairFound = R_FindGLSLSourcePair( stage->glslProgramName, vertexPath, fragmentPath,
        &vertex, &fragment, &vertexBytes, &fragmentBytes );
    if ( sourcePairFound ) {
        request.vertexName = vertexPath.c_str(); request.fragmentName = fragmentPath.c_str();
        if ( vertexBytes > 0 && fragmentBytes > 0
            && static_cast<size_t>( vertexBytes ) <= oq4material::MaxSourceBytes
            && static_cast<size_t>( fragmentBytes ) <= oq4material::MaxSourceBytes ) {
            request.vertexSource.assign( vertex, vertexBytes );
            request.fragmentSource.assign( fragment, fragmentBytes );
        }
        fileSystem->FreeFile( vertex ); fileSystem->FreeFile( fragment );
    }
    // Reuse precedes the capacity check: an unchanged reload must still work
    // when all version slots are occupied. Failed versions are reusable too.
    for ( size_t i = 0; i < programs.size(); ++i ) {
        Program &old = *programs[i];
        if ( old.key == key && old.sourcePairFound == sourcePairFound
            && old.vertexSource == request.vertexSource && old.fragmentSource == request.fragmentSource ) {
            currentPrograms[key] = static_cast<int>( i );
            if ( !old.valid ) { common->Warning( "Vulkan authored GLSL '%s': %s", old.name.c_str(), old.compiled.diagnostic.c_str() ); }
            return &old;
        }
    }
    if ( CacheFull() ) { return NULL; }
    auto program = std::make_unique<Program>();
    program->name = stage->glslProgramName;
    program->key = key;
    program->identity = static_cast<int>( programs.size() );
    program->sourcePairFound = sourcePairFound;
    program->vertexSource = request.vertexSource;
    program->fragmentSource = request.fragmentSource;
    if ( sourcePairFound ) {
        for ( int i = 0; i < stage->numShaderParms; ++i ) {
            request.parameters.push_back( {stage->shaderParmNames[i], i, ParameterComponents( stage, i )} );
        }
        for ( int i = 0; i < stage->numShaderTextures; ++i ) { request.textures.push_back( {stage->shaderTextureNames[i], i} ); }
        if ( compilerBytes + request.vertexSource.size() + request.fragmentSource.size() < MaxCompilerBytes ) {
            program->valid = oq4material::CompileGLSL( request, program->compiled );
        } else { program->compiled.diagnostic = "material compiler storage exhausted"; }
    } else { program->compiled.diagnostic = "could not find a complete vertex/fragment source pair"; }
    const size_t bytes = program->vertexSource.size() + program->fragmentSource.size() + program->compiled.diagnostic.size()
        + program->compiled.vertexSource.size() + program->compiled.fragmentSource.size()
        + (program->compiled.vertex.size() + program->compiled.fragment.size()) * sizeof( uint32_t );
    if ( bytes > MaxCompilerBytes - Min( compilerBytes, MaxCompilerBytes ) ) {
        program->compiled = {};
        // clear() retains string capacity and would defeat the storage bound.
        std::string().swap( program->vertexSource );
        std::string().swap( program->fragmentSource );
        program->compiled.diagnostic = "material compiler storage exhausted";
        program->valid = false;
    } else { compilerBytes += bytes; }
    if ( !program->valid ) {
        common->Warning( "Vulkan authored GLSL '%s': %s", program->name.c_str(), program->compiled.diagnostic.c_str() );
    } else {
        common->Printf( "Vulkan: compiled authored GLSL '%s' (%s, %s)\n", program->name.c_str(), vertexPath.c_str(), fragmentPath.c_str() );
    }
    currentPrograms[key] = program->identity;
    programs.push_back( std::move( program ) );
    return programs.back().get();
}

bool CreateLayouts() {
    if ( pipelineLayout != VK_NULL_HANDLE ) { return true; }
    VkDescriptorSetLayoutBinding binding = {};
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1; info.pBindings = &binding;
    if ( uniformLayout == VK_NULL_HANDLE && vkCreateDescriptorSetLayout( vkCtx.device, &info, NULL, &uniformLayout ) != VK_SUCCESS ) { return false; }
    VkDescriptorSetLayoutBinding textures[oq4material::MaxTextures] = {};
    for ( int i = 0; i < oq4material::MaxTextures; ++i ) {
        textures[i].binding = i; textures[i].descriptorCount = 1;
        textures[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textures[i].stageFlags = binding.stageFlags;
    }
    info.bindingCount = oq4material::MaxTextures; info.pBindings = textures;
    if ( textureLayout == VK_NULL_HANDLE && vkCreateDescriptorSetLayout( vkCtx.device, &info, NULL, &textureLayout ) != VK_SUCCESS ) { return false; }
    const VkDescriptorSetLayout layouts[] = {uniformLayout, textureLayout};
    VkPipelineLayoutCreateInfo pipeline = {};
    pipeline.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline.setLayoutCount = 2; pipeline.pSetLayouts = layouts;
    return vkCreatePipelineLayout( vkCtx.device, &pipeline, NULL, &pipelineLayout ) == VK_SUCCESS;
}

bool CreateModule( const std::vector<uint32_t> &code, VkShaderModule &module ) {
    if ( module != VK_NULL_HANDLE ) { return true; }
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof( uint32_t ); info.pCode = code.data();
    return vkCreateShaderModule( vkCtx.device, &info, NULL, &module ) == VK_SUCCESS;
}
} // namespace

vkGLSLProgramFamily_t VK_MaterialPrograms_NativeFamily( const char *program ) {
    if ( program == NULL ) { return VK_GLSL_PROGRAM_FAMILY_UNKNOWN; }
    const auto cached = nativeFamilies.find( program );
    if ( cached != nativeFamilies.end() ) { return cached->second; }
    idStr canonical = program;
    canonical.BackSlashesToSlashes(); canonical.ToLower();
    if ( canonical.Cmpn( "glprogs/", 8 ) == 0 ) { canonical = canonical.Mid( 8, canonical.Length() - 8 ); }
    vkGLSLProgramFamily_t candidate = VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
    for ( const auto &signature : vkNativeMaterialSources ) {
        if ( canonical == signature.program ) { candidate = signature.family; break; }
    }
    if ( candidate == VK_GLSL_PROGRAM_FAMILY_UNKNOWN ) { return candidate; }
    if ( nativeFamilies.size() >= MaxPrograms ) {
        if ( !warnedCapacity ) { common->Warning( "Vulkan: native material source cache exhausted" ); warnedCapacity = true; }
        return VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
    }
    char *vertex = NULL, *fragment = NULL;
    int vertexBytes = 0, fragmentBytes = 0;
    bool anySource = false;
    idStr vertexPath, fragmentPath;
    const bool pair = R_FindGLSLSourcePair( program, vertexPath, fragmentPath,
        &vertex, &fragment, &vertexBytes, &fragmentBytes, &anySource );
    vkGLSLProgramFamily_t selected = VK_GLSL_PROGRAM_FAMILY_UNKNOWN;
    if ( pair ) {
        const std::string vertexHash = SourceDigest( vertex, vertexBytes );
        const std::string fragmentHash = SourceDigest( fragment, fragmentBytes );
        fileSystem->FreeFile( vertex ); fileSystem->FreeFile( fragment );
        for ( const auto &signature : vkNativeMaterialSources ) {
            if ( canonical == signature.program && !vertexHash.empty() && !fragmentHash.empty()
                && vertexHash == signature.vertexSHA256 && fragmentHash == signature.fragmentSHA256 ) {
                selected = signature.family; break;
            }
        }
    } else if ( !anySource ) {
        // Some retail guide families ship without GLSL files. Keep their
        // built-in compatibility implementation only for the canonical name
        // with no authored source at all; a partial override must fail openly.
        selected = candidate;
    }
    nativeFamilies.emplace( program, selected );
    common->Printf( "Vulkan: GLSL source selection '%s': %s\n", program,
        selected == VK_GLSL_PROGRAM_FAMILY_UNKNOWN ? "authored source" : pair ? "verified native source" : "built-in default (no source)" );
    return selected;
}

bool VK_MaterialPrograms_Validate( const newShaderStage_t *stage ) {
    Program *program = Find( stage );
    return program != NULL && program->valid;
}

bool VK_MaterialPrograms_NeedsStencil( const idMaterial *material, const float *registers ) {
    if ( material == NULL || !material->HasActiveCustomGLSLLighting( registers ) ) { return false; }
    for ( int i = 0; i < material->GetNumStages(); ++i ) {
        const shaderStage_t *stage = material->GetStage( i );
        const newShaderStage_t *program = stage->newStage;
        if ( program == NULL || !program->glslProgram || !program->customLighting
            || ( registers != NULL && registers[stage->conditionRegister] == 0.0f ) ) { continue; }
        const vkGLSLProgramFamily_t family = VK_MaterialPrograms_NativeFamily( program->glslProgramName );
        if ( family != VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT && family != VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP ) { return true; }
    }
    return false;
}

void VK_MaterialPrograms_Reload() {
    currentPrograms.clear();
    nativeFamilies.clear();
    for ( auto &program : programs ) { program->warnedResource = false; }
    common->Printf( "Vulkan: authored GLSL sources will be reloaded on use\n" );
}

void VK_MaterialPrograms_Report() {
    common->Printf( "Vulkan authored GLSL programs: %d versions, %zu compiler bytes\n", static_cast<int>( programs.size() ), compilerBytes );
    for ( const auto &program : programs ) {
        common->Printf( "  %s %s%s\n", program->valid ? "compiled" : "invalid", program->name.c_str(), program->loggedDraw ? " (drawn)" : "" );
    }
}

void VK_MaterialPrograms_BeginFrame( int slot ) {
    drawCounts[slot] = 0;
    poolReady[slot] = pools[slot] == VK_NULL_HANDLE || vkResetDescriptorPool( vkCtx.device, pools[slot], 0 ) == VK_SUCCESS;
}

bool VK_MaterialPrograms_Bind( const newShaderStage_t *stage,
        const oq4material::UniformBlock &uniforms, int stateBits, bool separateColor,
        const drawInteraction_t *interaction ) {
    Program *program = Find( stage );
    if ( program == NULL || !program->valid || !VK_Exec_MainRenderingScopeOpen() ) { return false; }
    if ( !CreateLayouts() || !CreateModule( program->compiled.vertex, program->vertex )
        || !CreateModule( program->compiled.fragment, program->fragment ) ) { return Fail( *program, "shader resource allocation failed" ); }
    const int slot = VK_Exec_ActiveFrameSlot();
    if ( !poolReady[slot] || drawCounts[slot] >= MaxDraws ) { return Fail( *program, "frame descriptor capacity exhausted" ); }
    if ( pools[slot] == VK_NULL_HANDLE ) {
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxDraws},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxDraws * oq4material::MaxTextures}};
        VkDescriptorPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = MaxDraws * 2; info.poolSizeCount = 2; info.pPoolSizes = sizes;
        if ( vkCreateDescriptorPool( vkCtx.device, &info, NULL, &pools[slot] ) != VK_SUCCESS ) { return Fail( *program, "frame descriptor pool allocation failed" ); }
    }
    VkDescriptorImageInfo images[oq4material::MaxTextures] = {};
    for ( int i = 0; i < oq4material::MaxTextures; ++i ) {
        const bool used = (program->compiled.textureMask & (1u << i)) != 0;
        idImage *image = used ? RB_ResolveGLSLShaderTextureImage( stage, i, interaction ) : globalImages->whiteImage;
        if ( image == NULL ) { return Fail( *program, "missing shader texture" ); }
        if ( used ) { image->SetSamplerState( stage->shaderTextureFilters[i], stage->shaderTextureRepeats[i] ); }
        const bool cube = (program->compiled.cubeTextureMask & (1u << i)) != 0;
        if ( VK_Exec_ImageDescriptor( image->GetDeviceHandle(), !cube ) == VK_NULL_HANDLE ) { return Fail( *program, "shader texture is not resident" ); }
        const vkImageEntry_t *entry = VK_Image_GetEntry( image->GetDeviceHandle() );
        if ( entry == NULL || entry->isCube != cube || entry->samples != VK_SAMPLE_COUNT_1_BIT ) { return Fail( *program, "shader texture type/sample mismatch" ); }
        if ( used && entry->materialSampleFlipY ) { return Fail( *program, "flipped render-target texture sampling is not yet supported" ); }
        images[i].sampler = entry->sampler; images[i].imageView = entry->view; images[i].imageLayout = entry->layout;
    }
    VkPipeline pipeline = VK_Exec_AuthoredMaterialPipeline( program->identity,
        program->vertex, program->fragment, pipelineLayout, stateBits, separateColor, program->compiled.vertexInputMask );
    if ( pipeline == VK_NULL_HANDLE ) { return Fail( *program, "material pipeline allocation failed" ); }
    VkDescriptorBufferInfo buffer = {};
    if ( !VK_Exec_AuthoredUniformAlloc( &uniforms, sizeof( uniforms ), buffer ) ) { return Fail( *program, "material uniform ring exhausted" ); }
    const VkDescriptorSetLayout layouts[] = {uniformLayout, textureLayout};
    VkDescriptorSet sets[2] = {};
    VkDescriptorSetAllocateInfo allocate = {};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pools[slot]; allocate.descriptorSetCount = 2; allocate.pSetLayouts = layouts;
    if ( vkAllocateDescriptorSets( vkCtx.device, &allocate, sets ) != VK_SUCCESS ) { return Fail( *program, "material descriptor allocation failed" ); }
    ++drawCounts[slot];
    VkWriteDescriptorSet writes[1 + oq4material::MaxTextures] = {};
    for ( int i = 0; i <= oq4material::MaxTextures; ++i ) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = sets[i == 0 ? 0 : 1]; writes[i].dstBinding = i == 0 ? 0 : i - 1;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if ( i == 0 ) { writes[i].pBufferInfo = &buffer; } else { writes[i].pImageInfo = &images[i - 1]; }
    }
    vkUpdateDescriptorSets( vkCtx.device, 1 + oq4material::MaxTextures, writes, 0, NULL );
    const VkCommandBuffer cmd = VK_Exec_ActiveCmd();
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0, NULL );
    if ( !program->loggedDraw ) {
        common->Printf( "Vulkan: drawing authored GLSL '%s'\n", program->name.c_str() );
        program->loggedDraw = true;
    }
    if ( interaction != NULL && !program->loggedLightingDraw ) {
        common->Printf( "Vulkan: drawing authored GLSL lighting '%s'\n", program->name.c_str() );
        program->loggedLightingDraw = true;
    }
    return true;
}

void VK_MaterialPrograms_Shutdown() {
    for ( auto &program : programs ) {
        if ( program->vertex != VK_NULL_HANDLE ) { vkDestroyShaderModule( vkCtx.device, program->vertex, NULL ); }
        if ( program->fragment != VK_NULL_HANDLE ) { vkDestroyShaderModule( vkCtx.device, program->fragment, NULL ); }
    }
    for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; ++i ) {
        if ( pools[i] != VK_NULL_HANDLE ) { vkDestroyDescriptorPool( vkCtx.device, pools[i], NULL ); }
        pools[i] = VK_NULL_HANDLE; drawCounts[i] = 0; poolReady[i] = false;
    }
    if ( pipelineLayout != VK_NULL_HANDLE ) { vkDestroyPipelineLayout( vkCtx.device, pipelineLayout, NULL ); }
    if ( uniformLayout != VK_NULL_HANDLE ) { vkDestroyDescriptorSetLayout( vkCtx.device, uniformLayout, NULL ); }
    if ( textureLayout != VK_NULL_HANDLE ) { vkDestroyDescriptorSetLayout( vkCtx.device, textureLayout, NULL ); }
    pipelineLayout = VK_NULL_HANDLE; uniformLayout = textureLayout = VK_NULL_HANDLE;
    programs.clear(); currentPrograms.clear(); nativeFamilies.clear(); compilerBytes = 0; warnedCapacity = false;
}
#endif

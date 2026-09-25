#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../PBRMath.h"
#include "point_shadow_math.glsl"

// openQ4 Vulkan point-shadow-receiving interaction â€” fragment stage (Phase
// F2b).
//
// interaction.frag plus the point-light cube shadow sample of the GL
// shadow_point_interaction.fs contract. Set 7 exposes both LEQUAL comparison
// and unfiltered raw-depth cube samplers, selected by
// r_shadowMapPointDepthCompare at runtime. The stock fixed/stable-rotated
// tangent-disc kernel uses exact 1/5/9/13 Poisson tap tiers. The compare value
// is the normalized radial distance from the light â€” exactly what the
// caster's fragment stage wrote into native depth. Out-of-envelope receivers
// stay lit (factor 1.0). Vulkan cube sampling is always seamless (parity with
// the GL path's GL_TEXTURE_CUBE_MAP_SEAMLESS enable).

layout(set = 0, binding = 0) uniform sampler2D specularTableMap;
layout(set = 1, binding = 0) uniform sampler2D bumpMap;
layout(set = 2, binding = 0) uniform sampler2D lightFalloffMap;
layout(set = 3, binding = 0) uniform sampler2D lightProjectionMap;
layout(set = 4, binding = 0) uniform sampler2D diffuseMap;
layout(set = 5, binding = 0) uniform sampler2D specularMap;
layout(set = 7, binding = 0) uniform samplerCubeShadow shadowCompareMap;
layout(set = 7, binding = 2) uniform samplerCube shadowRawMap;

layout(push_constant) uniform InteractionPushConstants {
    mat4 mvp;
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 d;
} pc;

layout(set = 6, binding = 0, std140) uniform InteractionBlock {
    vec4 localLightOrigin;
    vec4 localViewOrigin;
    vec4 lightProjectionS;
    vec4 lightProjectionT;
    vec4 lightProjectionQ;
    vec4 lightFalloffS;
    vec4 bumpMatrixS;
    vec4 bumpMatrixT;
    vec4 diffuseMatrixS;
    vec4 diffuseMatrixT;
    vec4 specularMatrixS;
    vec4 specularMatrixT;
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 flatDiffuseParams;
    // cel banding (RB_SetCelInteractionUniform): x enabled, y band count,
    // z hard specular, w softness; zero when the surface is not banded
    vec4 celParams;
} inter;

layout(set = 7, binding = 1, std140) uniform ShadowBlock {
    vec4 modelRow0;      // model -> world matrix rows
    vec4 modelRow1;
    vec4 modelRow2;
    vec4 lightOriginFar; // xyz: world-space light origin, w: far envelope
    vec4 biasParams;     // x: constant bias, y: normal bias, z: texel depth bias, w: per-distance normal-offset factor
    vec4 filterParams;   // x: radius, y: taps, z: mode, w: cube texel scale
    vec4 samplingParams; // x: hardware compare enabled
    vec4 debugParams;    // x: r_shadowMapDebugMode, y: receiver fallback reason
} shadow;

layout(location = 0) in vec2 vBumpTexCoord;
layout(location = 1) in vec2 vDiffuseTexCoord;
layout(location = 2) in vec2 vSpecularTexCoord;
layout(location = 3) in vec4 vLightFalloffTexCoord;
layout(location = 4) in vec4 vLightProjectionTexCoord;
layout(location = 5) in vec3 vLightVector;
layout(location = 6) in vec3 vHalfAngleVector;
layout(location = 7) in vec3 vVertexColor;
layout(location = 8) in vec3 vPointShadowVector;
layout(location = 9) in float vShadowLightCos;
layout(location = 10) in vec3 vViewVector;

// Pack the basis into unused components so the projected receiver remains
// within Vulkan's minimum 16 varying locations (0..15).
layout(location = 12, component = 1) in vec3 vPBRTangent0;
layout(location = 14, component = 1) in vec3 vPBRTangent1;
layout(location = 15) in vec3 vPBRNormal;

layout(location = 0) out vec4 outColor;

// The point-cube subset of shadowMapDebugMode_t (tr_local.h), matching
// glprogs/shadow_point_interaction.fs: a cube has no atlas rect, cascade
// ladder, or projected w to visualize.
const float kShadowDebugProjectedDepth = 4.0;
const float kShadowDebugBiasOff = 8.0;
const float kShadowDebugPCFOff = 9.0;
const float kShadowDebugReceiverPlaneBiasOff = 11.0;
const float kShadowDebugCompareDelta = 12.0;
const float kShadowDebugReceiverEligibility = 13.0;
const float kShadowDebugReceiverFallbackReason = 14.0;

bool ShadowDebugModeIs(float mode) {
    return abs(shadow.debugParams.x - mode) < 0.5;
}

bool ShadowReceiverDebugMode() {
    return ShadowDebugModeIs(kShadowDebugReceiverEligibility)
        || ShadowDebugModeIs(kShadowDebugReceiverFallbackReason);
}

bool ShadowVisualDebugMode() {
    return ShadowDebugModeIs(1.0)
        || ShadowDebugModeIs(kShadowDebugProjectedDepth)
        || ShadowDebugModeIs(kShadowDebugCompareDelta)
        || ShadowReceiverDebugMode();
}

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

vec3 ApplyFlatDiffuseSweep(vec3 diffuse, float localZ) {
    if (inter.flatDiffuseParams.x <= 0.0) {
        return diffuse;
    }
    float height = clamp((localZ - inter.flatDiffuseParams.y)
        * inter.flatDiffuseParams.z, 0.0, 1.0);
    float distanceToBand = abs(height - fract(inter.flatDiffuseParams.w));
    distanceToBand = min(distanceToBand, 1.0 - distanceToBand);
    float band = 1.0 - smoothstep(0.045, 0.16, distanceToBand);
    return mix(diffuse, vec3(1.0), inter.flatDiffuseParams.x * band);
}

#include "pbr_direct.glsl"

float StableShadowHash(vec3 value) {
    return fract(sin(dot(value, vec3(12.9898, 78.233, 37.719)))
        * 43758.5453);
}

mat2 ShadowOffsetRotation(vec3 direction) {
    if (shadow.filterParams.z < 0.5) {
        return mat2(1.0, 0.0, 0.0, 1.0);
    }
    float angle = StableShadowHash(floor(direction * 37.0)) * 6.2831853;
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, s, -s, c);
}

float ShadowReceiverBias() {
    if (ShadowDebugModeIs(kShadowDebugBiasOff)) {
        return 0.0;
    }
    float lightCos = clamp(vShadowLightCos, 0.20, 1.0);
    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));
    float slopeBias = min(sinTheta / lightCos, 4.0);
    float normalBias = ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)
        ? 0.0 : shadow.biasParams.y;
    float scalarBias = shadow.biasParams.x + normalBias * sinTheta;
    float texelBias = shadow.biasParams.z * (1.0 + slopeBias);
    return max(max(scalarBias, 0.0), max(texelBias, 0.0));
}

float SamplePointShadowCompare(vec3 direction, float depth) {
    float compareDepth = depth - ShadowReceiverBias();
    if (shadow.samplingParams.x > 0.5) {
        return texture(shadowCompareMap, vec4(direction, compareDepth));
    }
    float storedDepth = texture(shadowRawMap, direction).r;
    return compareDepth <= storedDepth ? 1.0 : 0.0;
}

float RawPointShadowDepth(vec3 direction) {
    return texture(shadowRawMap, direction).r;
}

float SampleShadowFactor() {
    float far = shadow.lightOriginFar.w;
    // !(far > 0) also rejects NaN
    if (!(far > 0.0)) {
        return 1.0;
    }

    float depth = PointShadowRadialDepth(vPointShadowVector, far);
    if (depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }
    vec3 direction = SafeNormalize(vPointShadowVector);

    float filterRadius = ShadowDebugModeIs(kShadowDebugPCFOff)
        ? 0.0 : shadow.filterParams.x;
    if (filterRadius <= 0.0 || shadow.filterParams.w <= 0.0) {
        return SamplePointShadowCompare(direction, depth);
    }

    vec3 up = abs(direction.z) < 0.99
        ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = SafeNormalize(cross(up, direction));
    vec3 bitangent = cross(direction, tangent);
    float tap = shadow.filterParams.w * filterRadius;

    float result = SamplePointShadowCompare(direction, depth);
    if (shadow.filterParams.y <= 1.0) {
        return result;
    }
    mat2 rotation = ShadowOffsetRotation(direction);
    vec2 o1 = rotation * vec2(-0.326212, -0.405805);
    vec2 o2 = rotation * vec2(-0.840144, -0.073580);
    vec2 o3 = rotation * vec2(-0.695914, 0.457137);
    vec2 o4 = rotation * vec2(-0.203345, 0.620716);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o1.x + bitangent * o1.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o2.x + bitangent * o2.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o3.x + bitangent * o3.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o4.x + bitangent * o4.y) * tap), depth);
    if (shadow.filterParams.y <= 5.0) {
        return result * (1.0 / 5.0);
    }
    vec2 o5 = rotation * vec2(0.962340, -0.194983);
    vec2 o6 = rotation * vec2(0.473434, -0.480026);
    vec2 o7 = rotation * vec2(0.519456, 0.767022);
    vec2 o8 = rotation * vec2(0.185461, -0.893124);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o5.x + bitangent * o5.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o6.x + bitangent * o6.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o7.x + bitangent * o7.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o8.x + bitangent * o8.y) * tap), depth);
    if (shadow.filterParams.y <= 9.0) {
        return result * (1.0 / 9.0);
    }
    vec2 o9 = rotation * vec2(0.507431, 0.064425);
    vec2 o10 = rotation * vec2(0.896420, 0.412458);
    vec2 o11 = rotation * vec2(-0.321940, -0.932615);
    vec2 o12 = rotation * vec2(-0.791559, -0.597705);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o9.x + bitangent * o9.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o10.x + bitangent * o10.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o11.x + bitangent * o11.y) * tap), depth);
    result += SamplePointShadowCompare(SafeNormalize(direction
        + (tangent * o12.x + bitangent * o12.y) * tap), depth);
    return result * (1.0 / 13.0);
}

// Vulkan admits shadow maps per light rather than per receiver surface, so a
// receiver that reached this shader is always eligible and the reason the CPU
// uploads is 0. The ladder is kept whole so the two backends read alike if a
// per-surface reason ever appears.
vec4 PointReceiverDebugOutput() {
    float reason = floor(shadow.debugParams.y + 0.5);
    if (ShadowDebugModeIs(kShadowDebugReceiverEligibility)) {
        if (reason < 0.5) {
            return vec4(0.0, 0.95, 0.18, 1.0);
        }
        if (reason < 1.5) {
            return vec4(0.0, 0.85, 1.0, 1.0);
        }
        return vec4(1.0, 0.18, 0.08, 1.0);
    }

    if (reason < 0.5) {
        return vec4(0.0, 0.85, 0.16, 1.0);
    }
    if (reason < 1.5) {
        return vec4(0.0, 0.82, 1.0, 1.0);
    }
    if (reason < 2.5) {
        return vec4(1.0, 0.08, 0.08, 1.0);
    }
    if (reason < 3.5) {
        return vec4(0.95, 0.12, 1.0, 1.0);
    }
    if (reason < 4.5) {
        return vec4(1.0, 0.86, 0.08, 1.0);
    }
    return vec4(1.0, 0.45, 0.0, 1.0);
}

vec4 PointShadowDebugOutput() {
    if (ShadowReceiverDebugMode()) {
        return PointReceiverDebugOutput();
    }
    float far = shadow.lightOriginFar.w;
    if (!(far > 0.0)) {
        return vec4(1.0, 0.0, 1.0, 1.0);
    }

    float depth = PointShadowRadialDepth(vPointShadowVector, far);
    if (depth <= 0.0 || depth >= 1.0) {
        return vec4(1.0, 1.0, 0.0, 1.0);
    }
    vec3 direction = SafeNormalize(vPointShadowVector);
    float storedDepth = RawPointShadowDepth(direction);

    if (ShadowDebugModeIs(kShadowDebugCompareDelta)) {
        float delta = depth - ShadowReceiverBias() - storedDepth;
        float magnitude = clamp(abs(delta) * 64.0, 0.0, 1.0);
        vec3 litColor = vec3(0.1, 0.35, 1.0);
        vec3 shadowColor = vec3(1.0, 0.16, 0.08);
        vec3 nearColor = vec3(0.0, 1.0, 0.22);
        vec3 signColor = (delta > 0.0) ? shadowColor : litColor;
        return vec4(mix(nearColor, signColor, magnitude), 1.0);
    }

    if (ShadowDebugModeIs(kShadowDebugProjectedDepth)) {
        return vec4(vec3(depth), 1.0);
    }

    // mode 1: the cube lookup direction with its stored radial depth
    return vec4(direction * 0.5 + 0.5, clamp(storedDepth, 0.0, 1.0));
}

// ---------------------------------------------------------------------------
// Cel banding, from glprogs/material_interaction.fs and the shadow interaction
// programs. inter.celParams is ( bandsEnabled, bandCount, hardSpecular,
// softness ); R_CelQuantizeUnitValue is the CPU copy of the ladder.
// ---------------------------------------------------------------------------

float CelSteps() {
    return max(inter.celParams.y - 1.0, 1.0);
}

// Places a 0..1 value on the band ladder; softness widens every boundary into
// a smoothstep centred where the hard step would land.
float CelLadder(float value) {
    float steps = CelSteps();
    float scaled = value * steps;

    float softness = clamp(inter.celParams.w, 0.0, 1.0);
    if (softness <= 0.0) {
        return floor(scaled + 0.5) / steps;
    }

    float lower = floor(scaled);
    float halfWidth = softness * 0.5;
    float blend = smoothstep(0.5 - halfWidth, 0.5 + halfWidth, scaled - lower);

    return (lower + blend) / steps;
}

// Quantizes a light contribution without shifting its hue: the brightest
// channel picks the band. Black and overbright pass through untouched.
vec3 CelQuantizeLight(vec3 light) {
    if (inter.celParams.x <= 0.5) {
        return light;
    }

    float peak = max(max(light.r, light.g), light.b);
    if (peak <= 0.0 || peak >= 1.0) {
        return light;
    }

    return light * (CelLadder(peak) / peak);
}

// Collapses the specular falloff into flat plateaus on the same ladder.
float CelSpecularTerm(float term) {
    if (inter.celParams.x <= 0.5 || inter.celParams.z <= 0.5) {
        return term;
    }

    return CelLadder(clamp(term, 0.0, 1.0));
}

#ifdef VK_HDR_DOMAIN_MRT
layout(location = 1) out vec4 outPBRColor;
#define main HDRMaterialMain
#endif

void main() {
    if (pc.d.x > 2.5) {
        // Emission is drawn once per surface in the ambient walk. An ordered
        // transparent draw still owes the frame its coverage, so it composites
        // black through the authored alpha instead of contributing nothing.
        outColor = vec4(0.0, 0.0, 0.0, PBRTransparentAlpha(vDiffuseTexCoord));
        return;
    }
    if (pc.d.x > 1.5) {
        outColor = vec4(0.0, 1.0, 0.0, PBRTransparentAlpha(vDiffuseTexCoord));
        return;
    }

    vec2 bumpTexCoord = vBumpTexCoord;
    vec2 diffuseTexCoord = vDiffuseTexCoord;
    vec2 specularTexCoord = vSpecularTexCoord;
    if (pc.c.z > 0.5) {
        float height = texture(bumpMap, bumpTexCoord).r;
        vec2 offset = SafeNormalize(vViewVector).xy * (height * pc.c.x + pc.c.y);
        bumpTexCoord += offset;
        diffuseTexCoord += offset;
        specularTexCoord += offset;
    }

    vec4 bumpSample = texture(bumpMap, bumpTexCoord);
    if (pc.d.x > 0.5) {
        vec3 localNormal = PBRDirectNormal(bumpTexCoord);
        vec3 packed = EvaluatePBRDirect(localNormal,
            diffuseTexCoord, specularTexCoord, SampleShadowFactor());
        if (ShadowVisualDebugMode()) {
            outColor = PointShadowDebugOutput();
            return;
        }
        outColor = vec4(packed, PBRTransparentAlpha(diffuseTexCoord));
        return;
    }
    vec3 localNormal = vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0;

    vec3 lightDir = (pc.a.z > 0.5) ? pc.b.xyz : SafeNormalize(vLightVector);
    float ndotl = max(dot(lightDir, localNormal), 0.0);

    vec3 light = vec3(ndotl);
    light *= textureProj(lightFalloffMap, vLightFalloffTexCoord).rgb;
    light *= textureProj(lightProjectionMap, vLightProjectionTexCoord).rgb;
    light *= SampleShadowFactor();
    if (ShadowVisualDebugMode()) {
        outColor = PointShadowDebugOutput();
        return;
    }

    vec3 diffuse = texture(diffuseMap, diffuseTexCoord).rgb * inter.diffuseColor.rgb;
    diffuse = ApplyFlatDiffuseSweep(diffuse, vLightFalloffTexCoord.z);

    vec3 halfAngle = SafeNormalize(vHalfAngleVector);
    float specularDot = clamp(dot(halfAngle, localNormal), 0.0, 1.0);
    float specularTerm = texture(specularTableMap, vec2(specularDot, 0.5)).r * 2.0;
    specularTerm = CelSpecularTerm(specularTerm * 0.5) * 2.0;
    vec3 specular = texture(specularMap, specularTexCoord).rgb * inter.specularColor.rgb * specularTerm;

    light = CelQuantizeLight(light);
    outColor = vec4((diffuse + specular) * light * vVertexColor, 0.0);
}

#ifdef VK_HDR_DOMAIN_MRT
#undef main
void main() {
    HDRMaterialMain();
    // Both attachments use the same blend state. Preserve source alpha in
    // both outputs for coverage/blending; the primary stores scene alpha.
    bool nativePBR = pc.d.x > 0.5;
    outPBRColor = vec4(nativePBR ? outColor.rgb : vec3(0.0), outColor.a);
    if (nativePBR) outColor.rgb = vec3(0.0);
}
#endif

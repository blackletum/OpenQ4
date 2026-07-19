#version 450

// openQ4 Vulkan shadow-receiving interaction — fragment stage (Phase F2a).
//
// interaction.frag plus the projected shadow sample of the GL
// shadow_interaction.fs contract, reduced to the scratch-first surface:
// single cascade, hardware LEQUAL compare through sampler2DShadow with
// LINEAR filtering (2×2 hardware PCF — the r_shadowMapDepthCompare default),
// simple receiver bias (constant + normal-sloped + texel-aware; no PCSS, no
// receiver-plane derivative bias, no multi-tap software PCF). The shadow
// coordinate projects like the GL shader: xy/w for the tile-local UV, z
// UNdivided as the compare depth (the caster wrote dot(position, depthPlane)
// straight into the map). Out-of-frustum receivers stay lit (factor 1.0).

layout(set = 0, binding = 0) uniform sampler2D specularTableMap;
layout(set = 1, binding = 0) uniform sampler2D bumpMap;
layout(set = 2, binding = 0) uniform sampler2D lightFalloffMap;
layout(set = 3, binding = 0) uniform sampler2D lightProjectionMap;
layout(set = 4, binding = 0) uniform sampler2D diffuseMap;
layout(set = 5, binding = 0) uniform sampler2D specularMap;
layout(set = 7, binding = 0) uniform sampler2DShadow shadowMap;

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
} inter;

layout(set = 7, binding = 1, std140) uniform ShadowBlock {
    vec4 shadowRow0;
    vec4 shadowRow1;
    vec4 shadowRow2;
    vec4 shadowRow3;
    vec4 atlasRect;    // composed atlas UV rect (u0, v0, u1, v1); v span inverted for the Vulkan row order
    vec4 biasParams;   // x: constant bias, y: normal bias, z: texel depth bias, w: normal-offset world units
    vec4 texelSize;    // x,y: 1 / atlas dimensions
} shadow;

layout(location = 0) in vec2 vBumpTexCoord;
layout(location = 1) in vec2 vDiffuseTexCoord;
layout(location = 2) in vec2 vSpecularTexCoord;
layout(location = 3) in vec4 vLightFalloffTexCoord;
layout(location = 4) in vec4 vLightProjectionTexCoord;
layout(location = 5) in vec3 vLightVector;
layout(location = 6) in vec3 vHalfAngleVector;
layout(location = 7) in vec3 vVertexColor;
layout(location = 8) in vec4 vShadowCoord;
layout(location = 9) in float vShadowLightCos;

layout(location = 0) out vec4 outColor;

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

float SampleShadowFactor() {
    float w = vShadowCoord.w;
    // !(w > eps) also rejects NaN
    if (!(w > 1.0e-5) || w > 65536.0) {
        return 1.0;
    }

    vec2 projectedXY = vShadowCoord.xy / w;
    float depth = vShadowCoord.z;
    vec2 localUv = projectedXY * 0.5 + 0.5;
    if (localUv.x <= 0.0 || localUv.x >= 1.0 || localUv.y <= 0.0 || localUv.y >= 1.0) {
        return 1.0;
    }
    if (depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }

    vec2 uv = mix(shadow.atlasRect.xy, shadow.atlasRect.zw, localUv);
    // the composed rect's v span is inverted (Vulkan y-down atlas rows), so
    // derive the clamp window from the component-wise extremes
    vec2 rectMin = min(shadow.atlasRect.xy, shadow.atlasRect.zw);
    vec2 rectMax = max(shadow.atlasRect.xy, shadow.atlasRect.zw);
    vec2 guardBand = shadow.texelSize.xy * 1.5;
    uv = clamp(uv, rectMin + guardBand, rectMax - guardBand);

    float lightCos = clamp(vShadowLightCos, 0.20, 1.0);
    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));
    float slopeBias = min(sinTheta / lightCos, 4.0);
    float scalarBias = shadow.biasParams.x + shadow.biasParams.y * sinTheta;
    float texelBias = shadow.biasParams.z * (1.0 + slopeBias);
    float bias = max(max(scalarBias, 0.0), max(texelBias, 0.0));

    return texture(shadowMap, vec3(uv, depth - bias));
}

void main() {
    vec4 bumpSample = texture(bumpMap, vBumpTexCoord);
    vec3 localNormal = vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0;

    vec3 lightDir = (pc.a.z > 0.5) ? pc.b.xyz : SafeNormalize(vLightVector);
    float ndotl = max(dot(lightDir, localNormal), 0.0);

    vec3 light = vec3(ndotl);
    light *= textureProj(lightFalloffMap, vLightFalloffTexCoord).rgb;
    light *= textureProj(lightProjectionMap, vLightProjectionTexCoord).rgb;
    light *= SampleShadowFactor();

    vec3 diffuse = texture(diffuseMap, vDiffuseTexCoord).rgb * inter.diffuseColor.rgb;

    vec3 halfAngle = SafeNormalize(vHalfAngleVector);
    float specularDot = clamp(dot(halfAngle, localNormal), 0.0, 1.0);
    float specularTerm = texture(specularTableMap, vec2(specularDot, 0.5)).r * 2.0;
    vec3 specular = texture(specularMap, vSpecularTexCoord).rgb * inter.specularColor.rgb * specularTerm;

    outColor = vec4((diffuse + specular) * light * vVertexColor, 0.0);
}

#version 450

// openQ4 Vulkan shadow-receiving interaction — vertex stage (Phase F2a,
// docs/dev/plans/2026-07-19-vulkan-phase-f.md).
//
// interaction.vert plus the projected shadow coordinate of the GL
// shadow_interaction.vs contract (single cascade, scratch-first): the four
// shadow rows are the light's world clip planes localized to the surface's
// model space CPU-side per space (draw_arb2.cpp:8552-8581), so the shadow
// coordinate is four dot products against the raw model-space position.
// Normal-offset shadows push the sampled point along the geometric normal by
// (world texel size × sinθ) before projecting, fixing self-shadow acne
// structurally where pure depth bias would detach contact shadows.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent0;
layout(location = 4) in vec3 inTangent1;
layout(location = 5) in vec2 inTexCoord;

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

layout(location = 0) out vec2 vBumpTexCoord;
layout(location = 1) out vec2 vDiffuseTexCoord;
layout(location = 2) out vec2 vSpecularTexCoord;
layout(location = 3) out vec4 vLightFalloffTexCoord;
layout(location = 4) out vec4 vLightProjectionTexCoord;
layout(location = 5) out vec3 vLightVector;
layout(location = 6) out vec3 vHalfAngleVector;
layout(location = 7) out vec3 vVertexColor;
layout(location = 8) out vec4 vShadowCoord;
layout(location = 9) out float vShadowLightCos;

vec3 TangentSpaceVector(vec3 objectVector) {
    return vec3(
        dot(inTangent0, objectVector),
        dot(inTangent1, objectVector),
        dot(inNormal, objectVector));
}

void main() {
    vec4 position = vec4(inPosition, 1.0);
    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);

    vec3 toLight = inter.localLightOrigin.xyz - position.xyz;
    vec3 toView = inter.localViewOrigin.xyz - position.xyz;

    vLightVector = TangentSpaceVector(toLight);
    vHalfAngleVector = TangentSpaceVector(normalize(toLight) + normalize(toView));

    vBumpTexCoord = vec2(dot(texCoord, inter.bumpMatrixS), dot(texCoord, inter.bumpMatrixT));
    vDiffuseTexCoord = vec2(dot(texCoord, inter.diffuseMatrixS), dot(texCoord, inter.diffuseMatrixT));
    vSpecularTexCoord = vec2(dot(texCoord, inter.specularMatrixS), dot(texCoord, inter.specularMatrixT));

    vLightFalloffTexCoord = vec4(dot(position, inter.lightFalloffS), 0.5, 0.0, 1.0);
    vLightProjectionTexCoord = vec4(
        dot(position, inter.lightProjectionS),
        dot(position, inter.lightProjectionT),
        0.0,
        dot(position, inter.lightProjectionQ));

    vec3 shadowNormal = normalize(inNormal);
    float shadowLightCos = max(dot(shadowNormal, normalize(toLight)), 0.0);
    float shadowSinTheta = sqrt(max(1.0 - shadowLightCos * shadowLightCos, 0.0));
    vec4 offsetPosition = vec4(position.xyz + shadowNormal * (shadow.biasParams.w * shadowSinTheta), 1.0);
    vShadowCoord = vec4(
        dot(offsetPosition, shadow.shadowRow0),
        dot(offsetPosition, shadow.shadowRow1),
        dot(offsetPosition, shadow.shadowRow2),
        dot(offsetPosition, shadow.shadowRow3));
    vShadowLightCos = shadowLightCos;

    vVertexColor = inColor.rgb * pc.a.x + vec3(pc.a.y);

    gl_Position = pc.mvp * position;
}

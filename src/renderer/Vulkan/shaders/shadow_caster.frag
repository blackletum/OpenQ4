#version 450

// openQ4 Vulkan shadow-map caster — fragment stage (Phase F2a).
//
// GL shadow_proj_caster.fs parity, minus the hashed-alpha path (plain alpha
// test only, scratch-first):
// - perforated casters alpha-test against the material's alpha map;
// - casters past the falloff plane clamp to far depth instead of vanishing
//   (better behaved near the falloff boundary); only apex-side geometry
//   (depth <= 0), which cannot occlude anything inside the light volume,
//   is discarded — after the texture fetch so derivatives stay defined;
// - shader-written fragment depth bypasses the fixed-function depth bias, so
//   the classic slope-scale caster offset (r_shadowMapPolygonFactor /
//   r_shadowMapPolygonOffset) is applied here via screen-space derivatives,
//   exactly like the GL caster program.

layout(set = 0, binding = 0) uniform sampler2D alphaMap;

layout(push_constant) uniform CasterPushConstants {
    mat4 mvp;
    vec4 depthRow;   // model-local shadow depth plane (clip plane 2)
    vec4 alphaS;     // alpha-test texture matrix S row; z = slope-scale depth factor
    vec4 alphaT;     // alpha-test texture matrix T row; z = constant depth offset
    vec4 params;     // x: alpha mode (0 off, 1 greater, -1 less, 2 equal), y: alphaRef, z: alphaScale
} pc;

layout(location = 0) in vec2 vAlphaTexCoord;
layout(location = 1) in float vShadowDepth;

void main() {
    if (pc.params.x != 0.0) {
        float alpha = texture(alphaMap, vAlphaTexCoord).a * pc.params.z;
        bool testPassed;
        if (pc.params.x < -0.5) {
            testPassed = alpha < pc.params.y;
        } else if (pc.params.x > 1.5) {
            testPassed = abs(alpha - pc.params.y) <= (0.5 / 255.0);
        } else {
            testPassed = alpha > pc.params.y;
        }
        if (!testPassed) {
            discard;
        }
    }

    if (vShadowDepth <= 0.0) {
        discard;
    }

    float depthSlope = max(abs(dFdx(vShadowDepth)), abs(dFdy(vShadowDepth)));
    gl_FragDepth = clamp(vShadowDepth + pc.alphaS.z * depthSlope + pc.alphaT.z, 0.0, 1.0);
}

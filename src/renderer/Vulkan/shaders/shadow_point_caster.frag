#version 450

// openQ4 Vulkan point-light shadow-map caster — fragment stage (Phase F2b).
//
// GL shadow_point_caster.fs parity in its hardware-compare depth variant
// (OPENQ4_POINT_SHADOW_CASTER_DEPTH), minus the hashed-alpha path (plain
// alpha test only — the same F2a reduction):
// - perforated casters alpha-test against the material's alpha map;
// - the stored depth is the normalized radial distance from the light
//   (length of the light-centered view-space position / far), clamped so
//   casters beyond the padded far envelope stay at the far depth instead of
//   vanishing (the receiver treats depth >= 1 as unshadowed);
// - shader-written fragment depth bypasses the fixed-function depth bias,
//   so the slope-scale caster offset (r_shadowMapPolygonFactor /
//   r_shadowMapPolygonOffset) folds in via screen-space derivatives of the
//   radial depth, exactly like the GL caster program.

layout(set = 0, binding = 0) uniform sampler2D alphaMap;

layout(push_constant) uniform CasterPushConstants {
    mat4 mvp;        // model -> cube-face view space
    vec4 depthRow;   // x: zA, y: zB (clip z = zA*z_eye + zB*w_eye), z: far envelope
    vec4 alphaS;     // alpha-test texture matrix S row; z = slope-scale depth factor
    vec4 alphaT;     // alpha-test texture matrix T row; z = constant depth offset
    vec4 params;     // x: alpha mode (0 off, 1 greater, -1 less, 2 equal), y: alphaRef, z: alphaScale
} pc;

layout(location = 0) in vec2 vAlphaTexCoord;
layout(location = 1) in vec3 vPointShadowVector;

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

    if (pc.depthRow.z <= 0.0) {
        discard;
    }

    float rawDepth = length(vPointShadowVector) / pc.depthRow.z;
    float depthSlope = max(abs(dFdx(rawDepth)), abs(dFdy(rawDepth)));
    gl_FragDepth = clamp(rawDepth + pc.alphaS.z * depthSlope + pc.alphaT.z, 0.0, 1.0);
}

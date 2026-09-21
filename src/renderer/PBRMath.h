// Copyright (C) 2026 DarkMatter Productions
// Original shared scalar equations for authored metallic/roughness materials.
#ifndef OPENQ4_PBR_MATH_H
#define OPENQ4_PBR_MATH_H

// Keep the scalar kernel executable by both the native numerical tests and
// GLSL. GL embeds the string below; offline Vulkan shaders include this file.
// No renderer state, texture transfer, or legacy-material policy lives here.
// The 0.045 perceptual floor bounds the mirror peak in a half-float HDR target.
#define OPENQ4_PBR_INLINE
#define OPENQ4_PBR_SCALAR_FUNCTIONS \
OPENQ4_PBR_INLINE float PBRClamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); } \
OPENQ4_PBR_INLINE float PBRRoughness(float r) { return PBRClamp(r, 0.045, 1.0); } \
OPENQ4_PBR_INLINE float PBRFilteredRoughness(float perceptualRoughness, float normalVariance) { \
    float r = PBRRoughness(perceptualRoughness); \
    float alpha = r * r; \
    float kernel = PBRClamp(2.0 * normalVariance, 0.0, 0.18); \
    return sqrt(sqrt(PBRClamp(alpha * alpha + kernel, 0.0, 1.0))); \
} \
OPENQ4_PBR_INLINE float PBRSRGBToLinear(float value) { \
    float x = PBRClamp(value, 0.0, 1.0); \
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4); \
} \
OPENQ4_PBR_INLINE float PBRLinearToSRGB(float value) { \
    float x = PBRClamp(value, 0.0, 1.0); \
    return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055; \
} \
OPENQ4_PBR_INLINE float PBRFresnelWeight(float VoH) { \
    float x = 1.0 - PBRClamp(VoH, 0.0, 1.0); \
    float x2 = x * x; \
    return x2 * x2 * x; \
} \
OPENQ4_PBR_INLINE float PBRDistributionGGX(float NoH, float perceptualRoughness) { \
    float r = PBRRoughness(perceptualRoughness); \
    float alpha = r * r; \
    float n = PBRClamp(NoH, 0.0, 1.0); \
    float aNoH = alpha * n; \
    float denominator = (1.0 - n) * (1.0 + n) + aNoH * aNoH; \
    float ratio = alpha / denominator; \
    return ratio * ratio / 3.141592653589793; \
} \
OPENQ4_PBR_INLINE float PBRVisibilitySmithGGX(float NoV, float NoL, float perceptualRoughness) { \
    float v = PBRClamp(NoV, 0.0, 1.0); \
    float l = PBRClamp(NoL, 0.0, 1.0); \
    if (v <= 0.0 || l <= 0.0) { return 0.0; } \
    float r = PBRRoughness(perceptualRoughness); \
    float alpha = r * r; \
    float a2 = alpha * alpha; \
    float lambdaV = l * sqrt(a2 + (1.0 - a2) * v * v); \
    float lambdaL = v * sqrt(a2 + (1.0 - a2) * l * l); \
    return 0.5 / (lambdaV + lambdaL); \
}

#ifdef __cplusplus
#define OPENQ4_PBR_STRING_IMPL(...) #__VA_ARGS__
#define OPENQ4_PBR_STRING(...) OPENQ4_PBR_STRING_IMPL(__VA_ARGS__)
static const char OPENQ4_PBR_SCALAR_GLSL[] =
    OPENQ4_PBR_STRING(OPENQ4_PBR_SCALAR_FUNCTIONS);
#undef OPENQ4_PBR_STRING
#undef OPENQ4_PBR_STRING_IMPL

#include <cmath>
#undef OPENQ4_PBR_INLINE
#define OPENQ4_PBR_INLINE inline
namespace openq4PBRMath {
using std::pow;
using std::sqrt;
OPENQ4_PBR_SCALAR_FUNCTIONS
}
#else
OPENQ4_PBR_SCALAR_FUNCTIONS
#endif

#endif

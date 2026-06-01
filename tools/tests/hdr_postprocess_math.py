#!/usr/bin/env python3
import math
from pathlib import Path
import sys


LUMA = (0.2126, 0.7152, 0.0722)
BLOOM_SATURATION_WEIGHT = 0.25


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def clamp(value, low, high):
    return max(low, min(high, value))


def smoothstep(edge0, edge1, value):
    if edge1 <= edge0:
        return 1.0 if value >= edge1 else 0.0
    t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, t):
    return tuple(x * (1.0 - t) + y * t for x, y in zip(a, b))


def max_channel(color):
    return max(color[0], color[1], color[2])


def scene_referred_bloom_color(color):
    return tuple(max(channel, 0.0) for channel in color)


def scene_referred_hdr_color(color):
    return tuple(max(channel, 0.0) for channel in color)


def bloom_brightness(color):
    color = scene_referred_bloom_color(color)
    luminance = dot(color, LUMA)
    peak = max_channel(color)
    return luminance * (1.0 - BLOOM_SATURATION_WEIGHT) + peak * BLOOM_SATURATION_WEIGHT


def bloom_contribution(color, threshold, soft_knee):
    brightness = bloom_brightness(color)
    if brightness <= 1.0e-4:
        return 0.0

    if threshold <= 1.0e-4:
        return 1.0

    knee = max(threshold * soft_knee, 0.0)
    soft = 0.0
    if knee > 1.0e-4:
        soft = brightness - threshold + knee
        soft = clamp(soft, 0.0, 2.0 * knee)
        soft = (soft * soft) / max(4.0 * knee, 1.0e-4)

    hard = max(brightness - threshold, 0.0)
    contribution = max(hard, soft)
    return contribution / brightness


def extract_bloom(color, threshold, soft_knee):
    color = scene_referred_bloom_color(color)
    contribution = bloom_contribution(color, threshold, soft_knee)
    return tuple(channel * contribution for channel in color)


def aces_film_scalar(value):
    a = 2.51
    b = 0.03
    c = 2.43
    d = 0.59
    e = 0.14
    return (value * (a * value + b)) / (value * (c * value + d) + e)


def highlight_compress(color, highlight_desaturation, gamut_compression):
    luma = dot(color, LUMA)
    peak = max_channel(color)
    highlight = clamp((peak - 0.6) / 0.4, 0.0, 1.0)
    color = mix(color, (luma, luma, luma), clamp(highlight * highlight_desaturation, 0.0, 1.0))

    peak = max_channel(color)
    if peak > 1.0 and gamut_compression > 0.0:
        compressed_peak = 1.0 + (peak - 1.0) / (1.0 + gamut_compression * (peak - 1.0))
        scale = compressed_peak / peak
        color = tuple(channel * scale for channel in color)

    return color


def tone_map_hdr(color, exposure, white_point, highlight_desaturation, gamut_compression):
    color = scene_referred_hdr_color(color)
    safe_exposure = max(exposure, 1.0e-3)
    exposed = tuple(channel * safe_exposure for channel in color)
    safe_white = max(white_point, 1.0)
    shoulder_start = 0.75
    exposed_white = max(safe_white * safe_exposure, shoulder_start + 1.0e-3)
    shoulder_range = max(exposed_white - shoulder_start, 1.0e-3)
    shoulder_norm = max(1.0 - math.exp(-4.0), 1.0e-4)
    mapped = []
    for channel in exposed:
        if channel < shoulder_start:
            mapped.append(channel)
            continue
        shoulder_t = max(channel - shoulder_start, 0.0) / shoulder_range
        shoulder_value = shoulder_start + (1.0 - shoulder_start) * (1.0 - math.exp(-shoulder_t * 4.0)) / shoulder_norm
        mapped.append(shoulder_value)
    compressed = highlight_compress(tuple(mapped), highlight_desaturation, gamut_compression)
    return tuple(clamp(channel, 0.0, 1.0) for channel in compressed)


def hdr_log_luminance_sample(color):
    color = scene_referred_hdr_color(color)
    luminance = dot(color, LUMA)
    return math.log(max(luminance, 1.0e-4))


def apply_lift_gamma_gain(color, lift, post_gamma, gain):
    safe_gamma = max(post_gamma, 1.0e-3)
    lifted = tuple(max(channel + lift, 0.0) for channel in color)
    corrected = tuple(math.pow(channel, 1.0 / safe_gamma) for channel in lifted)
    return tuple(channel * gain for channel in corrected)


def apply_vibrance(color, vibrance):
    luma = dot(color, LUMA)
    saturation = max_channel(color) - min(color[0], color[1], color[2])
    vibrance_mix = clamp(1.0 + vibrance * (1.0 - saturation), 0.0, 2.0)
    return mix((luma, luma, luma), color, vibrance_mix)


def apply_color_adjustments(color, lift, post_gamma, gain, vibrance, saturation, contrast):
    color = apply_lift_gamma_gain(color, lift, post_gamma, gain)
    color = apply_vibrance(color, vibrance)
    luma = dot(color, LUMA)
    color = mix((luma, luma, luma), color, saturation)
    color = tuple((channel - 0.5) * contrast + 0.5 for channel in color)
    return tuple(clamp(channel, 0.0, 1.0) for channel in color)


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def test_bloom_contribution_monotonic():
    previous = -1.0
    for step in range(0, 200):
        value = step / 16.0
        contribution = bloom_contribution((value, value, value), 1.0, 0.25)
        assert_true(contribution >= previous - 1.0e-6, "bloom contribution must be monotonic")
        previous = contribution


def test_bloom_threshold_is_soft():
    contribution = bloom_contribution((1.0, 1.0, 1.0), 1.0, 0.25)
    assert_true(contribution > 0.0, "soft knee should start contributing at threshold")
    assert_true(contribution < 0.1, "threshold hit should not keep the full source color")


def test_bloom_extract_keeps_only_excess_energy():
    color = (1.25, 1.25, 1.25)
    extracted = extract_bloom(color, 1.0, 0.25)
    assert_true(extracted[0] > 0.0, "above-threshold color should still contribute to bloom")
    assert_true(extracted[0] < color[0], "bloom should keep only energy above threshold")


def test_bloom_extract_preserves_saturated_hdr_highlights():
    red_hdr = extract_bloom((2.0, 0.0, 0.0), 0.7, 0.15)
    blue_hdr = extract_bloom((0.0, 0.0, 4.0), 0.7, 0.15)
    red_ldr = extract_bloom((1.0, 0.0, 0.0), 0.7, 0.15)

    assert_true(red_hdr[0] > 0.0, "saturated red HDR highlights should survive the bloom bright-pass")
    assert_true(blue_hdr[2] > 0.0, "saturated blue HDR highlights should survive the bloom bright-pass")
    assert_true(max_channel(red_ldr) == 0.0, "ordinary saturated LDR color should remain below the default bloom threshold")


def test_bloom_extract_discards_negative_energy():
    extracted = extract_bloom((-4.0, 2.0, -1.0), 0.7, 0.15)
    assert_true(extracted[0] == 0.0 and extracted[2] == 0.0, "bloom should not carry negative scene energy")
    assert_true(extracted[1] >= 0.0, "positive scene energy should remain non-negative")


def test_zero_threshold_extracts_full_color():
    color = (0.8, 0.4, 0.2)
    extracted = extract_bloom(color, 0.0, 0.15)
    for extracted_channel, source_channel in zip(extracted, color):
        assert_true(abs(extracted_channel - source_channel) < 1.0e-6, "zero threshold should keep the full color")


def test_tone_map_monotonic():
    previous = -1.0
    for step in range(0, 1024):
        value = step / 32.0
        mapped = tone_map_hdr((value, value, value), 1.0, 6.0, 0.35, 1.0)[0]
        assert_true(mapped >= previous - 1.0e-6, "tone map must be monotonic")
        previous = mapped


def test_white_point_near_one():
    mapped = tone_map_hdr((6.0, 6.0, 6.0), 1.0, 6.0, 0.35, 1.0)[0]
    assert_true(abs(mapped - 1.0) < 0.05, "white point should map close to display white")


def test_tone_map_preserves_midrange_sdr():
    color = (0.5, 0.5, 0.5)
    mapped = tone_map_hdr(color, 1.0, 6.0, 0.35, 1.0)
    for mapped_channel, source_channel in zip(mapped, color):
        assert_true(abs(mapped_channel - source_channel) < 0.02, "HDR tonemap should preserve midrange SDR contrast before the shoulder")


def test_hdr_rejects_negative_scene_energy():
    mapped = tone_map_hdr((-8.0, 0.5, -0.25), 1.0, 6.0, 0.35, 1.0)
    assert_true(mapped[0] == 0.0 and mapped[2] == 0.0, "negative HDR color must not create tone-mapped output")
    assert_true(mapped[1] > 0.0, "positive HDR color should survive negative-channel cleanup")

    dark_log_luma = hdr_log_luminance_sample((-4.0, -2.0, -1.0))
    assert_true(abs(dark_log_luma - math.log(1.0e-4)) < 1.0e-6, "auto exposure luminance should ignore negative scene energy")


def test_no_nan_edge_cases():
    cases = [
        ((0.0, 0.0, 0.0), 0.0, 1.0, 0.0, 0.0),
        ((32.0, 0.1, 0.1), 0.01, 16.0, 1.0, 4.0),
        ((0.001, 0.002, 0.003), 8.0, 1.0, 0.0, 0.0),
    ]
    for color, exposure, white_point, desat, compression in cases:
        mapped = tone_map_hdr(color, exposure, white_point, desat, compression)
        adjusted = apply_color_adjustments(mapped, -0.25, 2.5, 2.0, 1.0, 2.0, 3.0)
        for channel in adjusted:
            assert_true(math.isfinite(channel), "color adjustment produced a non-finite value")


def test_modern_lighting_keeps_scene_referred_energy():
    shader_library = Path(__file__).resolve().parents[2] / "src" / "renderer" / "ModernGLShaderLibrary.cpp"
    source = shader_library.read_text(encoding="utf-8")

    blocked_lighting_clamps = [
        "out_Color = vec4(clamp(lit, vec3(0.0), vec3(1.0))",
        "out_Color = vec4(clamp(mix(baseColor, blendColor, blendAmount) + lightAccum + emissive, vec3(0.0), vec3(1.0))",
    ]
    for snippet in blocked_lighting_clamps:
        assert_true(snippet not in source, "modern lighting must preserve HDR energy for bloom/tonemap")

    assert_true("ModernSceneReferredColor" in source, "modern shaders should route final scene color through the HDR-safe helper")


def test_bloom_shader_uses_saturation_aware_brightness():
    root = Path(__file__).resolve().parents[2]
    shader = (root / "content" / "baseoq4" / "glprogs" / "bloom_extract.fs").read_text(encoding="utf-8")
    draw_common = (root / "src" / "renderer" / "draw_common.cpp").read_text(encoding="utf-8")

    assert_true("BloomBrightness" in shader, "bloom extraction should use an explicit brightness helper")
    assert_true("mix( luminance, peak, 0.25 )" in shader, "bloom extraction should preserve saturated HDR highlights")
    assert_true("SceneReferredBloomColor" in shader, "bloom extraction should reject negative scene energy")
    assert_true("r_bloomIntensity.GetFloat() > 0.0001f" in draw_common, "zero-intensity bloom should not force the bloom pass")


def test_hdr_shader_uses_scene_referred_inputs():
    root = Path(__file__).resolve().parents[2]
    composite = (root / "content" / "baseoq4" / "glprogs" / "bloom.fs").read_text(encoding="utf-8")
    luminance = (root / "content" / "baseoq4" / "glprogs" / "hdr_luminance.fs").read_text(encoding="utf-8")
    draw_common = (root / "src" / "renderer" / "draw_common.cpp").read_text(encoding="utf-8")

    assert_true("SceneReferredHDRColor" in composite, "HDR composite should sanitize scene-referred color before tonemap/debug")
    assert_true("sampleColor = max( sampleColor, vec3( 0.0 ) );" in luminance, "auto exposure should ignore negative scene energy")
    assert_true("r_hdrAutoExposure.GetBool() && r_hdrToneMap.GetBool()" in draw_common, "auto exposure should not request HDR work when tonemap is off")


def main():
    tests = [
        test_bloom_contribution_monotonic,
        test_bloom_threshold_is_soft,
        test_bloom_extract_keeps_only_excess_energy,
        test_bloom_extract_preserves_saturated_hdr_highlights,
        test_bloom_extract_discards_negative_energy,
        test_zero_threshold_extracts_full_color,
        test_tone_map_monotonic,
        test_white_point_near_one,
        test_tone_map_preserves_midrange_sdr,
        test_hdr_rejects_negative_scene_energy,
        test_no_nan_edge_cases,
        test_modern_lighting_keeps_scene_referred_energy,
        test_bloom_shader_uses_saturation_aware_brightness,
        test_hdr_shader_uses_scene_referred_inputs,
    ]

    for test in tests:
        test()

    print("hdr_postprocess_math: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

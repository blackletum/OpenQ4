// Copyright (C) 2026 DarkMatter Productions
// Execute the same scalar kernel embedded in GL and included by Vulkan.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../../../src/renderer/PBRMath.h"
#include "../../../src/renderer/PBREnvironment.h"
#include "../../../src/imagetools/SRGB.h"

namespace kernel = openq4PBRMath;

static void Require(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "PBRMathTest: %s\n", message); std::exit(1); }
}

static double RelativeError(double a, double b) {
    return std::fabs(a - b) / std::fmax(std::fabs(b), 1e-20);
}

// Independent double-precision reference: Smith's correlated lambda form.
static double ReferenceVisibility(double v, double l, double roughness) {
    const double a2 = std::pow(roughness, 4.0);
    const double lambdaV = (std::sqrt(1.0 + a2 * (1.0 - v * v) / (v * v)) - 1.0) / 2.0;
    const double lambdaL = (std::sqrt(1.0 + a2 * (1.0 - l * l) / (l * l)) - 1.0) / 2.0;
    return 1.0 / (4.0 * v * l * (1.0 + lambdaV + lambdaL));
}

static void EnvironmentTests() {
    using namespace openq4PBR;
    // Independent numeric decoding covers every nonnegative finite binary16
    // value, including subnormals and the mantissa/exponent carry boundaries.
    for (unsigned int half = 0; half <= 0x7bff; ++half) {
        const int exponent = half >> 10, mantissa = half & 1023;
        const float value = exponent == 0 ? std::ldexp(float(mantissa), -24)
            : std::ldexp(1.0f + float(mantissa)/1024.0f, exponent-15);
        Require(RadianceHalf(value) == half, "finite half-float round trip");
        if (half < 0x7bff) {
            const unsigned int next = half + 1;
            const float nextValue = (next >> 10) == 0 ? std::ldexp(float(next & 1023), -24)
                : std::ldexp(1.0f + float(next & 1023)/1024.0f, int(next >> 10)-15);
            Require(RadianceHalf((value + nextValue)*0.5f) == (half & 1 ? next : half),
                "half-float midpoint ties to even");
        }
    }
    Require(RadianceHalf(-1.0f) == 0 && RadianceHalf(INFINITY) == 0
        && RadianceHalf(NAN) == 0 && RadianceHalf(1e10f) == 0x7bff,
        "invalid radiance and finite HDR saturation");
    Cube cube;
    cube.size = 8;
    for (auto &face : cube.faces) { face.assign(64, {0.25f, 0.5f, 2.0f}); }
    for (int face = 0; face < 6; ++face) for (float r : {0.0f, 0.25f, 0.5f, 1.0f}) {
        const auto filtered = PrefilterFace(cube, face, 8, r);
        for (int pixel = 0; pixel < 64; ++pixel) {
            Require(std::fabs(filtered[pixel*4] - 0.25f) < 1e-5f, "constant-radiance specular furnace");
            Require(std::fabs(filtered[pixel*4+2] - 2.0f) < 1e-5f, "HDR probe must not clamp");
        }
    }
    const auto diffuse = DiffuseIrradiance(cube, 8);
    for (int pixel = 0; pixel < 64; ++pixel) { Require(std::fabs(diffuse[pixel*4+1] - 0.5f) < 1e-5f, "diffuse irradiance normalization"); }
    for (int face = 0; face < 6; ++face) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const Vector d = FaceDirection(face, 2.0f*(x+0.5f)/8-1, 2.0f*(y+0.5f)/8-1);
        cube.faces[face][y*8+x] = (d + Vector{1,1,1})*0.5f;
    }
    for (Vector edge : {Vector{1,1,0.3f}, Vector{1,0.3f,1}, Vector{0.3f,1,-1}}) {
        const Vector a = cube(edge*1.001f + Vector{0.001f,0,0});
        const Vector b = cube(edge + Vector{0,0.001f,0});
        Require(Dot(a-b, a-b) < 0.001f, "cube seams cross into neighbouring faces");
    }
    const auto impulse = [](Vector d) -> Vector { const float x = d.z > 0.99f ? 1.0f : 0.0f; return {x,x,x}; };
    const auto sharp = PrefilterFace(impulse, 4, 9, 0);
    const auto rough = PrefilterFace(impulse, 4, 9, 0.8f, 1024);
    Require(sharp[40*4] == 1 && rough[40*4] < 0.1f && rough[40*4] > 0.001f, "roughness filters the source highlight");
    Require(rough[38*4] > sharp[38*4], "roughness broadens the highlight");
    const auto smoothBRDF = IntegrateBRDF(1, 0.045f, 4096);
    Require(smoothBRDF[0] > 0.99f && smoothBRDF[1] < 0.001f, "split-sum mirror reference");
    const auto roughBRDF = IntegrateBRDF(1, 1, 4096);
    Require(std::fabs(roughBRDF[0] + roughBRDF[1] - (1-std::log(2.0))) < 0.001, "split-sum rough furnace reference");
}

int main() {
    const double pi = 3.141592653589793;
    const float roughnesses[] = {0.045f, 0.08f, 0.2f, 0.5f, 0.8f, 1.0f};
    Require(std::fabs(kernel::PBRSRGBToLinear(0.5f) - 0.21404114f) < 1e-7f, "sRGB midpoint");
    Require(std::fabs(kernel::PBRSRGBToLinear(0.02f) - 0.02f / 12.92f) < 1e-8f, "sRGB toe");
    for (int i = 0; i <= 255; ++i) {
        const float x = i / 255.0f;
        Require(std::fabs(kernel::PBRLinearToSRGB(kernel::PBRSRGBToLinear(x)) - x) < 2e-7f, "sRGB round trip");
        Require(std::fabs(openq4SRGB::Decode(x)-kernel::PBRSRGBToLinear(x)) < 2e-7f, "CPU image/shader sRGB agreement");
    }
    // A checker mip must preserve half the light, while coverage averages
    // linearly. Gamma-correcting alpha incorrectly gives 186 instead of 128.
    const unsigned char checker[16]={0,0,0,0, 255,255,255,255, 255,255,255,255, 0,0,0,0};
    unsigned char mip[4]={};
    openq4SRGB::Downsample(checker,2,2,mip);
    Require(mip[0]==188 && mip[1]==188 && mip[2]==188 && mip[3]==128,"linear-light RGB mip and linear coverage");
    openq4SRGB::Downsample(checker,1,2,mip);
    Require(mip[0]==188 && mip[3]==128,"one-column sRGB mip");
    openq4SRGB::Downsample(checker,2,1,mip);
    Require(mip[0]==188 && mip[3]==128,"one-row sRGB mip");
    const unsigned char toe[8]={0,0,0,17, 20,20,20,239};
    openq4SRGB::Downsample(toe,2,1,mip);
    Require(mip[0]==11 && mip[3]==128,"sRGB toe differs from power-2.2 filtering");
    Require(kernel::PBRVisibilitySmithGGX(0, 1, 0) == 0, "grazing limit");
    Require(kernel::PBRVisibilitySmithGGX(1, -1, 0) == 0, "back-face light");
    Require(kernel::PBRRoughness(0) == 0.045f && kernel::PBRRoughness(2) == 1, "roughness floor/range");
    for (float r : roughnesses) {
        Require(std::fabs(kernel::PBRFilteredRoughness(r, 0)-r)<1e-7f,"specular AA preserves a constant normal");
        float previous = r;
        for (float variance : {0.00001f,0.0001f,0.001f,0.01f,0.1f,10.0f}) {
            const float filtered = kernel::PBRFilteredRoughness(r,variance);
            Require(filtered>=previous && filtered<=1,"specular AA broadens without overflow");
            previous=filtered;
        }
        Require(kernel::PBRFilteredRoughness(r,0.1f)==kernel::PBRFilteredRoughness(r,10),"specular AA footprint cap");
    }

    for (float r : roughnesses) {
        const double expectedPeak = 1.0 / (pi * std::pow(double(r), 4.0));
        Require(RelativeError(kernel::PBRDistributionGGX(1, r), expectedPeak) < 1e-6, "GGX smooth peak suppressed");
        Require(kernel::PBRVisibilitySmithGGX(1, 1, r) == 0.25f, "Cook-Torrance factor of four");
        Require(kernel::PBRDistributionGGX(1, r) * 0.25f < 65504, "half-float highlight overflow");
        for (int vi = 1; vi <= 80; ++vi) {
            const float v = vi / 80.0f;
            for (int li = 1; li <= 80; ++li) {
                const float l = li / 80.0f;
                const float visibility = kernel::PBRVisibilitySmithGGX(v, l, r);
                Require(std::isfinite(visibility) && visibility > 0, "finite positive visibility");
                Require(RelativeError(visibility, ReferenceVisibility(v, l, r)) < 2e-6, "Smith correlated reference");
                Require(visibility == kernel::PBRVisibilitySmithGGX(l, v, r), "BRDF reciprocity");
            }
        }

        // A white conductor in a unit radiance furnace cannot create energy.
        // Integrate with a stratified GGX half-vector distribution, independent
        // of the kernel's D expression. This also covers very narrow lobes.
        for (float v : {0.05f, 0.2f, 0.6f, 1.0f}) {
            const double vx = std::sqrt(1.0 - double(v) * v);
            const double a2 = std::pow(double(r), 4.0);
            double energy = 0;
            const int rows = 256, columns = 256;
            for (int y = 0; y < rows; ++y) {
                const double u = (y + 0.5) / rows;
                const double hz = std::sqrt((1.0 - u) / (1.0 + (a2 - 1.0) * u));
                const double radial = std::sqrt(1.0 - hz * hz);
                for (int x = 0; x < columns; ++x) {
                    const double hx = radial * std::cos(2.0 * pi * (x + 0.5) / columns);
                    const double vh = vx * hx + v * hz;
                    const double l = 2.0 * vh * hz - v;
                    if (l > 0 && vh > 0) {
                        energy += 4.0 * l * kernel::PBRVisibilitySmithGGX(v, float(l), r) * vh / hz;
                    }
                }
            }
            energy /= rows * columns;
            Require(std::isfinite(energy) && energy > 0 && energy <= 1.005, "white furnace energy gain");
        }
    }
    EnvironmentTests();
    std::puts("PBRMathTest: passed sRGB, mirror peaks, Smith reference, reciprocity, 24 white furnaces, probe convolution, HDR, seams, irradiance and split-sum integration");
    return 0;
}

// Copyright (C) 2026 DarkMatter Productions
// Native opaque/perforated PBR inputs shared by unshadowed, projected and
// point-shadow interactions. The classic pipeline keeps its original ABI.
// pc.c.xyw carries the data-layout bits, normal encoding and specular-AA switch.
// The classic specular-table sampler is unused by PBR and carries metallic.

// Native ordered transparency: pc.a.w carries the authored blend stage's
// alpha register, and the albedo image supplies the per-texel coverage the
// classic stage would have sampled. Every other interaction draw is additive
// and keeps the zero-alpha contract.
float PBRTransparentAlpha(vec2 albedoTexCoord) {
    if (pc.a.w <= 0.0) {
        return 0.0;
    }
    return clamp(texture(diffuseMap, albedoTexCoord).a * pc.a.w, 0.0, 1.0);
}

vec3 PBRDirectNormal(vec2 texCoord) {
    int encoding = int(pc.c.y + 0.5);
    if (encoding == 0) {
        return vec3(0.0, 0.0, 1.0);
    }
    vec4 value = texture(bumpMap, texCoord);
    vec3 normal;
    if (encoding == 2) {
        vec2 xy = (value.rg * 2.0 - 1.0) * pc.d.w;
        normal = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
    } else {
        normal = (encoding == 3 ? value.rgb : value.agb) * 2.0 - 1.0;
        normal.xy *= pc.d.w;
    }
    float lengthSquared = dot(normal, normal);
    return lengthSquared > 1.0e-8
        ? normal * inversesqrt(lengthSquared) : vec3(0.0, 0.0, 1.0);
}

vec3 EvaluatePBRDirect(vec3 localNormal, vec2 albedoTexCoord,
        vec2 dataTexCoord, float shadowFactor) {
    // Color uses sRGB storage and decodes before filtering. Data stays linear.
    vec3 albedo = texture(diffuseMap, albedoTexCoord).rgb;
    int dataFlags = int(pc.c.x + 0.5);
    vec2 materialData = vec2(1.0);
    if ((dataFlags & 1) != 0) {
        materialData = texture(specularMap, dataTexCoord).bg;
    } else {
        if ((dataFlags & 2) != 0) {
            materialData.x = texture(specularTableMap, dataTexCoord).r;
        }
        if ((dataFlags & 4) != 0) {
            materialData.y = texture(specularMap, dataTexCoord).r;
        }
    }
    float metallic = clamp(materialData.x * pc.d.y, 0.0, 1.0);
    vec3 radiance = textureProj(lightFalloffMap, vLightFalloffTexCoord).rgb
        * textureProj(lightProjectionMap, vLightProjectionTexCoord).rgb
        * inter.diffuseColor.rgb * shadowFactor;
    if (pc.a.z > 0.5) {
        // Authored ambient lights are an isotropic diffuse source, matching
        // ModernClusterEvaluatePBRLight. They have neither the classic
        // tangent-space ambient direction nor a view-dependent specular lobe.
        // Metallic response belongs to the environment, and authored AO is
        // reserved for that indirect source rather than this light stage.
        return radiance * albedo * (1.0 - metallic)
            * (0.96 / 3.14159265) * vVertexColor;
    }
    float roughness = PBRRoughness(materialData.y * pc.d.z);
    // A flat tangent-space normal still varies across a curved surface.
    // Measure the final normal in object space; rigid model rotation leaves
    // this variance unchanged. The three interaction variants share the same
    // footprint before any per-fragment lighting rejection.
    vec3 objectNormal = SafeNormalize(
        SafeNormalize(vPBRTangent0) * localNormal.x
        + SafeNormalize(vPBRTangent1) * localNormal.y
        + SafeNormalize(vPBRNormal) * localNormal.z);
    vec3 normalDx = dFdx(objectNormal);
    vec3 normalDy = dFdy(objectNormal);
    if (pc.c.w > 0.5) {
        roughness = PBRFilteredRoughness(roughness,
            0.5 * (dot(normalDx, normalDx) + dot(normalDy, normalDy)));
    }
    vec3 lightDir = SafeNormalize(vLightVector);
    vec3 viewDir = SafeNormalize(vViewVector);
    vec3 halfDir = SafeNormalize(lightDir + viewDir);
    float ndotl = max(dot(objectNormal, lightDir), 0.0);
    float ndotv = max(dot(objectNormal, viewDir), 0.0);
    float ndoth = max(dot(objectNormal, halfDir), 0.0);
    float vdoth = max(dot(viewDir, halfDir), 0.0);
    if (ndotl <= 0.0 || ndotv <= 0.0 || dot(lightDir + viewDir, lightDir + viewDir) <= 1.0e-8) {
        return vec3(0.0);
    }
    float distribution = PBRDistributionGGX(ndoth, roughness);
    float visibility = PBRVisibilitySmithGGX(ndotv, ndotl, roughness);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = f0 + (vec3(1.0) - f0) * PBRFresnelWeight(vdoth);
    vec3 specular = distribution * visibility * fresnel;
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic)
        * albedo * (1.0 / 3.14159265);
    // Authored AO modulates indirect irradiance, never this direct light.
    return (diffuse + specular) * radiance * ndotl * vVertexColor;
}

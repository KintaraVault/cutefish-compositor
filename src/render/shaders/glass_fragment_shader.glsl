varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform vec4 uSampleRect;
uniform vec2 uSize;
uniform vec2 uCoverage;
// The blurred backdrop's texels, fewer than the coverage's pixels for a wide blur.
uniform vec2 uBackdropSize;
uniform float uRadius;
uniform float uBevel;
uniform float uDepth;
uniform float uEta;
uniform float uProfile;
uniform float uDisplacement;
uniform float uChroma;
uniform vec3 uLight;
uniform vec3 uSpecular;
uniform vec2 uAmbient;
uniform float uSaturation;
// Intensity, and one logical pixel in device pixels.
uniform vec2 uRim;
uniform float uRimWrap;
uniform float uBackdropReflection;
uniform float uRimSpread;
uniform float uSoftBackdrop;
uniform vec4 uTint;
uniform vec2 uTone;
// The lens within the quad, in device pixels; a client shape masks the quad.
uniform vec4 uLens;
uniform sampler2D uShape;
uniform float uHasShape;
uniform vec4 uShapeRect;
uniform float uOpacity;
uniform float uNestedLens;
uniform float uFrosted;

float roundRect(vec2 p, vec2 halfSize, float r) {
    if (uBackdropReflection > 0.0) {
        // Match the actual circular arcs of the shell's path mask, so the
        // optical lip doesn't disappear into, or detach from, the corners.
        vec2 q = abs(p) - halfSize + r;
        return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    }
    return roundedDistance(p, vec4(-halfSize, halfSize * 2.0), vec2(r));
}

// Superellipse surface: flat on top, rolling off to nothing over one band.
float height(float sd, float band) {
    float t = clamp(-sd / band, 0.0, 1.0);
    float n = max(uProfile, 1.01);
    return pow(max(1.0 - pow(1.0 - t, n), 0.0), 1.0 / n) * uDepth;
}

// Outward direction of the distance field: along an axis on the straight edges,
// following the corner's gradient inside a corner.
vec2 outwardAt(vec2 p, vec2 halfSize, float r) {
    vec2 radius = cornerExtent(vec2(r), halfSize);
    vec2 edge = abs(p) - halfSize;
    vec2 q = edge + radius;
    if (min(radius.x, radius.y) <= 0.0 || min(q.x, q.y) <= 0.0)
        return edge.x > edge.y ? vec2(sign(p.x), 0.0) : vec2(0.0, sign(p.y));
    if (uBackdropReflection > 0.0)
        return sign(p) * normalize(q);
    return sign(p) * cornerNormal(q / radius, radius);
}

// The surface normal at one point. One forward sample along the outward
// direction is enough for the slope, and unlike a centred difference it cannot
// straddle the corner rounding. A superellipse edge is unboundedly steep, so
// the slope is capped at a few times the average across the bevel.
vec3 normalAt(vec2 p, vec2 halfSize, float r, float band) {
    vec2 outward = outwardAt(p, halfSize, r);
    float slope = height(roundRect(p + outward, halfSize, r), band)
                - height(roundRect(p, halfSize, r), band);
    float limit = uDepth / max(band * 0.7, 1.0);
    return normalize(vec3(-outward * clamp(slope, -limit, limit), 1.0));
}

// A shape reaching past the lens, such as an arrow, continues its nearest edge,
// so it matches the lip it grows out of instead of showing the plain backdrop.
vec2 insideLens(vec2 p, vec2 halfSize, float r) {
    float sd = roundRect(p, halfSize, r);
    return sd > 0.0 ? p - outwardAt(p, halfSize, r) * (sd + 0.5) : p;
}

// Where the eye ray leaves the flat back face of the glass, as an offset into
// the backdrop. The z floor bounds it smoothly; a hard clamp on its length
// would put a discontinuous ring along the lip, which aliases badly.
vec2 lensOffset(vec2 p, vec2 halfSize, float r, float band) {
    p = insideLens(p, halfSize, r);
    vec3 ray = refract(vec3(0.0, 0.0, -1.0), normalAt(p, halfSize, r, band), uEta);
    if (length(ray) < 0.0001)
        return vec2(0.0);
    return ray.xy / max(-ray.z, 0.45) * uDisplacement;
}

vec3 backdrop(vec2 uv) {
    return texture2D(uTexture, vec2(uv.x, 1.0 - uv.y)).rgb;
}

float luminance(vec3 rgb) {
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

vec3 scatteringRow(vec2 uv, vec2 dx, vec2 margin) {
    return 0.35 * (backdrop(clamp(uv - dx, margin, 1.0 - margin))
                 + backdrop(clamp(uv + dx, margin, 1.0 - margin)))
         + 0.15 * (backdrop(clamp(uv - 3.0 * dx, margin, 1.0 - margin))
                 + backdrop(clamp(uv + 3.0 * dx, margin, 1.0 - margin)));
}

vec4 softBackdrop(vec2 position, vec2 uv, vec2 margin) {
    // Wider scattering of the same Gaussian-filtered backdrop as the lenses.
    vec2 stepUv = 10.0 * uRim.y / uCoverage;
    vec2 dx = vec2(stepUv.x, 0.0);
    vec2 dy = vec2(0.0, stepUv.y);
    vec3 color = 0.35 * (scatteringRow(uv - dy, dx, margin) + scatteringRow(uv + dy, dx, margin))
               + 0.15 * (scatteringRow(uv - 3.0 * dy, dx, margin) + scatteringRow(uv + 3.0 * dy, dx, margin));
    vec2 pos = position / uRim.y;
    vec2 remaining = (uLens.zw - position) / uRim.y;
    float horizontal = smoothstep(0.0, 68.0, pos.x) * smoothstep(0.0, 20.0, remaining.x);
    float vertical = smoothstep(0.0, 52.0, pos.y) * smoothstep(0.0, 65.0, remaining.y);
    // A bright scene already separates the darker lenses from the gutters;
    // keep the wide blur but don't leave a dark sheet across that backdrop.
    float dim = mix(0.16, 0.19, smoothstep(0.20, 0.85, luminance(color)));
    return vec4(color * (1.0 - dim), 1.0) * (horizontal * vertical * uOpacity);
}

// Approximate integration over the pixel's projection onto the edge normal.
// At 45 degrees that footprint is wider than on the flats. A constant width
// makes the crest grow teeth as the corners move at fractional scale.
float rimBand(float distance, float start, float end, vec2 outward) {
    float halfWidth = max(0.5, 0.5 * dot(abs(outward), vec2(1.0)));
    return smoothstep(start - halfWidth, start + halfWidth, distance)
         - smoothstep(end - halfWidth, end + halfWidth, distance);
}

vec3 reflectedRim(vec3 transmission, vec2 uv, vec2 outward, vec3 normal,
                  float sd, float band, vec2 margin) {
    float pixel = uRim.y;
    float d = -sd;
    vec2 tap = outward / uCoverage;
    vec2 tangent = vec2(-tap.y, tap.x);
    // The lip reflects a small patch outside the outline. The footprint fans
    // out with the bevel slope; opposite sides can pick up different colours.
    float reach = max(d, 0.0) + band * mix(0.45, 1.15, length(normal.xy));
    // Outside and inside the bevel have different footprints. In particular,
    // a crossing bright window shouldn't paint the same highlight all around.
    vec2 ray = uv + tap * reach;
    vec3 environment = 0.48 * backdrop(clamp(ray, margin, 1.0 - margin));
    environment += 0.22 * backdrop(clamp(ray + tangent * band * 0.64, margin, 1.0 - margin));
    environment += 0.22 * backdrop(clamp(ray - tangent * band * 0.64, margin, 1.0 - margin));
    environment += 0.08 * backdrop(clamp(ray + tap * band * 1.2, margin, 1.0 - margin));
    vec3 local = backdrop(clamp(uv, margin, 1.0 - margin));
    float fresnel = pow(1.0 - clamp(normal.z, 0.0, 1.0), 2.0);
    vec2 light = normalize(uLight.xy);
    float incident = dot(outward, light);
    float key = pow(max(incident, 0.0), 2.1);
    float bounce = pow(max(-incident, 0.0), 1.75);
    float crestStart = mix(0.40, 0.10, key) * pixel;
    float crestEnd = crestStart + mix(0.68, 1.05, max(key, bounce)) * pixel;
    float lip = rimBand(d, crestStart, crestEnd, outward);
    float spread = max(pixel * 0.5, uRimSpread * max(pixel, band * (0.25 + .18 * key)));
    float shoulder = uRimSpread * smoothstep(crestStart - .5, crestEnd, d)
                   * exp(-max(d - crestEnd, 0.0) / spread);
    float ambient = luminance(environment);
    float contrast = clamp(0.65 + 1.4 * (ambient - luminance(local)), 0.30, 1.0);
    float weight = uRim.x * uBackdropReflection;
    // Two broad incident lights sweep along the curvature. The backdrop
    // supplies the hue; neutral incident light prevents a black-level cutoff.
    float bright = smoothstep(0.30, 0.9, ambient);
    vec3 color = mix(transmission, environment, weight * (0.09 * lip + 0.25 * fresnel * shoulder));
    vec3 caustic = sqrt(max(environment, vec3(0.0))) * 0.64 + (1.0 - bright) * 0.36;
    float reflectedLight = 0.66 + 1.20 * key + 0.93 * bounce;
    float energy = weight * contrast * (reflectedLight * lip + (0.09 + .14 * key) * shoulder);
    // Compress the brightest glints instead of clipping a channel at the rim.
    color += (1.0 - exp(-caustic * energy)) * (1.0 - color);
    float silhouette = 1.0 - smoothstep(-0.30 * pixel, 0.82 * pixel, d);
    // A pale backdrop has a luminous edge rather than a black keyline. On
    // saturated backdrops the outer ray keeps the hue through the shoulder.
    return clamp(color * (1.0 - weight * silhouette * mix(0.17, 0.08, ambient)), 0.0, 1.0);
}

// Sample each ray before averaging: averaging offsets loses the pixel footprint
// where the lens crosses a high-contrast backdrop edge.
vec3 refractedAt(vec2 p, vec2 offset, vec2 halfSize, float r, float band,
                 vec2 uv, vec2 texel, vec2 margin) {
    vec2 bend = lensOffset(p + offset, halfSize, r, band);
    float reach = min(uCoverage.x, uCoverage.y) * 0.35;
    if (length(bend) > reach)
        bend = normalize(bend) * reach;
    vec2 duv = bend / uCoverage;
    vec2 sampleUv = uv + offset * texel;
    vec3 color;
    color.r = backdrop(clamp(sampleUv + duv * (1.0 + uChroma), margin, 1.0 - margin)).r;
    color.g = backdrop(clamp(sampleUv + duv, margin, 1.0 - margin)).g;
    color.b = backdrop(clamp(sampleUv + duv * (1.0 - uChroma), margin, 1.0 - margin)).b;
    return color;
}

// A thicker diffuser than the separate control lenses. The five binomial taps
// in each axis integrate over the already softly filtered capture; unlike a
// second layer of tint, this hides hard edges while keeping broad colour fields.
float frostWeight(int i) {
    return i == 0 ? 0.375 : (i == -1 || i == 1 ? 0.25 : 0.0625);
}

vec3 frostedSheet(vec2 p, vec2 halfSize, float r, float band, float sd, vec2 uv, vec2 margin) {
    vec2 bend = 0.24 * lensOffset(p, halfSize, r, band) / uCoverage;
    vec2 stepUv = 25.0 * uRim.y / uCoverage;
    vec3 scene = vec3(0.0);
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 sampleUv = clamp(uv + bend + vec2(float(x), float(y)) * stepUv, margin, 1.0 - margin);
            vec3 sampleColor = backdrop(sampleUv);
            // Bright content (especially the dock or white windows) shouldn't
            // burn an opaque white silhouette through the material. Compress
            // the incident light before integrating it into the diffuser.
            sampleColor *= 1.0 - 0.32 * smoothstep(0.46, 1.0, luminance(sampleColor));
            scene += sampleColor * (frostWeight(x) * frostWeight(y));
        }
    }

    // Keep wallpaper colour separate from the neutral tone curve. A neutral
    // backdrop stays neutral, while either theme keeps enough contrast for ink.
    float y = luminance(scene);
    vec3 chroma = scene - vec3(y);
    bool dark = uFrosted < 0.0;
    float value = dark ? 0.092 + 0.24 * y : 0.53 + 0.32 * y;
    float spread = dark ? 0.34 : 0.71;
    float negative = -min(chroma.r, min(chroma.g, chroma.b));
    float positive = max(chroma.r, max(chroma.g, chroma.b));
    spread = min(spread, min((value - 0.035) / max(negative, 0.001),
                            (0.96 - value) / max(positive, 0.001)));
    vec3 color = vec3(value) + chroma * spread;

    // This is a satin sheet, not another thick tile. Keep a restrained,
    // scene-coloured glint at the very edge without tracing a separate outline.
    if (sd > -4.0 * uRim.y && uBackdropReflection > 0.0) {
        vec2 edge = insideLens(p, halfSize, r);
        float falloff = smoothstep(-4.0 * uRim.y, -1.5 * uRim.y, sd);
        color = mix(color, reflectedRim(color, uv, outwardAt(edge, halfSize, r),
                                      normalAt(edge, halfSize, r, band), sd, band, margin), 0.34 * falloff);
    }
    return color;
}

void shade() {
    vec2 halfSize = uLens.zw * 0.5;
    vec2 p = vTexCoord * uSize - uLens.xy - halfSize;
    vec2 uv = uSampleRect.xy + vTexCoord * uSampleRect.zw;
    // Bilinear filtering must not reach past what was captured.
    vec2 margin = 1.2 / uBackdropSize;
    if (uSoftBackdrop > 0.0) {
        gl_FragColor = softBackdrop(p + halfSize, uv, margin);
        return;
    }
    float r = clamp(uRadius, 0.0, min(halfSize.x, halfSize.y));
    float sd = roundRect(p, halfSize, r);
    // The roll-off band is bounded so the glass cannot keep curving towards the
    // middle of a large surface.
    float band = clamp(uBevel > 0.0 ? uBevel : r, 1.0, min(halfSize.x, halfSize.y));

    if (abs(uFrosted) > 0.5) {
        float mask = mix(1.0, texture2D(uShape, uShapeRect.xy + vTexCoord * uShapeRect.zw).a, uHasShape);
        gl_FragColor = vec4(frostedSheet(p, halfSize, r, band, sd, uv, margin), 1.0) * mask * uOpacity;
        return;
    }

    // Over the lip the offset changes so fast that neighbouring pixels read far
    // apart in the backdrop, so widen the sampling footprint there.
    vec2 texel = mix(0.75, 2.5, clamp(1.0 + sd / band, 0.0, 1.0)) / uCoverage;
    vec3 refracted = 0.25 * (
        refractedAt(p, vec2(0.375, -0.125), halfSize, r, band, uv, texel, margin) +
        refractedAt(p, vec2(0.125, 0.375), halfSize, r, band, uv, texel, margin) +
        refractedAt(p, vec2(-0.375, 0.125), halfSize, r, band, uv, texel, margin) +
        refractedAt(p, vec2(-0.125, -0.375), halfSize, r, band, uv, texel, margin));

    float luma = luminance(refracted);
    refracted = max(mix(vec3(luma), refracted, uSaturation), 0.0);
    vec3 color = mix(refracted, uTint.rgb, uTint.a);
    color = mix(color, vec3(1.0), uTone.x * (1.0 - smoothstep(0.015, 0.34, luma)));
    color *= 1.0 - uTone.y * smoothstep(0.36, 0.94, luma);
    // The glass body blocks light near its edge, which is what anchors it on a
    // backdrop too bright for the refraction to show.
    float occluded = 1.0 - smoothstep(0.0, max(uAmbient.y, 0.001), -sd);
    color *= 1.0 - occluded * uAmbient.x;

    vec3 normal = normalAt(insideLens(p, halfSize, r), halfSize, r, band);
    vec3 reflected = reflect(-uLight, normal);
    float specular = pow(max(reflected.z, 0.0), max(uSpecular.y, 1.0)) * uSpecular.x;
    float sheen = pow(max(dot(normal, uLight), 0.0), 1.65) * uSpecular.z;

    // Screen blend rather than addition, so light over a bright backdrop
    // brightens towards white instead of blowing out. Without a rim the edge is
    // not lit here: the window outline layer draws it, over the client buffer.
    vec3 lit = vec3(specular + sheen) * (1.0 - uBackdropReflection);
    color = color + lit - color * lit;
    float peak = max(color.r, max(color.g, color.b));
    if (peak > 1.0)
        color /= peak;

    // A darker hairline wraps the whole edge. Inside it the edges facing up and
    // down catch a light from above: a lifted hairline with a glow fading
    // inwards, weaker along the bottom. Sharing a pixel, the two would cancel
    // into a smudge, and a lit edge with nothing outside it reads as cut off.
    if (uBackdropReflection > 0.0) {
        color = reflectedRim(color, uv, outwardAt(insideLens(p, halfSize, r), halfSize, r),
                             normal, sd, band, margin);
    } else if (uRim.x > 0.0) {
        vec2 outward = outwardAt(insideLens(p, halfSize, r), halfSize, r);
        // Surface-local y grows downwards, so the top faces -y.
        float lit = mix(uRimWrap, 1.0, abs(outward.y)) * mix(1.0, mix(0.35, 0.77, uRimWrap), 0.5 + 0.5 * outward.y);
        float inset = -sd;
        // Three quarters and half a logical pixel, never thinner than a device pixel.
        float litWidth = max(0.75 * uRim.y, 1.0);
        float shadeWidth = max(0.5 * uRim.y, 1.0);
        float litStart = shadeWidth;
        // The part of this device pixel the lit hairline covers.
        float hairline = clamp(min(inset + 0.5, litStart + litWidth) - max(inset - 0.5, litStart), 0.0, 1.0);
        float glow = exp(-max(inset - litStart - litWidth, 0.0) / (1.7 * uRim.y))
                   * clamp(inset + 0.5 - litStart, 0.0, 1.0);
        // The glow is additive, so the same glow shows over a white window and a
        // dark one. A body within a few levels of white has no room for it: the
        // light would clip into a soft white band instead of stepping down.
        float headroom = 1.0 - smoothstep(0.88, 1.0, dot(color, vec3(0.2126, 0.7152, 0.0722)));
        color += uRim.x * lit * headroom * 0.075 * glow;
        // The hairline screens: a dark body gets a bright line, a light one a
        // faint one. Added, it clipped to white over a light body, and a clipped
        // line loses its antialiasing and steps along the corners.
        color = 1.0 - (1.0 - color) * (1.0 - uRim.x * lit * 0.43 * hairline);
        // Lighter along the top and bottom, where the lit hairline sits inside it.
        float shade = (0.3 + 0.7 * outward.x * outward.x) * (1.0 - smoothstep(shadeWidth - 0.5, shadeWidth + 0.5, inset));
        color = mix(color, color * (1.0 - 0.1 * uRim.x) - 0.05 * uRim.x, shade);
        color = clamp(color, 0.0, 1.0);
    }

    // The rounded coverage of the clip this draw is wrapped in masks the
    // result, so the lens does not antialias its own silhouette: that shape is
    // curved, and a one pixel ramp off the distance field visibly stairsteps
    // where the outline next to it does not.
    float mask = mix(1.0, texture2D(uShape, uShapeRect.xy + vTexCoord * uShapeRect.zw).a, uHasShape);
    if (uNestedLens > 0.0) {
        vec2 n = outwardAt(insideLens(p, halfSize, r), halfSize, r);
        float footprint = max(0.5, 0.5 * dot(abs(n), vec2(1.0)));
        mask *= 1.0 - smoothstep(-footprint, footprint, sd);
    }
    gl_FragColor = vec4(max(color, vec3(0.0)), 1.0) * mask * uOpacity;
}

/***************************************************************************
 *   Copyright (C) 2024 by Deeprey Research Ltd                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "grib_shaders.h"

#include <cstdio>
#include <cstring>

// Program handles
GLuint grib_particle_update_program = 0;
GLuint grib_trail_decay_program = 0;
GLuint grib_particle_draw_program = 0;
GLuint grib_ribbon_draw_program = 0;
GLuint grib_trail_composite_program = 0;

// GL 3.3 preamble
static const GLchar* grib_shader_preamble = "#version 330 core\n";

// ============================================================================
// Fullscreen quad vertex shader (shared by update, decay, composite)
// ============================================================================

static const GLchar* fullscreen_quad_vertex_source =
    "in vec2 aPos;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "    vTexCoord = aPos * 0.5 + 0.5;\n"
    "}\n";

// ============================================================================
// Shader 1: Particle State Update — advect particles via wind textures
// ============================================================================

static const GLchar* particle_update_fragment_source =
    "uniform sampler2D uStateTex;\n"
    "uniform sampler2D uWindU;\n"
    "uniform sampler2D uWindV;\n"
    "uniform sampler2D uRandomTex;\n"
    "uniform sampler2D uWavePeriod;\n"
    "uniform int uWaveMode;\n"
    "\n"
    "uniform vec2 uGridOrigin;\n"    // (Lo1, La1) — bottom-left of grid
    "uniform vec2 uGridSpacing;\n"   // (Di, abs(Dj))
    "uniform ivec2 uGridSize;\n"     // (Ni, Nj)
    "uniform vec4 uGridBounds;\n"    // (lonMin, lonMax, latMin, latMax)
    "uniform vec4 uSpawnBounds;\n"   // visible area clamped to grid
    "\n"
    "uniform float uMaxAge;\n"
    "uniform float uDropRate;\n"
    "uniform float uDropRateBump;\n"
    "uniform float uSpeedFactor;\n"
    "uniform float uRandomSeed;\n"
    "\n"
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "\n"
    "const float PI = 3.14159265;\n"
    "\n"
    "float hash(vec2 p) {\n"
    "    return fract(sin(dot(p + uRandomSeed, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 state = texture(uStateTex, vTexCoord);\n"
    "    float lon = state.r;\n"
    "    float lat = state.g;\n"
    "    float age = state.b;\n"
    "    float speed = state.a;\n"
    "\n"
    "    vec4 rnd = texture(uRandomTex, vTexCoord);\n"
    "    float h = hash(vTexCoord);\n"
    "    float h2 = hash(vTexCoord + vec2(0.37, 0.91));\n"
    "\n"
    "    // Wrap longitude to grid range (handles 0-360 vs -180/+180 mismatch)\n"
    "    float gridWidth = float(uGridSize.x - 1) * uGridSpacing.x;\n"
    "    float wlon = lon;\n"
    "    if (wlon < uGridOrigin.x) wlon += 360.0;\n"
    "    if (wlon > uGridOrigin.x + gridWidth) wlon -= 360.0;\n"
    "\n"
    "    // Convert geo position to wind texture UV\n"
    "    vec2 windUV = vec2(\n"
    "        (wlon - uGridOrigin.x) / gridWidth,\n"
    "        (lat - uGridOrigin.y) / (float(uGridSize.y - 1) * uGridSpacing.y)\n"
    "    );\n"
    "\n"
    "    // Kill particles outside valid grid UV range (avoids edge artifacts)\n"
    "    bool outsideGrid = windUV.x < 0.01 || windUV.x > 0.99\n"
    "                    || windUV.y < 0.01 || windUV.y > 0.99;\n"
    "\n"
    "    float rawU = texture(uWindU, windUV).r;\n"
    "    float rawV = texture(uWindV, windUV).r;\n"
    "\n"
    "    float u, v, magnitude;\n"
    "\n"
    "    if (uWaveMode == 1) {\n"
    "        // Wave mode: uWindU=height, uWindV=direction(deg), uWavePeriod=period(s)\n"
    "        float waveHeight = rawU;\n"
    "        float dirDeg = rawV;\n"
    "        float period = max(texture(uWavePeriod, windUV).r, 1.0);\n"
    "        // Deep water celerity: C = 1.56 * T (m/s)\n"
    "        float celerity = 1.56 * period;\n"
    "        // Meteorological convention: direction waves come FROM\n"
    "        float dirRad = dirDeg * PI / 180.0;\n"
    "        u = -celerity * sin(dirRad);\n"
    "        v = -celerity * cos(dirRad);\n"
    "        magnitude = waveHeight;\n"
    "    } else {\n"
    "        // Wind mode: u,v are Cartesian wind components\n"
    "        u = rawU;\n"
    "        v = rawV;\n"
    "        magnitude = length(vec2(u, v));\n"
    "    }\n"
    "\n"
    "    // Advect: convert m/s to degree displacement\n"
    "    float cosLat = cos(radians(lat));\n"
    "    cosLat = max(cosLat, 0.01);\n"
    "    float dlon = u * uSpeedFactor / (cosLat * 111320.0);\n"
    "    float dlat = v * uSpeedFactor / 110540.0;\n"
    "\n"
    "    lon += dlon;\n"
    "    lat += dlat;\n"
    "    age += 1.0;\n"
    "    speed = magnitude;\n"
    "\n"
    "    // Drop rate: calm areas lose particles faster\n"
    "    float dropMag = (uWaveMode == 1) ? magnitude * 3.0 : magnitude;\n"
    "    float drop = uDropRate + uDropRateBump / (dropMag + 0.5);\n"
    "\n"
    "    // Margin around visible area before killing particles\n"
    "    float marginLon = (uSpawnBounds.y - uSpawnBounds.x) * 0.3;\n"
    "    float marginLat = (uSpawnBounds.w - uSpawnBounds.z) * 0.3;\n"
    "    bool outsideView = wlon < uSpawnBounds.x - marginLon\n"
    "                    || wlon > uSpawnBounds.y + marginLon\n"
    "                    || lat < uSpawnBounds.z - marginLat\n"
    "                    || lat > uSpawnBounds.w + marginLat;\n"
    "\n"
    "    float minMag = (uWaveMode == 1) ? 0.05 : 0.01;\n"
    "    bool shouldRespawn = age >= uMaxAge\n"
    "                      || outsideGrid\n"
    "                      || wlon < uGridBounds.x || wlon > uGridBounds.y\n"
    "                      || lat < uGridBounds.z || lat > uGridBounds.w\n"
    "                      || magnitude < minMag\n"
    "                      || outsideView\n"
    "                      || h < drop;\n"
    "\n"
    "    if (shouldRespawn) {\n"
    "        // Respawn within visible area (clamped to grid bounds)\n"
    "        float rx = mix(rnd.x, h, 0.3);\n"
    "        float ry = mix(rnd.y, h2, 0.3);\n"
    "        lon = mix(uSpawnBounds.x, uSpawnBounds.y, rx);\n"
    "        lat = mix(uSpawnBounds.z, uSpawnBounds.w, ry);\n"
    "        age = floor(rnd.z * uMaxAge * 0.3);\n"
    "        speed = 0.0;\n"
    "    }\n"
    "\n"
    "    fragColor = vec4(lon, lat, age, speed);\n"
    "}\n";

// ============================================================================
// Shader 2: Trail Decay — fade previous trail with optional pan shift
// ============================================================================

static const GLchar* trail_decay_fragment_source =
    "uniform sampler2D uTrailTex;\n"
    "uniform float uDecay;\n"
    "uniform vec2 uPanOffset;\n"  // normalized pixel shift
    "\n"
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = vTexCoord + uPanOffset;\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
    "        fragColor = vec4(0.0);\n"
    "        return;\n"
    "    }\n"
    "    vec4 old = texture(uTrailTex, uv);\n"
    "    fragColor = vec4(old.rgb, old.a * uDecay);\n"
    "}\n";

// ============================================================================
// Shader 3: Particle Draw — point sprites projected from geo coords
// ============================================================================

static const GLchar* particle_draw_vertex_source =
    "uniform sampler2D uStateTex;\n"
    "uniform sampler2D uPrevStateTex;\n"
    "uniform int uParticleTexSize;\n"
    "\n"
    "uniform vec2 uViewCenterMerc;\n"  // pre-computed Mercator center (meters)
    "uniform vec2 uRotCS;\n"           // pre-computed (cos, sin) of rotation
    "uniform float uViewScalePPM;\n"
    "uniform vec2 uViewSize;\n"
    "uniform float uRotation;\n"
    "\n"
    "uniform float uMaxSpeed;\n"
    "uniform float uMaxAge;\n"
    "\n"
    "// Wave mode uniforms\n"
    "uniform int uWaveMode;\n"
    "uniform sampler2D uWindU;\n"     // wave height texture
    "uniform sampler2D uWindV;\n"     // wave direction texture
    "uniform sampler2D uWavePeriod;\n"
    "uniform vec2 uGridOrigin;\n"
    "uniform vec2 uGridSpacing;\n"
    "uniform ivec2 uGridSize;\n"
    "uniform float uWaveCrestScale;\n"
    "\n"
    "out float vAlpha;\n"
    "out float vCrestAngle;\n"
    "\n"
    "const float PI = 3.14159265;\n"
    "const float DEG_TO_RAD = PI / 180.0;\n"
    "const float EARTH_RADIUS = 6378137.0;\n"
    "\n"
    "vec2 geoToNDC(float lon, float lat) {\n"
    "    // Normalize to -180/+180 for Mercator projection\n"
    "    if (lon > 180.0) lon -= 360.0;\n"
    "    float x = lon * DEG_TO_RAD * EARTH_RADIUS;\n"
    "    float latRad = lat * DEG_TO_RAD;\n"
    "    float y = log(tan(latRad * 0.5 + 0.7853981633974483)) * EARTH_RADIUS;\n"
    "    vec2 offsetPx = vec2(x - uViewCenterMerc.x, y - uViewCenterMerc.y) * uViewScalePPM;\n"
    "    vec2 rotated = vec2(offsetPx.x * uRotCS.x - offsetPx.y * uRotCS.y,\n"
    "                        offsetPx.x * uRotCS.y + offsetPx.y * uRotCS.x);\n"
    "    vec2 screenPos = uViewSize * 0.5 + vec2(rotated.x, -rotated.y);\n"
    "    vec2 ndc = (screenPos / uViewSize) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    return ndc;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    // GL_LINES: 2 vertices per particle for both modes\n"
    "    int particleId = gl_VertexID / 2;\n"
    "    int isHead = gl_VertexID % 2;\n"
    "\n"
    "    int tx = particleId % uParticleTexSize;\n"
    "    int ty = particleId / uParticleTexSize;\n"
    "    vec2 tc = (vec2(tx, ty) + 0.5) / float(uParticleTexSize);\n"
    "\n"
    "    vec4 curr = texture(uStateTex, tc);\n"
    "    vec4 prev = texture(uPrevStateTex, tc);\n"
    "    float speed = curr.a;\n"
    "    float age = curr.b;\n"
    "\n"
    "    vCrestAngle = 0.0;\n"
    "\n"
    "    if (uWaveMode == 1) {\n"
    "        // Wave mode: draw crest line perpendicular to wave direction\n"
    "        // Both endpoints at current position, offset along crest\n"
    "        float lon = curr.r;\n"
    "        float lat = curr.g;\n"
    "        float waveHeight = speed;\n"
    "\n"
    "        // Look up direction from grid (wrap lon to grid convention)\n"
    "        float wlon = lon;\n"
    "        if (wlon < uGridOrigin.x) wlon += 360.0;\n"
    "        if (wlon > uGridOrigin.x + float(uGridSize.x - 1) * uGridSpacing.x) wlon -= 360.0;\n"
    "        vec2 windUV = vec2(\n"
    "            (wlon - uGridOrigin.x) / (float(uGridSize.x - 1) * uGridSpacing.x),\n"
    "            (lat - uGridOrigin.y) / (float(uGridSize.y - 1) * uGridSpacing.y)\n"
    "        );\n"
    "        float dirDeg = texture(uWindV, windUV).r;\n"
    "        float dirRad = dirDeg * DEG_TO_RAD;\n"
    "\n"
    "        // Crest half-length in pixels, scales with wave height and zoom\n"
    "        float halfLen = clamp((waveHeight * 2.0 + 3.0) * uWaveCrestScale, 3.0, 15.0);\n"
    "\n"
    "        // Offset: vertex 0 = left end, vertex 1 = right end\n"
    "        float side = isHead == 1 ? 1.0 : -1.0;\n"
    "        vec2 center = geoToNDC(lon, lat);\n"
    "        // Perpendicular to propagation in geo space: (cos(dir), -sin(dir))\n"
    "        // After chart rotation, convert to NDC\n"
    "        float perpAngle = dirRad - uRotation;\n"
    "        vec2 offsetNDC = side * halfLen * vec2(\n"
    "            cos(perpAngle) * 2.0 / uViewSize.x,\n"
    "            -sin(perpAngle) * 2.0 / uViewSize.y\n"
    "        );\n"
    "        gl_Position = vec4(center + offsetNDC, 0.0, 1.0);\n"
    "\n"
    "        // Alpha from wave height\n"
    "        vAlpha = clamp(waveHeight / 4.0, 0.4, 1.0);\n"
    "\n"
    "        // Hide dead or just-spawned\n"
    "        if (waveHeight < 0.05 || age < 2.0) {\n"
    "            gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "            vAlpha = 0.0;\n"
    "        }\n"
    "    } else {\n"
    "        // Wind mode: line from previous to current position\n"
    "        float lon = isHead == 1 ? curr.r : prev.r;\n"
    "        float lat = isHead == 1 ? curr.g : prev.g;\n"
    "        gl_Position = vec4(geoToNDC(lon, lat), 0.0, 1.0);\n"
    "        vAlpha = isHead == 1 ? 1.0 : 0.5;\n"
    "        if (speed < 0.01 || age < 2.0) {\n"
    "            gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "            vAlpha = 0.0;\n"
    "        }\n"
    "    }\n"
    "}\n";

static const GLchar* particle_draw_fragment_source =
    "in float vAlpha;\n"
    "in float vCrestAngle;\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    fragColor = vec4(1.0, 1.0, 1.0, vAlpha);\n"
    "}\n";

// ============================================================================
// Shader 5: Ribbon Draw — comet-tail ribbons from trail history (wind mode)
// ============================================================================

static const GLchar* ribbon_draw_vertex_source =
    "uniform sampler2DArray uTrailArray;\n"
    "uniform int uTrailWriteIdx;\n"
    "uniform int uValidTrailCount;\n"
    "uniform int uParticleTexSize;\n"
    "\n"
    "uniform vec2 uViewCenterMerc;\n"  // pre-computed Mercator center (meters)
    "uniform vec2 uRotCS;\n"           // pre-computed (cos, sin) of rotation
    "uniform float uViewScalePPM;\n"
    "uniform vec2 uViewSize;\n"
    "\n"
    "uniform int uStride;\n"
    "uniform int uVertsPerParticle;\n"
    "\n"
    "out float vT;\n"
    "out float vSide;\n"
    "out float vAlpha;\n"
    "\n"
    "const int TRAIL_LEN = 16;\n"
    "const float PI = 3.14159265;\n"
    "const float DEG_TO_RAD = PI / 180.0;\n"
    "const float EARTH_RADIUS = 6378137.0;\n"
    "\n"
    "vec2 geoToNDC(float lon, float lat) {\n"
    "    // Normalize to -180/+180 for Mercator projection\n"
    "    if (lon > 180.0) lon -= 360.0;\n"
    "    float x = lon * DEG_TO_RAD * EARTH_RADIUS;\n"
    "    float latRad = lat * DEG_TO_RAD;\n"
    "    float y = log(tan(latRad * 0.5 + 0.7853981633974483)) * EARTH_RADIUS;\n"
    "    vec2 offsetPx = vec2(x - uViewCenterMerc.x, y - uViewCenterMerc.y) * uViewScalePPM;\n"
    "    vec2 rotated = vec2(offsetPx.x * uRotCS.x - offsetPx.y * uRotCS.y,\n"
    "                        offsetPx.x * uRotCS.y + offsetPx.y * uRotCS.x);\n"
    "    vec2 screenPos = uViewSize * 0.5 + vec2(rotated.x, -rotated.y);\n"
    "    vec2 ndc = (screenPos / uViewSize) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "    return ndc;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    int particleId = gl_VertexID / uVertsPerParticle;\n"
    "    int localVert  = gl_VertexID % uVertsPerParticle;\n"
    "    int segIdx     = localVert / 6;\n"
    "    int vertInQuad = localVert % 6;\n"
    "\n"
    "    // Texel coord for this particle\n"
    "    int tx = particleId % uParticleTexSize;\n"
    "    int ty = particleId / uParticleTexSize;\n"
    "\n"
    "    // Two trail point indices for this segment via stride\n"
    "    int i0 = segIdx * uStride;\n"
    "    int i1 = i0 + uStride;\n"
    "\n"
    "    // Hide if not enough history\n"
    "    if (i1 >= uValidTrailCount) {\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        vT = 1.0; vSide = 0.0; vAlpha = 0.0;\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // Ring buffer layer mapping (most recent = writeIdx-1)\n"
    "    int layer0 = (uTrailWriteIdx - 1 - i0 + TRAIL_LEN * 2) % TRAIL_LEN;\n"
    "    int layer1 = (uTrailWriteIdx - 1 - i1 + TRAIL_LEN * 2) % TRAIL_LEN;\n"
    "\n"
    "    // Fetch particle state from trail array\n"
    "    vec4 s0 = texelFetch(uTrailArray, ivec3(tx, ty, layer0), 0);\n"
    "    vec4 s1 = texelFetch(uTrailArray, ivec3(tx, ty, layer1), 0);\n"
    "\n"
    "    float speed0 = s0.a;\n"
    "    float age0   = s0.b;\n"
    "    float speed1 = s1.a;\n"
    "    float age1   = s1.b;\n"
    "\n"
    "    // Kill dead or just-spawned particles\n"
    "    if (speed0 < 0.01 || age0 < 2.0) {\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        vT = 1.0; vSide = 0.0; vAlpha = 0.0;\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // Respawn detection: age should increase between snapshots.\n"
    "    // A decrease (or the older point being dead) means a respawn happened.\n"
    "    float ageDiff = age0 - age1;\n"
    "    if (ageDiff < 0.0 || speed1 < 0.01) {\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        vT = 1.0; vSide = 0.0; vAlpha = 0.0;\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    vec2 p0 = geoToNDC(s0.r, s0.g);\n"
    "    vec2 p1 = geoToNDC(s1.r, s1.g);\n"
    "\n"
    "    // Fallback: large screen jump = teleport\n"
    "    float dist = length((p1 - p0) * uViewSize * 0.5);\n"
    "    if (dist > 50.0) {\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        vT = 1.0; vSide = 0.0; vAlpha = 0.0;\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    // Segment direction and perpendicular\n"
    "    vec2 dir = p1 - p0;\n"
    "    float dirLen = length(dir);\n"
    "    vec2 perp;\n"
    "    if (dirLen > 0.0001) {\n"
    "        vec2 nd = dir / dirLen;\n"
    "        perp = vec2(-nd.y, nd.x);\n"
    "    } else {\n"
    "        perp = vec2(1.0, 0.0);\n"
    "    }\n"
    "\n"
    "    // Taper width: head=full, tail=zero (in NDC, ~2px half-width)\n"
    "    float maxHalfW = 2.0 / min(uViewSize.x, uViewSize.y);\n"
    "    float t0 = float(i0) / float(TRAIL_LEN - 1);\n"
    "    float t1 = float(i1) / float(TRAIL_LEN - 1);\n"
    "    float hw0 = mix(maxHalfW, 0.0, t0);\n"
    "    float hw1 = mix(maxHalfW, 0.0, t1);\n"
    "\n"
    "    // Quad vertex layout: 0,1,2, 2,1,3 (two triangles)\n"
    "    //   0=(p0, left)  1=(p0, right)  2=(p1, left)  3=(p1, right)\n"
    "    int quadCorner;\n"
    "    if      (vertInQuad == 0) quadCorner = 0;\n"
    "    else if (vertInQuad == 1) quadCorner = 1;\n"
    "    else if (vertInQuad == 2) quadCorner = 2;\n"
    "    else if (vertInQuad == 3) quadCorner = 2;\n"
    "    else if (vertInQuad == 4) quadCorner = 1;\n"
    "    else                      quadCorner = 3;\n"
    "\n"
    "    vec2 pos;\n"
    "    float side;\n"
    "    float t;\n"
    "    if (quadCorner < 2) {\n"
    "        side = (quadCorner == 0) ? -1.0 : 1.0;\n"
    "        pos = p0 + perp * side * hw0;\n"
    "        t = t0;\n"
    "    } else {\n"
    "        side = (quadCorner == 2) ? -1.0 : 1.0;\n"
    "        pos = p1 + perp * side * hw1;\n"
    "        t = t1;\n"
    "    }\n"
    "\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "    vT = t;\n"
    "    vSide = side;\n"
    "    vAlpha = clamp(speed0 / 15.0, 0.3, 1.0);\n"
    "}\n";

static const GLchar* ribbon_draw_fragment_source =
    "in float vT;\n"
    "in float vSide;\n"
    "in float vAlpha;\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    // Blue-white tint: bright head, cooler tail\n"
    "    vec3 headColor = vec3(0.9, 0.95, 1.0);\n"
    "    vec3 tailColor = vec3(0.5, 0.7, 1.0);\n"
    "    vec3 color = mix(headColor, tailColor, vT);\n"
    "\n"
    "    // Radial falloff across ribbon width\n"
    "    float radial = smoothstep(0.0, 0.4, 1.0 - abs(vSide));\n"
    "\n"
    "    // Alpha: taper along trail, speed brightness, radial softness\n"
    "    float alpha = mix(1.0, 0.0, vT) * vAlpha * radial;\n"
    "\n"
    "    // Premultiplied alpha for additive blending\n"
    "    fragColor = vec4(color * alpha, alpha);\n"
    "}\n";

// ============================================================================
// Shader 4: Trail Composite — blend trail FBO onto the chart
// ============================================================================

static const GLchar* trail_composite_vertex_source =
    "in vec2 aPos;\n"
    "uniform mat4 uMVP;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
    "    vTexCoord = aPos * 0.5 + 0.5;\n"
    "}\n";

static const GLchar* trail_composite_fragment_source =
    "uniform sampler2D uTrailTex;\n"
    "uniform float uOpacity;\n"
    "\n"
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    vec4 trail = texture(uTrailTex, vTexCoord);\n"
    "    fragColor = vec4(trail.rgb, trail.a * uOpacity);\n"
    "}\n";

// ============================================================================
// Shader compilation helpers
// ============================================================================

static GLuint CompileShader(const GLchar* source, GLenum type) {
  GLuint shader = glCreateShader(type);
  const GLchar* sources[] = {grib_shader_preamble, source};
  GLint lengths[] = {(GLint)strlen(grib_shader_preamble),
                     (GLint)strlen(source)};
  glShaderSource(shader, 2, sources, lengths);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(shader, 512, nullptr, infoLog);
    printf("GRIB GPU shader compile error:\n%s\n", infoLog);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint LinkProgram(GLuint vert, GLuint frag) {
  if (!vert || !frag) return 0;

  GLuint program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(program, 512, nullptr, infoLog);
    printf("GRIB GPU shader link error:\n%s\n", infoLog);
    glDeleteProgram(program);
    program = 0;
  }

  glDeleteShader(vert);
  glDeleteShader(frag);
  return program;
}

// ============================================================================
// Public API
// ============================================================================

DpGribGLCapabilities grib_DetectCapabilities() {
  DpGribGLCapabilities caps = {false, false, 0};

  GLint major = 0, minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);

  caps.hasGL33 = (major > 3 || (major == 3 && minor >= 3));
  caps.hasGL43 = (major > 4 || (major == 4 && minor >= 3));
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.maxTextureSize);

  printf("GRIB GPU: GL %d.%d, GL33=%d\n", major, minor, caps.hasGL33);
  return caps;
}

static bool CompileProgram(const char* name, const GLchar* vertSrc,
                           const GLchar* fragSrc, GLuint& outProgram) {
  GLuint vert = CompileShader(vertSrc, GL_VERTEX_SHADER);
  GLuint frag = CompileShader(fragSrc, GL_FRAGMENT_SHADER);
  outProgram = LinkProgram(vert, frag);
  if (!outProgram) {
    printf("GRIB GPU: Failed to compile %s shader\n", name);
    return false;
  }
  printf("GRIB GPU: %s shader compiled OK\n", name);
  return true;
}

bool grib_InitGPUShaders() {
  bool ok = true;

  ok &= CompileProgram("particle_update",
                        fullscreen_quad_vertex_source,
                        particle_update_fragment_source,
                        grib_particle_update_program);

  ok &= CompileProgram("trail_decay",
                        fullscreen_quad_vertex_source,
                        trail_decay_fragment_source,
                        grib_trail_decay_program);

  ok &= CompileProgram("particle_draw",
                        particle_draw_vertex_source,
                        particle_draw_fragment_source,
                        grib_particle_draw_program);

  ok &= CompileProgram("ribbon_draw",
                        ribbon_draw_vertex_source,
                        ribbon_draw_fragment_source,
                        grib_ribbon_draw_program);

  ok &= CompileProgram("trail_composite",
                        trail_composite_vertex_source,
                        trail_composite_fragment_source,
                        grib_trail_composite_program);

  if (!ok) {
    grib_CleanupGPUShaders();
    return false;
  }

  printf("GRIB GPU: All 5 particle shaders compiled OK\n");
  return true;
}

void grib_CleanupGPUShaders() {
  auto cleanup = [](GLuint& prog) {
    if (prog) { glDeleteProgram(prog); prog = 0; }
  };
  cleanup(grib_particle_update_program);
  cleanup(grib_trail_decay_program);
  cleanup(grib_particle_draw_program);
  cleanup(grib_ribbon_draw_program);
  cleanup(grib_trail_composite_program);
}

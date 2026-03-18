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
    "\n"
    "uniform vec2 uGridOrigin;\n"    // (Lo1, La1) — bottom-left of grid
    "uniform vec2 uGridSpacing;\n"   // (Di, abs(Dj))
    "uniform ivec2 uGridSize;\n"     // (Ni, Nj)
    "uniform vec4 uGridBounds;\n"    // (lonMin, lonMax, latMin, latMax)
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
    "    // Convert geo position to wind texture UV\n"
    "    vec2 windUV = vec2(\n"
    "        (lon - uGridOrigin.x) / (float(uGridSize.x - 1) * uGridSpacing.x),\n"
    "        (lat - uGridOrigin.y) / (float(uGridSize.y - 1) * uGridSpacing.y)\n"
    "    );\n"
    "\n"
    "    float u = texture(uWindU, windUV).r;\n"
    "    float v = texture(uWindV, windUV).r;\n"
    "    float windSpeed = length(vec2(u, v));\n"
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
    "    speed = windSpeed;\n"
    "\n"
    "    // Drop rate: calm areas lose particles faster\n"
    "    float drop = uDropRate + uDropRateBump / (windSpeed + 0.5);\n"
    "    bool shouldRespawn = age >= uMaxAge\n"
    "                      || lon < uGridBounds.x || lon > uGridBounds.y\n"
    "                      || lat < uGridBounds.z || lat > uGridBounds.w\n"
    "                      || windSpeed < 0.01\n"
    "                      || h < drop;\n"
    "\n"
    "    if (shouldRespawn) {\n"
    "        // Respawn at random position using the random texture\n"
    "        // Mix between using random tex values and hash for variety\n"
    "        float rx = mix(rnd.x, h, 0.3);\n"
    "        float ry = mix(rnd.y, h2, 0.3);\n"
    "        lon = mix(uGridBounds.x, uGridBounds.y, rx);\n"
    "        lat = mix(uGridBounds.z, uGridBounds.w, ry);\n"
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
    "uniform int uParticleTexSize;\n"
    "\n"
    "// Viewport projection\n"
    "uniform vec2 uViewCenter;\n"      // (clon, clat)
    "uniform float uViewScalePPM;\n"
    "uniform vec2 uViewSize;\n"        // (pix_width, pix_height)
    "uniform float uRotation;\n"
    "\n"
    "uniform float uMaxSpeed;\n"
    "uniform float uMinPointSize;\n"
    "uniform float uMaxPointSize;\n"
    "uniform float uMaxAge;\n"
    "\n"
    "out float vAlpha;\n"
    "out vec3 vColor;\n"
    "\n"
    "const float DEG_TO_RAD = 3.14159265 / 180.0;\n"
    "const float EARTH_RADIUS = 6378137.0;\n"
    "\n"
    "vec2 mercatorProject(float lon, float lat) {\n"
    "    float x = lon * DEG_TO_RAD * EARTH_RADIUS;\n"
    "    float latRad = lat * DEG_TO_RAD;\n"
    "    float y = log(tan(latRad * 0.5 + 0.7853981633974483)) * EARTH_RADIUS;\n"
    "    return vec2(x, y);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    int id = gl_VertexID;\n"
    "    int tx = id % uParticleTexSize;\n"
    "    int ty = id / uParticleTexSize;\n"
    "    vec2 tc = (vec2(tx, ty) + 0.5) / float(uParticleTexSize);\n"
    "\n"
    "    vec4 state = texture(uStateTex, tc);\n"
    "    float lon = state.r;\n"
    "    float lat = state.g;\n"
    "    float age = state.b;\n"
    "    float speed = state.a;\n"
    "\n"
    "    vec2 particleM = mercatorProject(lon, lat);\n"
    "    vec2 centerM = mercatorProject(uViewCenter.x, uViewCenter.y);\n"
    "\n"
    "    vec2 offsetM = particleM - centerM;\n"
    "    vec2 offsetPx = offsetM * uViewScalePPM;\n"
    "\n"
    "    float c = cos(uRotation);\n"
    "    float s = sin(uRotation);\n"
    "    vec2 rotated = vec2(\n"
    "        offsetPx.x * c - offsetPx.y * s,\n"
    "        offsetPx.x * s + offsetPx.y * c\n"
    "    );\n"
    "\n"
    "    vec2 screenPos = uViewSize * 0.5 + vec2(rotated.x, -rotated.y);\n"
    "    vec2 ndc = (screenPos / uViewSize) * 2.0 - 1.0;\n"
    "    ndc.y = -ndc.y;\n"
    "\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "\n"
    "    float speedNorm = clamp(speed / uMaxSpeed, 0.0, 1.0);\n"
    "    gl_PointSize = mix(uMinPointSize, uMaxPointSize, speedNorm);\n"
    "\n"
    "    // Bright white — trail decay handles the dimming over time\n"
    "    vColor = vec3(1.0);\n"
    "    vAlpha = 1.0;\n"
    "\n"
    "    // Hide dead or just-spawned particles\n"
    "    if (speed < 0.01 || age < 1.0) {\n"
    "        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);\n"
    "        vAlpha = 0.0;\n"
    "    }\n"
    "}\n";

static const GLchar* particle_draw_fragment_source =
    "in float vAlpha;\n"
    "in vec3 vColor;\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    vec2 pc = gl_PointCoord * 2.0 - 1.0;\n"
    "    float dist = dot(pc, pc);\n"
    "    if (dist > 1.0) discard;\n"
    "    float softEdge = 1.0 - smoothstep(0.3, 1.0, dist);\n"
    "    fragColor = vec4(vColor * vAlpha * softEdge, vAlpha * softEdge);\n"
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

  ok &= CompileProgram("trail_composite",
                        trail_composite_vertex_source,
                        trail_composite_fragment_source,
                        grib_trail_composite_program);

  if (!ok) {
    grib_CleanupGPUShaders();
    return false;
  }

  printf("GRIB GPU: All 4 particle shaders compiled OK\n");
  return true;
}

void grib_CleanupGPUShaders() {
  auto cleanup = [](GLuint& prog) {
    if (prog) { glDeleteProgram(prog); prog = 0; }
  };
  cleanup(grib_particle_update_program);
  cleanup(grib_trail_decay_program);
  cleanup(grib_particle_draw_program);
  cleanup(grib_trail_composite_program);
}

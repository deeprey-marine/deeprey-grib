/***************************************************************************
 *   Copyright (C) 2024 by Deeprey Research Ltd                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#include "DpGribGPUParticles.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "GribRecord.h"
#include "GribSettingsDialog.h"
#include "ocpn_plugin.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float RandFloat(float lo, float hi) {
  return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

DpGribGPUParticles::DpGribGPUParticles()
    : m_initialized(false),
      m_fboWidth(0),
      m_fboHeight(0),
      m_particleTexSize(0),
      m_totalParticles(0),
      m_stateIndex(0),
      m_trailIndex(0),
      m_windTexU(0),
      m_windTexV(0),
      m_windNi(0),
      m_windNj(0),
      m_randomTex(0),
      m_quadVAO(0),
      m_quadVBO(0),
      m_gridLo1(0),
      m_gridLa1(0),
      m_gridDi(0),
      m_gridDj(0),
      m_gridLonMin(0),
      m_gridLonMax(0),
      m_gridLatMin(0),
      m_gridLatMax(0),
      m_lastClat(0),
      m_lastClon(0),
      m_lastScale(0),
      m_lastPixWidth(0),
      m_lastPixHeight(0),
      m_lastRotation(0),
      m_lastRefScreenX(0),
      m_lastRefScreenY(0),
      m_lastGRX(nullptr),
      m_lastGRY(nullptr),
      m_spawnLonMin(0),
      m_spawnLonMax(0),
      m_spawnLatMin(0),
      m_spawnLatMax(0),
      m_frameCount(0),
      m_density(0),
      m_speedFactor(100.0f) {
  m_stateTexture[0] = m_stateTexture[1] = 0;
  m_stateFBO[0] = m_stateFBO[1] = 0;
  m_trailTexture[0] = m_trailTexture[1] = 0;
  m_trailFBO[0] = m_trailFBO[1] = 0;
}

DpGribGPUParticles::~DpGribGPUParticles() {
  DestroyFBOs();
  DestroyTextures();
  if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
  if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
}

// ============================================================================
// Initialization
// ============================================================================

bool DpGribGPUParticles::Initialize(const DpGribGLCapabilities &caps) {
  if (!caps.hasGL33) return false;

  CreateFullscreenQuadVAO();
  m_initialized = true;
  return true;
}

void DpGribGPUParticles::CreateFullscreenQuadVAO() {
  // NDC fullscreen quad: two triangles covering [-1, 1]
  static const float quadVerts[] = {
      -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
      -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,  1.0f,
  };

  glGenVertexArrays(1, &m_quadVAO);
  glGenBuffers(1, &m_quadVBO);

  glBindVertexArray(m_quadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ============================================================================
// FBO Management
// ============================================================================

bool DpGribGPUParticles::CreateFBOs(int width, int height) {
  DestroyFBOs();

  m_fboWidth = width;
  m_fboHeight = height;

  for (int i = 0; i < 2; i++) {
    glGenTextures(1, &m_trailTexture[i]);
    glBindTexture(GL_TEXTURE_2D, m_trailTexture[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_trailFBO[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, m_trailFBO[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_trailTexture[i], 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      printf("GRIB GPU: Trail FBO %d incomplete!\n", i);
      DestroyFBOs();
      return false;
    }

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  m_trailIndex = 0;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

bool DpGribGPUParticles::CreateParticleStateTextures(int texSize) {
  // Destroy old state textures
  for (int i = 0; i < 2; i++) {
    if (m_stateFBO[i]) {
      glDeleteFramebuffers(1, &m_stateFBO[i]);
      m_stateFBO[i] = 0;
    }
    if (m_stateTexture[i]) {
      glDeleteTextures(1, &m_stateTexture[i]);
      m_stateTexture[i] = 0;
    }
  }

  m_particleTexSize = texSize;
  m_totalParticles = texSize * texSize;

  // Generate initial random particle positions within visible spawn bounds
  float sLonMin = m_spawnLonMin != 0 ? m_spawnLonMin : m_gridLonMin;
  float sLonMax = m_spawnLonMax != 0 ? m_spawnLonMax : m_gridLonMax;
  float sLatMin = m_spawnLatMin != 0 ? m_spawnLatMin : m_gridLatMin;
  float sLatMax = m_spawnLatMax != 0 ? m_spawnLatMax : m_gridLatMax;

  std::vector<float> initialState(m_totalParticles * 4);
  for (int i = 0; i < m_totalParticles; i++) {
    initialState[i * 4 + 0] = RandFloat(sLonMin, sLonMax);
    initialState[i * 4 + 1] = RandFloat(sLatMin, sLatMax);
    initialState[i * 4 + 2] = RandFloat(0.0f, 80.0f * 0.3f);
    initialState[i * 4 + 3] = 0.0f;
  }

  for (int i = 0; i < 2; i++) {
    glGenTextures(1, &m_stateTexture[i]);
    glBindTexture(GL_TEXTURE_2D, m_stateTexture[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, texSize, texSize, 0, GL_RGBA,
                 GL_FLOAT, initialState.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_stateFBO[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, m_stateFBO[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_stateTexture[i], 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      printf("GRIB GPU: State FBO %d incomplete!\n", i);
      return false;
    }
  }

  m_stateIndex = 0;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void DpGribGPUParticles::GenerateRandomSeedTexture(int texSize) {
  if (m_randomTex) {
    glDeleteTextures(1, &m_randomTex);
    m_randomTex = 0;
  }

  std::vector<float> randomData(texSize * texSize * 4);
  for (int i = 0; i < texSize * texSize; i++) {
    randomData[i * 4 + 0] = RandFloat(0.0f, 1.0f);
    randomData[i * 4 + 1] = RandFloat(0.0f, 1.0f);
    randomData[i * 4 + 2] = RandFloat(0.0f, 1.0f);
    randomData[i * 4 + 3] = RandFloat(0.0f, 1.0f);
  }

  glGenTextures(1, &m_randomTex);
  glBindTexture(GL_TEXTURE_2D, m_randomTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, texSize, texSize, 0, GL_RGBA,
               GL_FLOAT, randomData.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void DpGribGPUParticles::CreateWindTextures() {
  if (!m_windTexU) glGenTextures(1, &m_windTexU);
  if (!m_windTexV) glGenTextures(1, &m_windTexV);
}

// ============================================================================
// Wind Data Upload
// ============================================================================

void DpGribGPUParticles::UploadWindData(GribRecord *pGRX, GribRecord *pGRY) {
  int Ni = pGRX->getNi();
  int Nj = pGRX->getNj();
  bool flipJ = (pGRX->getDj() < 0);

  std::vector<float> uData(Ni * Nj);
  std::vector<float> vData(Ni * Nj);

  for (int j = 0; j < Nj; j++) {
    int srcJ = flipJ ? (Nj - 1 - j) : j;
    for (int i = 0; i < Ni; i++) {
      double valU = pGRX->getValue(i, srcJ);
      double valV = pGRY->getValue(i, srcJ);
      uData[j * Ni + i] = (valU == GRIB_NOTDEF) ? 0.0f : (float)valU;
      vData[j * Ni + i] = (valV == GRIB_NOTDEF) ? 0.0f : (float)valV;
    }
  }

  auto uploadTex = [&](GLuint tex, float *data) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, Ni, Nj, 0, GL_RED, GL_FLOAT,
                 data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };

  CreateWindTextures();
  uploadTex(m_windTexU, uData.data());
  uploadTex(m_windTexV, vData.data());

  m_windNi = Ni;
  m_windNj = Nj;

  // Cache grid metadata — when Dj < 0 we flipped rows, so origin is at latMin
  // getX(0) = Lo1, getY(0) = La1 (protected members accessed via public methods)
  if (flipJ) {
    m_gridLo1 = (float)pGRX->getX(0);
    m_gridLa1 = (float)pGRX->getLatMin();
    m_gridDi = (float)fabs(pGRX->getDi());
    m_gridDj = (float)fabs(pGRX->getDj());
  } else {
    m_gridLo1 = (float)pGRX->getX(0);
    m_gridLa1 = (float)pGRX->getY(0);
    m_gridDi = (float)fabs(pGRX->getDi());
    m_gridDj = (float)fabs(pGRX->getDj());
  }
  m_gridLonMin = (float)pGRX->getLonMin();
  m_gridLonMax = (float)pGRX->getLonMax();
  m_gridLatMin = (float)pGRX->getLatMin();
  m_gridLatMax = (float)pGRX->getLatMax();

  // Debug: verify wind data statistics
  float uMin = 1e9f, uMax = -1e9f, vMin = 1e9f, vMax = -1e9f;
  int nonZero = 0;
  for (int k = 0; k < Ni * Nj; k++) {
    if (uData[k] != 0.0f || vData[k] != 0.0f) nonZero++;
    if (uData[k] < uMin) uMin = uData[k];
    if (uData[k] > uMax) uMax = uData[k];
    if (vData[k] < vMin) vMin = vData[k];
    if (vData[k] > vMax) vMax = vData[k];
  }
  printf("GRIB GPU Wind: %dx%d flipJ=%d nonZero=%d/%d\n", Ni, Nj, flipJ,
         nonZero, Ni * Nj);
  printf("GRIB GPU Wind: U[%.2f..%.2f] V[%.2f..%.2f]\n", uMin, uMax, vMin,
         vMax);
  printf("GRIB GPU Grid: origin=(%.3f,%.3f) spacing=(%.4f,%.4f)\n", m_gridLo1,
         m_gridLa1, m_gridDi, m_gridDj);
  printf("GRIB GPU Grid: bounds lon[%.3f..%.3f] lat[%.3f..%.3f]\n",
         m_gridLonMin, m_gridLonMax, m_gridLatMin, m_gridLatMax);
}

// ============================================================================
// Viewport Change Handling
// ============================================================================

void DpGribGPUParticles::HandleViewportChange(PlugIn_ViewPort *vp) {
  bool sizeChanged =
      (vp->pix_width != m_lastPixWidth || vp->pix_height != m_lastPixHeight);
  bool scaleChanged = (vp->view_scale_ppm != m_lastScale);
  bool rotationChanged = (vp->rotation != m_lastRotation);

  if (sizeChanged) {
    CreateFBOs(vp->pix_width, vp->pix_height);
    m_lastRefScreenX = 0;
    m_lastRefScreenY = 0;
  } else if (scaleChanged || rotationChanged) {
    ClearTrailFBOs();
    m_lastRefScreenX = 0;
    m_lastRefScreenY = 0;
  }

  m_lastClat = vp->clat;
  m_lastClon = vp->clon;
  m_lastScale = vp->view_scale_ppm;
  m_lastPixWidth = vp->pix_width;
  m_lastPixHeight = vp->pix_height;
  m_lastRotation = vp->rotation;
}

void DpGribGPUParticles::ClearTrailFBOs() {
  GLint savedFBO;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);

  for (int i = 0; i < 2; i++) {
    if (m_trailFBO[i]) {
      glBindFramebuffer(GL_FRAMEBUFFER, m_trailFBO[i]);
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
}

// ============================================================================
// Resource Cleanup
// ============================================================================

void DpGribGPUParticles::DestroyFBOs() {
  for (int i = 0; i < 2; i++) {
    if (m_trailFBO[i]) {
      glDeleteFramebuffers(1, &m_trailFBO[i]);
      m_trailFBO[i] = 0;
    }
    if (m_trailTexture[i]) {
      glDeleteTextures(1, &m_trailTexture[i]);
      m_trailTexture[i] = 0;
    }
  }
  m_fboWidth = m_fboHeight = 0;
}

void DpGribGPUParticles::DestroyTextures() {
  for (int i = 0; i < 2; i++) {
    if (m_stateFBO[i]) {
      glDeleteFramebuffers(1, &m_stateFBO[i]);
      m_stateFBO[i] = 0;
    }
    if (m_stateTexture[i]) {
      glDeleteTextures(1, &m_stateTexture[i]);
      m_stateTexture[i] = 0;
    }
  }
  if (m_windTexU) {
    glDeleteTextures(1, &m_windTexU);
    m_windTexU = 0;
  }
  if (m_windTexV) {
    glDeleteTextures(1, &m_windTexV);
    m_windTexV = 0;
  }
  if (m_randomTex) {
    glDeleteTextures(1, &m_randomTex);
    m_randomTex = 0;
  }
}

// ============================================================================
// Reset
// ============================================================================

void DpGribGPUParticles::Reset() {
  ClearTrailFBOs();
  m_lastGRX = nullptr;
  m_lastGRY = nullptr;
  m_frameCount = 0;
}

// ============================================================================
// Update — called each frame from GribOverlayFactory
// ============================================================================

void DpGribGPUParticles::Update(GribRecord *pGRX, GribRecord *pGRY,
                                int settings,
                                GribOverlaySettings &overlaySettings,
                                PlugIn_ViewPort *vp) {
  if (!m_initialized || !pGRX || !pGRY) return;

  // Save GL state that Update modifies (FBO, texture bindings)
  GLint savedFBO;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
  GLint savedTex;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTex);

  // Upload wind data if GribRecords changed
  if (pGRX != m_lastGRX || pGRY != m_lastGRY) {
    UploadWindData(pGRX, pGRY);
    m_lastGRX = pGRX;
    m_lastGRY = pGRY;
  }

  // Compute zoom-adaptive speed factor
  if (vp->view_scale_ppm > 0) {
    m_speedFactor = 0.2f / (float)vp->view_scale_ppm;
    m_speedFactor = wxMin(m_speedFactor, 50000.0f);
    m_speedFactor = wxMax(m_speedFactor, 0.001f);
  }

  // Compute visible spawn bounds from viewport (approximate Mercator inverse)
  if (vp->view_scale_ppm > 0) {
    double halfWDeg =
        (vp->pix_width * 0.5) / (vp->view_scale_ppm * 111320.0);
    double halfHDeg =
        (vp->pix_height * 0.5) / (vp->view_scale_ppm * 110540.0);
    m_spawnLonMin = wxMax((float)(vp->clon - halfWDeg), m_gridLonMin);
    m_spawnLonMax = wxMin((float)(vp->clon + halfWDeg), m_gridLonMax);
    m_spawnLatMin = wxMax((float)(vp->clat - halfHDeg), m_gridLatMin);
    m_spawnLatMax = wxMin((float)(vp->clat + halfHDeg), m_gridLatMax);
  }

  // Zoom-adaptive particle count — fewer when zoomed in, more when zoomed out
  double density = overlaySettings.Settings[settings].m_dParticleDensity;
  int screenPixels = vp->pix_width * vp->pix_height;

  // Compute what fraction of the GRIB grid is visible
  double gridLonSpan = m_gridLonMax - m_gridLonMin;
  double gridLatSpan = m_gridLatMax - m_gridLatMin;
  double visLonSpan = m_spawnLonMax - m_spawnLonMin;
  double visLatSpan = m_spawnLatMax - m_spawnLatMin;
  double visibleFraction = 1.0;
  if (gridLonSpan > 0 && gridLatSpan > 0)
    visibleFraction = (visLonSpan * visLatSpan) / (gridLonSpan * gridLatSpan);
  visibleFraction = wxMin(visibleFraction, 1.0);
  visibleFraction = wxMax(visibleFraction, 0.001);

  // Base: ~1 particle per 3000 screen pixels, scaled by visible fraction
  // When zoomed out (fraction~1): full density
  // When zoomed in (fraction~0.01): far fewer particles
  double zoomScale = sqrt(visibleFraction);
  int targetCount = (int)(screenPixels / 3000.0 * density * zoomScale);
  targetCount = wxMin(targetCount, 3000);
  targetCount = wxMax(targetCount, 50);

  int side = (int)ceil(sqrt((double)targetCount));
  side = wxMax(side, 16);
  side = wxMin(side, 283);

  if (side != m_particleTexSize) {
    CreateParticleStateTextures(side);
    GenerateRandomSeedTexture(side);
    ClearTrailFBOs();
  }

  // Create trail FBOs on first call or resize
  if (m_fboWidth != vp->pix_width || m_fboHeight != vp->pix_height) {
    CreateFBOs(vp->pix_width, vp->pix_height);
  }

  HandleViewportChange(vp);

  // Restore GL state
  glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
  glBindTexture(GL_TEXTURE_2D, savedTex);

  // Don't run update if state textures aren't ready
  if (!m_stateTexture[0] || !m_stateFBO[0]) return;
  if (!m_windTexU || !m_windTexV) return;
}

// ============================================================================
// Render — called each frame after Update
// ============================================================================

void DpGribGPUParticles::Render(PlugIn_ViewPort *vp) {
  if (!m_initialized || !vp) return;
  if (!m_stateTexture[0] || !m_trailFBO[0]) return;
  if (!grib_particle_update_program || !grib_trail_decay_program ||
      !grib_particle_draw_program || !grib_trail_composite_program)
    return;

  // ===== SAVE GL STATE =====
  GLint savedVAO, savedProgram, savedFBO;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedVAO);
  glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);

  GLboolean savedBlend = glIsEnabled(GL_BLEND);
  GLint savedBlendSrc, savedBlendDst;
  glGetIntegerv(GL_BLEND_SRC_RGB, &savedBlendSrc);
  glGetIntegerv(GL_BLEND_DST_RGB, &savedBlendDst);

  GLint savedActiveTexture;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTexture);

  GLint savedTex[4];
  for (int i = 0; i < 4; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTex[i]);
  }

  GLint savedViewport[4];
  glGetIntegerv(GL_VIEWPORT, savedViewport);

  GLint savedArrayBuffer;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedArrayBuffer);

  GLboolean savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
  GLboolean savedScissorTest = glIsEnabled(GL_SCISSOR_TEST);

  // Disable depth/scissor for our FBO passes
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  static const int SUB_STEPS = 4;

  // ===== PASS 1: DECAY TRAIL FBO (once per frame) =====
  DecayTrails(vp);

  // ===== PASS 2: SUB-STEP LOOP (update + draw N times) =====
  for (int step = 0; step < SUB_STEPS; step++) {
    UpdateParticleState();
    DrawParticles(vp);
  }

  // Swap trail ping-pong
  m_trailIndex = 1 - m_trailIndex;

  // ===== PASS 3: COMPOSITE TO SCREEN =====
  glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
  glViewport(savedViewport[0], savedViewport[1], savedViewport[2],
             savedViewport[3]);
  CompositeToScreen(vp);

  // ===== RESTORE GL STATE =====
  glBindVertexArray(savedVAO);
  glUseProgram(savedProgram);
  glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
  glBindBuffer(GL_ARRAY_BUFFER, savedArrayBuffer);

  if (savedBlend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  glBlendFunc(savedBlendSrc, savedBlendDst);

  for (int i = 0; i < 4; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, savedTex[i]);
  }
  glActiveTexture(savedActiveTexture);

  glViewport(savedViewport[0], savedViewport[1], savedViewport[2],
             savedViewport[3]);

  if (savedDepthTest)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  if (savedScissorTest)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);

  m_frameCount++;
}

// ============================================================================
// Pass 1: Particle State Update
// ============================================================================

void DpGribGPUParticles::UpdateParticleState() {
  int writeIdx = 1 - m_stateIndex;

  glBindFramebuffer(GL_FRAMEBUFFER, m_stateFBO[writeIdx]);
  glViewport(0, 0, m_particleTexSize, m_particleTexSize);

  GLuint prog = grib_particle_update_program;
  glUseProgram(prog);

  // Bind state texture (read)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_stateTexture[m_stateIndex]);
  glUniform1i(glGetUniformLocation(prog, "uStateTex"), 0);

  // Bind wind textures
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_windTexU);
  glUniform1i(glGetUniformLocation(prog, "uWindU"), 1);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, m_windTexV);
  glUniform1i(glGetUniformLocation(prog, "uWindV"), 2);

  // Bind random texture
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, m_randomTex);
  glUniform1i(glGetUniformLocation(prog, "uRandomTex"), 3);

  // Grid uniforms
  glUniform2f(glGetUniformLocation(prog, "uGridOrigin"), m_gridLo1,
              m_gridLa1);
  glUniform2f(glGetUniformLocation(prog, "uGridSpacing"), m_gridDi, m_gridDj);
  glUniform2i(glGetUniformLocation(prog, "uGridSize"), m_windNi, m_windNj);
  glUniform4f(glGetUniformLocation(prog, "uGridBounds"), m_gridLonMin,
              m_gridLonMax, m_gridLatMin, m_gridLatMax);

  // Spawn bounds (visible area clamped to grid)
  glUniform4f(glGetUniformLocation(prog, "uSpawnBounds"), m_spawnLonMin,
              m_spawnLonMax, m_spawnLatMin, m_spawnLatMax);

  // Animation uniforms — speed divided by sub-steps for smooth trails
  static const int SUB_STEPS = 4;
  glUniform1f(glGetUniformLocation(prog, "uMaxAge"), 80.0f);
  glUniform1f(glGetUniformLocation(prog, "uDropRate"), 0.003f);
  glUniform1f(glGetUniformLocation(prog, "uDropRateBump"), 0.01f);
  glUniform1f(glGetUniformLocation(prog, "uSpeedFactor"),
              m_speedFactor / (float)SUB_STEPS);
  glUniform1f(glGetUniformLocation(prog, "uRandomSeed"),
              (float)(m_frameCount % 10000) * 0.001f);

  glDisable(GL_BLEND);

  glBindVertexArray(m_quadVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);

  m_stateIndex = writeIdx;
}

// ============================================================================
// Decay Trails — fade previous trail FBO (called once per frame)
// ============================================================================

void DpGribGPUParticles::DecayTrails(PlugIn_ViewPort *vp) {
  int writeIdx = 1 - m_trailIndex;

  glBindFramebuffer(GL_FRAMEBUFFER, m_trailFBO[writeIdx]);
  glViewport(0, 0, m_fboWidth, m_fboHeight);

  GLuint decayProg = grib_trail_decay_program;
  glUseProgram(decayProg);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_trailTexture[m_trailIndex]);
  glUniform1i(glGetUniformLocation(decayProg, "uTrailTex"), 0);
  glUniform1f(glGetUniformLocation(decayProg, "uDecay"), 0.96f);

  // Compute pan offset for smooth trail shifting
  float panOffsetX = 0.0f, panOffsetY = 0.0f;
  if (m_lastRefScreenX != 0.0 || m_lastRefScreenY != 0.0) {
    wxPoint refPt;
    GetCanvasPixLL(vp, &refPt, (m_gridLatMin + m_gridLatMax) * 0.5,
                   (m_gridLonMin + m_gridLonMax) * 0.5);
    double dx = refPt.x - m_lastRefScreenX;
    double dy = refPt.y - m_lastRefScreenY;
    if (fabs(dx) < m_fboWidth * 0.5 && fabs(dy) < m_fboHeight * 0.5) {
      panOffsetX = (float)(dx / m_fboWidth);
      panOffsetY = (float)(-dy / m_fboHeight);
    }
  }
  {
    wxPoint refPt;
    GetCanvasPixLL(vp, &refPt, (m_gridLatMin + m_gridLatMax) * 0.5,
                   (m_gridLonMin + m_gridLonMax) * 0.5);
    m_lastRefScreenX = refPt.x;
    m_lastRefScreenY = refPt.y;
  }

  glUniform2f(glGetUniformLocation(decayProg, "uPanOffset"), panOffsetX,
              panOffsetY);

  glDisable(GL_BLEND);

  glBindVertexArray(m_quadVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

// ============================================================================
// Draw Particles — line segments from previous to current position
// ============================================================================

void DpGribGPUParticles::DrawParticles(PlugIn_ViewPort *vp) {
  // Draw to the same trail FBO that DecayTrails wrote to
  int writeIdx = 1 - m_trailIndex;
  glBindFramebuffer(GL_FRAMEBUFFER, m_trailFBO[writeIdx]);
  glViewport(0, 0, m_fboWidth, m_fboHeight);

  GLuint drawProg = grib_particle_draw_program;
  glUseProgram(drawProg);

  // Current state texture (head position)
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_stateTexture[m_stateIndex]);
  glUniform1i(glGetUniformLocation(drawProg, "uStateTex"), 0);

  // Previous state texture (tail position)
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_stateTexture[1 - m_stateIndex]);
  glUniform1i(glGetUniformLocation(drawProg, "uPrevStateTex"), 1);

  glUniform1i(glGetUniformLocation(drawProg, "uParticleTexSize"),
              m_particleTexSize);

  // Viewport projection uniforms
  glUniform2f(glGetUniformLocation(drawProg, "uViewCenter"), (float)vp->clon,
              (float)vp->clat);
  glUniform1f(glGetUniformLocation(drawProg, "uViewScalePPM"),
              (float)vp->view_scale_ppm);
  glUniform2f(glGetUniformLocation(drawProg, "uViewSize"),
              (float)vp->pix_width, (float)vp->pix_height);
  glUniform1f(glGetUniformLocation(drawProg, "uRotation"),
              (float)vp->rotation);

  glUniform1f(glGetUniformLocation(drawProg, "uMaxSpeed"), 25.0f);
  glUniform1f(glGetUniformLocation(drawProg, "uMaxAge"), 80.0f);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive blending

  glEnable(GL_LINE_SMOOTH);
  glLineWidth(3.0f);

  // 2 vertices per particle (tail + head), drawn as GL_LINES
  glBindVertexArray(m_quadVAO);
  glDrawArrays(GL_LINES, 0, m_totalParticles * 2);
  glBindVertexArray(0);

  glDisable(GL_LINE_SMOOTH);
}

// ============================================================================
// Pass 3: Composite trail FBO onto screen
// ============================================================================

void DpGribGPUParticles::CompositeToScreen(PlugIn_ViewPort *vp) {
  GLuint prog = grib_trail_composite_program;
  glUseProgram(prog);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_trailTexture[m_trailIndex]);
  glUniform1i(glGetUniformLocation(prog, "uTrailTex"), 0);
  glUniform1f(glGetUniformLocation(prog, "uOpacity"), 1.0f);

  // Identity MVP — fullscreen quad in NDC maps 1:1 to screen
  float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  glUniformMatrix4fv(glGetUniformLocation(prog, "uMVP"), 1, GL_FALSE,
                     identity);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glBindVertexArray(m_quadVAO);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
}

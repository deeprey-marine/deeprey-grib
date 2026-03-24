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
      m_wavePerTex(0),
      m_windNi(0),
      m_windNj(0),
      m_randomTex(0),
      m_quadVAO(0),
      m_quadVBO(0),
      m_trailArrayTex(0),
      m_trailWriteIdx(0),
      m_validTrailCount(0),
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
      m_lastGRPer(nullptr),
      m_waveMode(false),
      m_spawnLonMin(0),
      m_spawnLonMax(0),
      m_spawnLatMin(0),
      m_spawnLatMax(0),
      m_frameCount(0),
      m_density(0),
      m_speedFactor(100.0f),
      m_subSteps(4) {
  m_stateTexture[0] = m_stateTexture[1] = 0;
  m_stateFBO[0] = m_stateFBO[1] = 0;
  m_trailTexture[0] = m_trailTexture[1] = 0;
  m_trailFBO[0] = m_trailFBO[1] = 0;
}

DpGribGPUParticles::~DpGribGPUParticles() {
  DestroyFBOs();
  DestroyTextures();
  DestroyTrailArrayTexture();
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
// Wave Period Data Upload
// ============================================================================

void DpGribGPUParticles::UploadWavePeriodData(GribRecord *pGRPer) {
  int Ni = pGRPer->getNi();
  int Nj = pGRPer->getNj();
  bool flipJ = (pGRPer->getDj() < 0);

  std::vector<float> perData(Ni * Nj);
  for (int j = 0; j < Nj; j++) {
    int srcJ = flipJ ? (Nj - 1 - j) : j;
    for (int i = 0; i < Ni; i++) {
      double val = pGRPer->getValue(i, srcJ);
      perData[j * Ni + i] = (val == GRIB_NOTDEF) ? 0.0f : (float)val;
    }
  }

  if (!m_wavePerTex) glGenTextures(1, &m_wavePerTex);
  glBindTexture(GL_TEXTURE_2D, m_wavePerTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, Ni, Nj, 0, GL_RED, GL_FLOAT,
               perData.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
// Trail History Array (wind mode ribbon rendering)
// ============================================================================

void DpGribGPUParticles::CreateTrailArrayTexture(int texSize) {
  DestroyTrailArrayTexture();

  glGenTextures(1, &m_trailArrayTex);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_trailArrayTex);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA32F, texSize, texSize, TRAIL_LEN,
               0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  m_trailWriteIdx = 0;
  m_validTrailCount = 0;
}

void DpGribGPUParticles::DestroyTrailArrayTexture() {
  if (m_trailArrayTex) {
    glDeleteTextures(1, &m_trailArrayTex);
    m_trailArrayTex = 0;
  }
  m_trailWriteIdx = 0;
  m_validTrailCount = 0;
}

void DpGribGPUParticles::CaptureTrailSnapshot() {
  if (!m_trailArrayTex || !m_stateFBO[m_stateIndex]) return;

  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_stateFBO[m_stateIndex]);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_trailArrayTex);
  glCopyTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, m_trailWriteIdx, 0, 0,
                      m_particleTexSize, m_particleTexSize);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

  m_trailWriteIdx = (m_trailWriteIdx + 1) % TRAIL_LEN;
  if (m_validTrailCount < TRAIL_LEN) m_validTrailCount++;
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
  DestroyTrailArrayTexture();
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
  if (m_wavePerTex) {
    glDeleteTextures(1, &m_wavePerTex);
    m_wavePerTex = 0;
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
  m_lastGRPer = nullptr;
  m_frameCount = 0;
  m_validTrailCount = 0;
  m_trailWriteIdx = 0;
}

// ============================================================================
// Update — called each frame from GribOverlayFactory
// ============================================================================

void DpGribGPUParticles::Update(GribRecord *pGRX, GribRecord *pGRY,
                                int settings,
                                GribOverlaySettings &overlaySettings,
                                PlugIn_ViewPort *vp,
                                GribRecord *pGRPeriod) {
  if (!m_initialized || !pGRX || !pGRY) return;

  // Save GL state that Update modifies (FBO, texture bindings)
  GLint savedFBO;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFBO);
  GLint savedTex;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTex);

  // Detect wave mode
  m_waveMode = (settings == GribOverlaySettings::WAVE);

  // Upload wind data if GribRecords changed
  if (pGRX != m_lastGRX || pGRY != m_lastGRY) {
    UploadWindData(pGRX, pGRY);
    m_lastGRX = pGRX;
    m_lastGRY = pGRY;
  }

  // Upload wave period texture when available and changed
  if (m_waveMode && pGRPeriod && pGRPeriod != m_lastGRPer) {
    UploadWavePeriodData(pGRPeriod);
    m_lastGRPer = pGRPeriod;
  }

  // Constant screen-space speed: baseFactor / scale
  // Screen pixels per frame = u(m/s) * baseFactor — same at every zoom level.
  if (vp->view_scale_ppm > 0) {
    float baseFactor = m_waveMode ? 0.03f : 0.2f;
    m_speedFactor = baseFactor / (float)vp->view_scale_ppm;
    m_speedFactor = wxMin(m_speedFactor, 400.0f);
    // Grid-relative cap: particles should not cross more than 2% of grid
    // per frame at strong wind (20 m/s). Prevents racing in small regions.
    float gridLonSpan = m_gridLonMax - m_gridLonMin;
    float gridCap = gridLonSpan * 0.02f * 111320.0f / 20.0f;
    m_speedFactor = wxMin(m_speedFactor, wxMax(gridCap, 5.0f));
    m_speedFactor = wxMax(m_speedFactor, 0.001f);
  }

  // Zoom-adaptive sub-steps: fewer when zoomed out (cheaper, still smooth)
  m_subSteps = (vp->view_scale_ppm > 5e-4) ? 4 : 2;

  // Compute visible spawn bounds from viewport (approximate Mercator inverse)
  if (vp->view_scale_ppm > 0) {
    double halfWDeg =
        (vp->pix_width * 0.5) / (vp->view_scale_ppm * 111320.0);
    double halfHDeg =
        (vp->pix_height * 0.5) / (vp->view_scale_ppm * 110540.0);

    // Normalize viewport center to grid longitude convention.
    // GFS grids use 0-360, but OpenCPN viewport uses -180 to +180.
    double clon = vp->clon;
    if (m_gridLonMax > 180.0 && clon < 0) clon += 360.0;

    m_spawnLonMin = wxMax((float)(clon - halfWDeg), m_gridLonMin);
    m_spawnLonMax = wxMin((float)(clon + halfWDeg), m_gridLonMax);
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

  // Base: ~1 particle per N screen pixels, scaled by visible fraction
  // Waves use fewer particles (1 per 10000px) than wind (1 per 3000px)
  double pixelsPerParticle = m_waveMode ? 800000.0 : 3000.0;
  int maxParticles = m_waveMode ? 80 : 3000;
  int minParticles = m_waveMode ? 16 : 50;
  double zoomScale = sqrt(visibleFraction);
  int targetCount = (int)(screenPixels / pixelsPerParticle * density * zoomScale);

  // For small grids: cap particles based on grid's screen-space area.
  // ~1 particle per 400 screen pixels of grid area.
  double gridScreenW = gridLonSpan * 111320.0 * vp->view_scale_ppm;
  double gridScreenH = gridLatSpan * 110540.0 * vp->view_scale_ppm;
  double gridScreenArea = gridScreenW * gridScreenH;
  int gridAreaMax = wxMax((int)(gridScreenArea / 400.0), 4);
  targetCount = wxMin(targetCount, gridAreaMax);

  targetCount = wxMin(targetCount, maxParticles);
  targetCount = wxMax(targetCount, wxMin(minParticles, gridAreaMax));

  int side = (int)ceil(sqrt((double)targetCount));
  side = wxMax(side, m_waveMode ? 4 : 4);
  side = wxMin(side, 283);

  if (side != m_particleTexSize) {
    CreateParticleStateTextures(side);
    GenerateRandomSeedTexture(side);
    ClearTrailFBOs();
    if (!m_waveMode) CreateTrailArrayTexture(side);
  }

  // Manage trail array texture based on mode transitions
  if (!m_waveMode && !m_trailArrayTex && m_particleTexSize > 0) {
    CreateTrailArrayTexture(m_particleTexSize);
  } else if (m_waveMode && m_trailArrayTex) {
    DestroyTrailArrayTexture();
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
  if (!m_stateTexture[0]) return;
  if (!grib_particle_update_program) return;

  // Mode-specific shader guards
  if (m_waveMode) {
    if (!m_trailFBO[0] || !grib_trail_decay_program ||
        !grib_particle_draw_program || !grib_trail_composite_program)
      return;
  } else {
    if (!grib_ribbon_draw_program) return;
  }

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

  GLint savedTex[5];
  for (int i = 0; i < 5; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTex[i]);
  }

  GLint savedTexArray;
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &savedTexArray);

  GLint savedViewport[4];
  glGetIntegerv(GL_VIEWPORT, savedViewport);

  GLint savedArrayBuffer;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedArrayBuffer);

  GLboolean savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
  GLboolean savedScissorTest = glIsEnabled(GL_SCISSOR_TEST);

  GLint savedReadFBO;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &savedReadFBO);

  // Disable depth/scissor for our FBO passes
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  if (m_waveMode) {
    // ===== WAVE MODE: existing FBO trail pipeline (unchanged) =====
    DecayTrails(vp);

    for (int step = 0; step < m_subSteps; step++) {
      UpdateParticleState();
      DrawParticles(vp);
    }

    m_trailIndex = 1 - m_trailIndex;

    glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2],
               savedViewport[3]);
    CompositeToScreen(vp);
  } else {
    // ===== WIND MODE: ribbon pipeline =====
    for (int step = 0; step < m_subSteps; step++) {
      UpdateParticleState();
    }

    CaptureTrailSnapshot();

    glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2],
               savedViewport[3]);
    DrawRibbons(vp);
  }

  // ===== RESTORE GL STATE =====
  glBindVertexArray(savedVAO);
  glUseProgram(savedProgram);
  glBindFramebuffer(GL_FRAMEBUFFER, savedFBO);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, savedReadFBO);
  glBindBuffer(GL_ARRAY_BUFFER, savedArrayBuffer);

  if (savedBlend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  glBlendFunc(savedBlendSrc, savedBlendDst);

  for (int i = 0; i < 5; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, savedTex[i]);
  }
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, savedTexArray);
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

  // Wave mode uniforms
  glUniform1i(glGetUniformLocation(prog, "uWaveMode"), m_waveMode ? 1 : 0);
  if (m_waveMode && m_wavePerTex) {
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_wavePerTex);
    glUniform1i(glGetUniformLocation(prog, "uWavePeriod"), 4);
  }

  // Animation uniforms — speed divided by sub-steps for smooth trails
  float maxAge = m_waveMode ? 80.0f : 80.0f;
  glUniform1f(glGetUniformLocation(prog, "uMaxAge"), maxAge);
  glUniform1f(glGetUniformLocation(prog, "uDropRate"), 0.003f);
  glUniform1f(glGetUniformLocation(prog, "uDropRateBump"), 0.01f);
  glUniform1f(glGetUniformLocation(prog, "uSpeedFactor"),
              m_speedFactor / (float)m_subSteps);
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
  float decay = m_waveMode ? 0.92f : 0.96f;
  glUniform1f(glGetUniformLocation(decayProg, "uDecay"), decay);

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

  // Pre-computed Mercator center and rotation (avoid per-vertex trig)
  float cx = (float)(vp->clon * M_PI / 180.0 * 6378137.0);
  float cLatRad = (float)(vp->clat * M_PI / 180.0);
  float cy = (float)(log(tan(cLatRad * 0.5 + M_PI / 4.0)) * 6378137.0);
  glUniform2f(glGetUniformLocation(drawProg, "uViewCenterMerc"), cx, cy);
  glUniform2f(glGetUniformLocation(drawProg, "uRotCS"),
              cosf((float)vp->rotation), sinf((float)vp->rotation));

  glUniform1f(glGetUniformLocation(drawProg, "uViewScalePPM"),
              (float)vp->view_scale_ppm);
  glUniform2f(glGetUniformLocation(drawProg, "uViewSize"),
              (float)vp->pix_width, (float)vp->pix_height);
  glUniform1f(glGetUniformLocation(drawProg, "uRotation"),
              (float)vp->rotation);

  float maxAge = m_waveMode ? 50.0f : 80.0f;
  glUniform1f(glGetUniformLocation(drawProg, "uMaxSpeed"), 25.0f);
  glUniform1f(glGetUniformLocation(drawProg, "uMaxAge"), maxAge);

  // Wave mode uniform and textures
  glUniform1i(glGetUniformLocation(drawProg, "uWaveMode"), m_waveMode ? 1 : 0);
  if (m_waveMode) {
    // Bind wind textures for direction lookup in draw shader
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_windTexU);
    glUniform1i(glGetUniformLocation(drawProg, "uWindU"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_windTexV);
    glUniform1i(glGetUniformLocation(drawProg, "uWindV"), 3);

    if (m_wavePerTex) {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, m_wavePerTex);
      glUniform1i(glGetUniformLocation(drawProg, "uWavePeriod"), 4);
    }

    // Grid uniforms for direction texture lookup
    glUniform2f(glGetUniformLocation(drawProg, "uGridOrigin"), m_gridLo1,
                m_gridLa1);
    glUniform2f(glGetUniformLocation(drawProg, "uGridSpacing"), m_gridDi,
                m_gridDj);
    glUniform2i(glGetUniformLocation(drawProg, "uGridSize"), m_windNi,
                m_windNj);

    // Zoom-adaptive crest scale: smaller when zoomed out, larger when zoomed in
    // view_scale_ppm ~1e-5 zoomed out, ~1e-3 zoomed in
    float crestScale = (float)(sqrt(vp->view_scale_ppm) * 300.0);
    crestScale = wxMin(crestScale, 1.0f);
    crestScale = wxMax(crestScale, 0.3f);
    glUniform1f(glGetUniformLocation(drawProg, "uWaveCrestScale"), crestScale);
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive blending

  glEnable(GL_LINE_SMOOTH);
  glLineWidth(m_waveMode ? 1.5f : 3.0f);

  // GL_LINES: 2 vertices per particle
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

// ============================================================================
// Wind Mode: Draw comet-tail ribbons from trail history
// ============================================================================

void DpGribGPUParticles::DrawRibbons(PlugIn_ViewPort *vp) {
  if (m_validTrailCount < 2 || !m_trailArrayTex) return;

  GLuint prog = grib_ribbon_draw_program;
  glUseProgram(prog);

  // Bind trail array texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, m_trailArrayTex);
  glUniform1i(glGetUniformLocation(prog, "uTrailArray"), 0);

  // Ring buffer state
  glUniform1i(glGetUniformLocation(prog, "uTrailWriteIdx"), m_trailWriteIdx);
  glUniform1i(glGetUniformLocation(prog, "uValidTrailCount"),
              m_validTrailCount);
  glUniform1i(glGetUniformLocation(prog, "uParticleTexSize"),
              m_particleTexSize);

  // Pre-computed Mercator center and rotation (avoid per-vertex trig)
  float cx = (float)(vp->clon * M_PI / 180.0 * 6378137.0);
  float cLatRad = (float)(vp->clat * M_PI / 180.0);
  float cy = (float)(log(tan(cLatRad * 0.5 + M_PI / 4.0)) * 6378137.0);
  glUniform2f(glGetUniformLocation(prog, "uViewCenterMerc"), cx, cy);
  glUniform2f(glGetUniformLocation(prog, "uRotCS"),
              cosf((float)vp->rotation), sinf((float)vp->rotation));

  glUniform1f(glGetUniformLocation(prog, "uViewScalePPM"),
              (float)vp->view_scale_ppm);
  glUniform2f(glGetUniformLocation(prog, "uViewSize"), (float)vp->pix_width,
              (float)vp->pix_height);

  // Zoom-adaptive stride: fewer ribbon segments when zoomed out
  int stride = 1;
  if (vp->view_scale_ppm < 1e-4)
    stride = 3;
  else if (vp->view_scale_ppm < 5e-4)
    stride = 2;

  int effectiveSegments = (TRAIL_LEN - 1) / stride;
  int vertsPerParticle = effectiveSegments * 6;
  glUniform1i(glGetUniformLocation(prog, "uStride"), stride);
  glUniform1i(glGetUniformLocation(prog, "uVertsPerParticle"),
              vertsPerParticle);

  // Additive blending (premultiplied alpha output from fragment shader)
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);

  int vertexCount = m_totalParticles * vertsPerParticle;
  glBindVertexArray(m_quadVAO);
  glDrawArrays(GL_TRIANGLES, 0, vertexCount);
  glBindVertexArray(0);
}

/***************************************************************************
 *   Copyright (C) 2024 by Deeprey Research Ltd                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#ifndef __DPGRIBGPUPARTICLES_H__
#define __DPGRIBGPUPARTICLES_H__

#include "pi_gl.h"
#include "grib_shaders.h"

class GribRecord;
class GribOverlaySettings;
struct PlugIn_ViewPort;

class DpGribGPUParticles {
public:
  static const int TRAIL_LEN = 20;
  static const int VERTS_PER_PARTICLE = (TRAIL_LEN - 1) * 6;  // 90 max (stride reduces at runtime)

  DpGribGPUParticles();
  ~DpGribGPUParticles();

  bool Initialize(const DpGribGLCapabilities &caps);
  void Update(GribRecord *pGRX, GribRecord *pGRY, int settings,
              GribOverlaySettings &overlaySettings, PlugIn_ViewPort *vp,
              GribRecord *pGRPeriod = nullptr);
  void Render(PlugIn_ViewPort *vp);
  void Reset();

private:
  // Initialization helpers
  bool CreateFBOs(int width, int height);
  bool CreateParticleStateTextures(int texSize);
  void CreateWindTextures();
  void CreateFullscreenQuadVAO();
  void GenerateRandomSeedTexture(int texSize);

  // Per-frame GPU passes
  void UpdateParticleState();
  void DecayTrails(PlugIn_ViewPort *vp);
  void DrawParticles(PlugIn_ViewPort *vp);
  void CompositeToScreen(PlugIn_ViewPort *vp);

  // Data management
  void UploadWindData(GribRecord *pGRX, GribRecord *pGRY);
  void UploadWavePeriodData(GribRecord *pGRPer);
  void HandleViewportChange(PlugIn_ViewPort *vp);
  void ClearTrailFBOs();

  // Trail history management (wind mode ribbons)
  void CreateTrailArrayTexture(int texSize);
  void DestroyTrailArrayTexture();
  void CaptureTrailSnapshot();
  void DrawRibbons(PlugIn_ViewPort *vp);

  // Resource cleanup
  void DestroyFBOs();
  void DestroyTextures();

  bool m_initialized;
  int m_fboWidth, m_fboHeight;
  int m_particleTexSize;
  int m_totalParticles;

  // Ping-pong particle state (RGBA32F: lon, lat, age, speed)
  GLuint m_stateTexture[2];
  GLuint m_stateFBO[2];
  int m_stateIndex;

  // Ping-pong trail accumulation (RGBA8, viewport-sized)
  GLuint m_trailTexture[2];
  GLuint m_trailFBO[2];
  int m_trailIndex;

  // Wind field textures (R32F)
  GLuint m_windTexU, m_windTexV;
  GLuint m_wavePerTex;  // R32F texture for wave period data
  int m_windNi, m_windNj;

  // Random seed texture (RGBA32F)
  GLuint m_randomTex;

  // Fullscreen quad geometry
  GLuint m_quadVAO, m_quadVBO;

  // Trail history ring buffer (wind mode ribbon rendering)
  GLuint m_trailArrayTex;    // GL_TEXTURE_2D_ARRAY, TRAIL_LEN layers, RGBA32F
  int m_trailWriteIdx;        // ring buffer write pointer 0..TRAIL_LEN-1
  int m_validTrailCount;      // frames captured since reset (caps at TRAIL_LEN)

  // Grid metadata for shader uniforms
  float m_gridLo1, m_gridLa1;
  float m_gridDi, m_gridDj;
  float m_gridLonMin, m_gridLonMax;
  float m_gridLatMin, m_gridLatMax;

  // Viewport cache for change detection
  double m_lastClat, m_lastClon, m_lastScale;
  int m_lastPixWidth, m_lastPixHeight;
  double m_lastRotation;

  // Pan tracking for trail shift
  double m_lastRefScreenX, m_lastRefScreenY;

  // GribRecord change detection
  GribRecord *m_lastGRX;
  GribRecord *m_lastGRY;
  GribRecord *m_lastGRPer;

  // Wave mode state
  bool m_waveMode;

  // Zoom-adaptive spawn bounds (visible area clamped to grid)
  float m_spawnLonMin, m_spawnLonMax;
  float m_spawnLatMin, m_spawnLatMax;

  // Animation state
  int m_frameCount;
  float m_density;
  float m_speedFactor;  // zoom-adaptive, computed from view_scale_ppm
  int m_subSteps;       // zoom-adaptive: 4 zoomed in, 2 zoomed out
};

#endif  // __DPGRIBGPUPARTICLES_H__

/***************************************************************************
 *   Copyright (C) 2014 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************/
/**
 * \file
 * GRIB Data Visualization and Rendering Factory.
 *
 * Provides comprehensive visualization capabilities for GRIB weather data in
 * OpenCPN, including:
 * - Wind barbs and particle animations
 * - Pressure isobars and directional arrows
 * - Color-coded overlay maps for various parameters
 * - Numerical data displays and labels
 *
 * The factory manages both OpenGL and bitmap-based rendering paths, handles
 * resource allocation, and provides efficient caching of rendered elements.
 * It serves as the central hub for converting raw GRIB data into meaningful
 * visual representations for mariners.
 */
#ifndef _GRIBOVERLAYFACTORY_H_
#define _GRIBOVERLAYFACTORY_H_

#include <map>

#include <wx/geometry.h>

#include "pi_gl.h"
#include "grib_shaders.h"
#include "DpGribGPUParticles.h"

#include "pi_ocpndc.h"
#include "pi_TexFont.h"

/**
 * Container for rendered GRIB data visualizations in texture or bitmap form.
 *
 * This class manages the rendered representation of GRIB weather data,
 * supporting both OpenGL texture-based rendering and bitmap-based rendering. It
 * handles resource allocation and cleanup for both rendering paths.
 */
class GribOverlay {
public:
  GribOverlay(void) {
    m_iTexture = 0;
    m_pDCBitmap = nullptr, m_pRGBA = nullptr;
  }

  ~GribOverlay(void) {
#ifdef ocpnUSE_GL
    if (m_iTexture) {
      glDeleteTextures(1, &m_iTexture);
    }
#endif
    delete m_pDCBitmap, delete[] m_pRGBA;
  }

  unsigned int m_iTexture, m_iTextureDim[2]; /* opengl mode */
  unsigned int m_iTexDataDim[2];

  wxBitmap *m_pDCBitmap; /* dc mode */
  unsigned char *m_pRGBA;

  int m_width;
  int m_height;

  double m_dwidth, m_dheight;
};


#define MAX_PARTICLE_HISTORY 8
#include <vector>
#include <list>
/**
 * Individual particle for wind/current animation.
 *
 * Represents a single particle in the animation system with position history
 * and rendering attributes.
 */
struct Particle {
  /** Duration this particle should exist in animation cycles. */
  int m_Duration;

  // history is a ringbuffer.. because so many particles are
  // used, it is a slight optimization over std::list
  int m_HistoryPos, m_HistorySize, m_Run;
  struct ParticleNode {
    float m_Pos[2];
    float m_Screen[2];
    wxUint8 m_Color[3];
  } m_History[MAX_PARTICLE_HISTORY];
};

/**
 * Manager for particle animation system.
 *
 * Handles collections of particles and their rendering data arrays.
 */
struct ParticleMap {
public:
  ParticleMap(int settings)
      : m_Setting(settings),
        history_size(0),
        array_size(0),
        color_array(nullptr),
        vertex_array(nullptr),
        color_float_array(nullptr) {
    // XXX should be done in default PlugIn_ViewPort CTOR
    last_viewport.bValid = false;
  }

  ~ParticleMap() {
    delete[] color_array;
    delete[] vertex_array;
    delete[] color_float_array;
  }

  std::vector<Particle> m_Particles;

  // particles are rebuilt whenever any of these fields change
  time_t m_Reference_Time;
  int m_Setting;
  int history_size;

  unsigned int array_size;
  unsigned char *color_array;
  float *vertex_array;
  float *color_float_array;

  PlugIn_ViewPort last_viewport;
};

class LineBuffer {
public:
  LineBuffer() {
    count = 0;
    lines = nullptr;
  }
  ~LineBuffer() { delete[] lines; }

  void pushLine(float x0, float y0, float x1, float y1);
  void pushPetiteBarbule(int b, int l);
  void pushGrandeBarbule(int b, int l);
  void pushTriangle(int b, int l);
  void Finalize();

  int count;
  float *lines;

private:
  std::list<float> buffer;
};

class GRIBUICtrlBar;
class GribRecord;
class GribTimelineRecordSet;

/**
 * Factory class for creating and managing GRIB data visualizations.
 *
 * This class is responsible for rendering all GRIB weather data visualizations
 * in OpenCPN. It handles multiple visualization types including wind barbs,
 * isobars, particles, directional arrows, and numeric overlays.
 */
class GRIBOverlayFactory : public wxEvtHandler {
public:
  GRIBOverlayFactory(GRIBUICtrlBar &dlg);
  ~GRIBOverlayFactory();

  void SetSettings(bool hiDefGraphics, bool GradualColors,
                   bool BarbedArrowHead = true) {
    m_hiDefGraphics = hiDefGraphics;
    m_bGradualColors = GradualColors;
    m_bDrawBarbedArrowHead = BarbedArrowHead;
    ClearCachedData();
  }
  void SetMessageFont();
  void SetMessage(wxString message) { m_Message = message; }
  void SetParentSize(int w, int h) {
    m_ParentSize.SetWidth(w);
    m_ParentSize.SetHeight(h);
  }

  void SetGribTimelineRecordSet(GribTimelineRecordSet *pGribTimelineRecordSet1);
  // Per-canvas timeline (dual-chart mode): each canvas can display a different
  // time step from the same loaded GRIB file. Passing nullptr makes that canvas
  // fall back to the shared/global set. NON-OWNING (the control bar owns the
  // sets, mirroring the single-set ownership) — the caller must keep them alive.
  void SetGribTimelineRecordSet(GribTimelineRecordSet *set, int canvasIndex);
  // Point the factory's active timeline + overlay-texture cache at one canvas for
  // the render/query that follows. The render is single-threaded, so swapping the
  // active pointers per canvas is safe.
  void SelectCanvasContext(int canvasIndex);
  bool RenderGribOverlay(wxDC &dc, PlugIn_ViewPort *vp, int canvasIndex = 0);
  bool RenderGLGribOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp,
                           int canvasIndex = 0);
  // Draws only the screen-space colour legend. Called in a separate, higher
  // priority pass (OVERLAY_OVER_UI) so the legend sits on top of chart graphics.
  bool RenderGLColorLegend(wxGLContext *pcontext, PlugIn_ViewPort *vp,
                           int canvasIndex = 0);

  void Reset();
  void ClearCachedData(void);
  void ClearCachedLabel(void) { m_labelCache.clear(); }
  void ClearParticles() {
    // Per-canvas particle state: clear every canvas's CPU map and reset every
    // canvas's GPU instance so a setting/time change restarts cleanly on all.
    for (auto &kv : m_particleMapByCanvas) delete kv.second;
    m_particleMapByCanvas.clear();
    for (auto &kv : m_gpuParticlesByCanvas)
      if (kv.second) kv.second->Reset();
    m_gpuAnimTimer.Stop();
  }

  GribTimelineRecordSet *m_pGribTimelineRecordSet;

  void DrawMessageZoomOut(PlugIn_ViewPort *vp);
  void GetGraphicColor(int settings, double val, unsigned char &r,
                       unsigned char &g, unsigned char &b);
  wxColour GetGraphicColor(int settings, double val);

  // Position the color legend for vertical stacking (set by deeprey-gui). See
  // DpGribAPI::SetLegendLayout. Defaults reproduce the standalone single bar.
  void SetLegendLayout(int slot, int stackCount, bool drawInfoRow) {
    m_legendSlot = slot;
    m_legendStackCount = stackCount;
    m_legendDrawInfoRow = drawInfoRow;
  }

  // True iff a colored overlay legend would actually be drawn this frame (same
  // active-overlay + non-degenerate-range gate RenderColorLegend uses). The
  // single source of truth for "weather legend is on screen", so deeprey-gui's
  // arbiter and course-button logic match what is really rendered.
  bool HasActiveColorOverlay();
  // Per-canvas variant: selects the canvas's timeline first (dual-chart mode).
  bool HasActiveColorOverlay(int canvasIndex);

  wxSize m_ParentSize;

  pi_ocpnDC *m_oDC;

private:
  void InitColorsTable();

  void SettingsIdToGribId(int i, int &idx, int &idy, bool &polar);
  bool DoRenderGribOverlay(PlugIn_ViewPort *vp, int canvasIndex = 0);
  /**
   * Renders wind or current barbed arrows on the chart.
   *
   * This function draws barbed arrows representing wind or current directions
   * and magnitudes. The barbs change appearance based on speed (more barbs for
   * higher speed). The arrows can be drawn using fixed spacing (grid) or
   * minimum spacing modes.
   *
   * @param settings The settings index identifying the data type (WIND,
   * CURRENT, etc.)
   * @param pGR Array of GribRecord pointers containing the data
   * @param vp Current viewport for rendering
   */
  void RenderGribBarbedArrows(int config, GribRecord **pGR,
                              PlugIn_ViewPort *vp);
  /**
   * Renders isobars (lines of equal value) for pressure or other scalar fields.
   *
   * This function draws isobar lines at specific intervals defined in the
   * settings. It also handles label placement along the isobars. For pressure,
   * the function supports different unit conversions. The implementation caches
   * isobar calculations to improve performance.
   *
   * @param settings The settings index identifying the data type (PRESSURE,
   * etc.)
   * @param pGR Array of GribRecord pointers containing the data
   * @param pIsobarArray Array of cached isobar objects for reuse
   * @param vp Current viewport for rendering
   */
  void RenderGribIsobar(int config, GribRecord **pGR,
                        wxArrayPtrVoid **pIsobarArray, PlugIn_ViewPort *vp);
  /**
   * Renders direction arrows for vector fields like wind or current.
   *
   * This function draws arrows showing flow direction for vector data. The
   * arrows can be single, double, or width-varied based on settings. Supports
   * both fixed spacing (grid) mode and minimum spacing mode.
   *
   * @param settings The settings index identifying the data type (WIND,
   * CURRENT, etc.)
   * @param pGR Array of GribRecord pointers containing the data
   * @param vp Current viewport for rendering
   */
  void RenderGribDirectionArrows(int config, GribRecord **pGR,
                                 PlugIn_ViewPort *vp);
  /**
   * Renders color-coded overlay maps showing data distribution.
   *
   * This function creates and draws bitmap or OpenGL texture overlays showing
   * geographic distribution of data using color gradients. It handles both
   * scalar and vector magnitude fields, and manages appropriate color mapping
   * based on data range. Includes transparency support for certain data types.
   *
   * @param settings The settings index identifying the data type
   * @param pGR Array of GribRecord pointers containing the data
   * @param vp Current viewport for rendering
   */
  void RenderGribOverlayMap(int config, GribRecord **pGR, PlugIn_ViewPort *vp);
  /**
   * Renders numeric values at fixed or minimum-spaced grid points.
   *
   * This function displays actual data values at locations across the chart.
   * Values are drawn with background colors matching the data scale.
   * Supports both fixed spacing (grid) mode and minimum spacing mode.
   * Grid placement is aligned to geographic coordinates to maintain proper
   * positioning during panning.
   *
   * @param settings The settings index identifying the data type
   * @param pGR Array of GribRecord pointers containing the data
   * @param vp Current viewport for rendering
   */
  void RenderGribNumbers(int config, GribRecord **pGR, PlugIn_ViewPort *vp);
  /**
   * Renders animated particles showing flow patterns.
   *
   * This function creates and updates flowing particles that visualize vector
   * fields like wind or current. Particles have position history, move based on
   * field strength and direction, and are color-coded by magnitude. The
   * implementation manages particle lifecycle, trajectory calculation, and
   * efficient rendering for potentially thousands of particles.
   *
   * @param settings The settings index identifying the data type (WIND,
   * CURRENT)
   * @param pGR Array of GribRecord pointers containing the data
   * @param vp Current viewport for rendering
   */
  void RenderGribParticles(int settings, GribRecord **pGR, PlugIn_ViewPort *vp,
                           int canvasIndex = 0);
  void DrawLineBuffer(LineBuffer &buffer);
  void OnParticleTimer(wxTimerEvent &event);
  void OnGPUAnimTimer(wxTimerEvent &event);
  void ScheduleGPUParticleRefresh();

  wxString GetRefString(GribRecord *rec, int map);
  void DrawMessageWindow(wxString msg, int x, int y, wxFont *mfont);

  void DrawProjectedPosition(int x, int y);

  // Draws the on-screen color legend for the single active overlay map, using
  // the shared DpColorBar widget. GL path only; no-op when no overlay is shown.
  void RenderColorLegend(PlugIn_ViewPort *vp);

  // Computes the actual displayed-data value range (in display units) for the
  // active overlay, so the legend keys the colours really on the map rather than
  // the fixed palette bounds. Raw min/max are cached and recomputed only when the
  // overlay / timeline / altitude changes; calibration (units) is applied per
  // call. Returns false if there is no usable data or the range is degenerate.
  bool GetActiveDataRange(int settings, double &dispMin, double &dispMax);

  int m_legendKeySettings = -1;
  const void *m_legendKeySet = nullptr;
  int m_legendKeyAlt = -1;
  double m_legendRawMin = 0.0;
  double m_legendRawMax = 0.0;
  bool m_legendHasData = false;

  // Legend fonts matching deepview (normal 11pt + bold 12pt, SWISS family).
  TexFont m_TexFontLegend, m_TexFontLegendBold;
  // Nav-mode icon textures (0=north, 1=course, 2=head up); lazily GL-loaded.
  unsigned int m_navTex[3] = {0, 0, 0};
  bool m_navTexTried = false;
  int m_legendScreenDpi = 0;

  // Vertical stacking, assigned by deeprey-gui via SetLegendLayout. Defaults give
  // the standalone single-bar layout (slot 0, alone, owns the shared info row).
  int m_legendSlot = 0;
  int m_legendStackCount = 1;
  bool m_legendDrawInfoRow = true;

  void drawDoubleArrow(int x, int y, double ang, wxColour arrowColor,
                       int arrowWidth, int arrowSizeIdx, double scale);
  void drawSingleArrow(int x, int y, double ang, wxColour arrowColor,
                       int arrowWidth, int arrowSizeIdx, double scale);
  void drawWindArrowWithBarbs(int settings, int x, int y, double vkn,
                              double ang, bool south, wxColour arrowColor,
                              double rotate_angle);
  void drawLineBuffer(LineBuffer &buffer, int x, int y, double ang,
                      double scale, bool south = false, bool head = true);

  void DrawNumbers(wxPoint p, double value, int settings, wxColour back_color);
  void FillGrid(GribRecord *pGR);

  wxString getLabelString(double value, int settings);
  wxImage &getLabel(double value, int settings, wxColour back_colour);

#ifdef ocpnUSE_GL
  void DrawGLTexture(GribOverlay *pGO, GribRecord *pGR, PlugIn_ViewPort *vp);
  void GetCalibratedGraphicColor(int settings, double val_in,
                                 unsigned char *data);
  bool CreateGribGLTexture(GribOverlay *pGO, int config, GribRecord *pGR);
  void DrawSingleGLTexture(GribOverlay *pGO, GribRecord *pGR, double uv[],
                           double x, double y, double xs, double ys);

  // GPU particle system init (GL 3.3+)
  void InitGPURenderer();
#endif
  wxImage CreateGribImage(int config, GribRecord *pGR, PlugIn_ViewPort *vp,
                          int grib_pixel_size, const wxPoint &porg);

  double m_last_vp_scale;

  // Overlay-map color textures, cached per canvas (dual-chart mode) so two
  // canvases at different times/layers don't reuse each other's texture.
  // m_pOverlay aliases the active canvas's row (set by SelectCanvasContext).
  GribOverlay *m_overlayByCanvas[2][GribOverlaySettings::SETTINGS_COUNT];
  GribOverlay **m_pOverlay;  // = m_overlayByCanvas[m_activeCanvas]
  int m_activeCanvas = 0;

  // Per-canvas timeline (non-owning) + shared/global fallback (non-owning).
  GribTimelineRecordSet *m_timelineByCanvas[2] = {nullptr, nullptr};
  GribTimelineRecordSet *m_timelineGlobal = nullptr;
  void ClearCanvasOverlay(int canvasIndex);  // free one canvas's overlay textures

  // GPU particle rendering state (GL 3.3+)
  bool m_bUseGPURenderer;
  bool m_bGPUInitialized;
  DpGribGLCapabilities m_glCaps;
  // One GPU particle renderer per chart canvas (keyed by canvasIndex). Created
  // lazily in RenderGribParticles so single-canvas use allocates only index 0.
  // Each instance owns its own viewport-sized FBOs / pan-tracking caches, so two
  // canvases at different pan/zoom no longer corrupt each other's animation.
  std::map<int, DpGribGPUParticles *> m_gpuParticlesByCanvas;

  wxString m_Message;
  wxString m_Message_Hiden;

  wxDC *m_pdc;
#if wxUSE_GRAPHICS_CONTEXT
  wxGraphicsContext *m_gdc;
#endif

  wxFont *m_Font_Message;

  bool m_hiDefGraphics;
  bool m_bGradualColors;
  bool m_bDrawBarbedArrowHead;

  std::map<double, wxImage> m_labelCache;

  TexFont m_TexFontMessage, m_TexFontNumbers;

  GRIBUICtrlBar &m_dlg;
  GribOverlaySettings &m_Settings;

  // One CPU particle map per chart canvas (keyed by canvasIndex). Each holds its
  // own last_viewport + cached screen coords; created lazily in RenderGribParticles.
  std::map<int, ParticleMap *> m_particleMapByCanvas;
  wxTimer m_tParticleTimer;
  wxTimer m_gpuAnimTimer;  // dedicated timer for GPU particle animation
  bool m_bUpdateParticles;
  // Target wall-time per GPU animation frame (config key GPUAnimTargetMs,
  // ms) and the monotonic-ms stamp of the last timer-driven refresh (-1 =
  // none); see ScheduleGPUParticleRefresh for the scheduling policy.
  int m_gpuAnimTargetMs;
  long long m_gpuRefreshStampMs;

  LineBuffer m_WindArrowCache[14];
  LineBuffer m_SingleArrow[2], m_DoubleArrow[2];

  double m_pixelMM;
  int windArrowSize;
};

#endif
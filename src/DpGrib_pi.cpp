/***************************************************************************
 *   Copyright (C) 2010 by David S. Register                               *
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
 * \implements \ref DpGrib_pi.h
 */
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"

#include "pi_gl.h"

#ifdef ocpnUSE_GL
#include <wx/glcanvas.h>
#endif
#endif  // precompiled headers

#include <wx/fileconf.h>
#include <wx/stdpaths.h>

#include "DpGrib_pi.h"
#include "DpUnitManager.h"

#ifdef __WXQT__
#include "qdebug.h"
#endif

double g_ContentScaleFactor;

// the class factories, used to create and destroy instances of the PlugIn

extern "C" DECL_EXP opencpn_plugin *create_pi(void *ppimgr) {
  return new DpGrib_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin *p) { delete p; }

extern int m_DialogStyle;

DpGrib_pi *g_pi;
bool g_bpause;

//---------------------------------------------------------------------------------------------------------
//
//    Grib PlugIn Implementation
//
//---------------------------------------------------------------------------------------------------------

#include "icons.h"

//---------------------------------------------------------------------------------------------------------
//
//          PlugIn initialization and de-init
//
//---------------------------------------------------------------------------------------------------------

DpGrib_pi::DpGrib_pi(void *ppimgr) : opencpn_plugin_116(ppimgr) {
  // Create the PlugIn icons
  initialize_images();

  wxString shareLocn = GetPluginDataDir("deeprey_grib_pi") +
                       wxFileName::GetPathSeparator() + _T("data") +
                       wxFileName::GetPathSeparator();
  wxImage panelIcon(shareLocn + _T("grib_panel_icon.png"));
  if (panelIcon.IsOk())
    m_panelBitmap = wxBitmap(panelIcon);
  else
    wxLogMessage(_T("    GRIB panel icon NOT loaded"));

  m_pLastTimelineSet = nullptr;
  m_bShowGrib = false;
  m_GUIScaleFactor = -1.;
  g_pi = this;

  // Initialize API
    m_gribAPI = nullptr;
}

DpGrib_pi::~DpGrib_pi(void) {
  delete _img_grib_pi;
  delete _img_grib;
  delete m_pLastTimelineSet;
}

int DpGrib_pi::Init(void) {
  AddLocaleCatalog(_T("opencpn-DpGrib_pi"));

  // Set some default private member parameters
  m_CtrlBarxy = wxPoint(0, 0);
  m_CursorDataxy = wxPoint(0, 0);

  m_pGribCtrlBar = nullptr;
  m_pGRIBOverlayFactory = nullptr;

  ::wxDisplaySize(&m_display_width, &m_display_height);

  m_DialogStyleChanged = false;

  //    Get a pointer to the opencpn configuration object
  m_pconfig = GetOCPNConfigObject();

  //    And load the configuration items
  LoadConfig();

  // Initialize the unit manager with OpenCPN config
  DpUnitManager::Instance().Init(m_pconfig);

  // Get a pointer to the opencpn display canvas, to use as a parent for the
  // GRIB dialog
  m_parent_window = GetOCPNCanvasWindow();

  g_ContentScaleFactor = m_parent_window->GetContentScaleFactor();

  //      int m_height = GetChartbarHeight();
  //    This PlugIn needs a CtrlBar icon, so request its insertion if enabled
  //    locally
  wxString shareLocn = *GetpSharedDataLocation() + _T("plugins") +
                       wxFileName::GetPathSeparator() + _T("DpGrib_pi") +
                       wxFileName::GetPathSeparator() + _T("data") +
                       wxFileName::GetPathSeparator();
  // Initialize catalog file
  wxString local_grib_catalog = "sources.json";
  wxString data_path = *GetpPrivateApplicationDataLocation() +
                       wxFileName::GetPathSeparator() + "DpGrib_pi";
  if (!wxDirExists(data_path)) {
    wxMkdir(data_path);
  }
  m_local_sources_catalog =
      data_path + wxFileName::GetPathSeparator() + local_grib_catalog;
  if (!wxFileExists(m_local_sources_catalog)) {
    wxCopyFile(shareLocn + local_grib_catalog, m_local_sources_catalog);
  }
  if (m_bGRIBShowIcon) {
    wxString normalIcon = shareLocn + _T("grib.svg");
    wxString toggledIcon = shareLocn + _T("grib_toggled.svg");
    wxString rolloverIcon = shareLocn + _T("grib_rollover.svg");

    //  For journeyman styles, we prefer the built-in raster icons which match
    //  the rest of the toolbar.
    if (GetActiveStyleName().Lower() != _T("traditional")) {
      normalIcon = _T("");
      toggledIcon = _T("");
      rolloverIcon = _T("");
    }

    wxLogMessage(normalIcon);
    m_leftclick_tool_id = InsertPlugInToolSVG(
        _T(""), normalIcon, rolloverIcon, toggledIcon, wxITEM_CHECK, _("Grib"),
        _T(""), nullptr, GRIB_TOOL_POSITION, 0, this);
  }

  if (!QualifyCtrlBarPosition(m_CtrlBarxy, m_CtrlBar_Sizexy)) {
    m_CtrlBarxy = wxPoint(20, 60);  // reset to the default position
    m_CursorDataxy = wxPoint(20, 170);
  }

  // Create API instance for communication with deepreygui
  if (!m_gribAPI) {
    m_gribAPI = new DpGrib::DpGribAPI(&m_settings, this);
    wxLogMessage("deepreygrib_pi: API created");
  }

  return (WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
          WANTS_CURSOR_LATLON | WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL |
          WANTS_CONFIG | WANTS_PREFERENCES | WANTS_PLUGIN_MESSAGING |
          WANTS_ONPAINT_VIEWPORT | WANTS_MOUSE_EVENTS | WANTS_NMEA_EVENTS |
          WANTS_LATE_INIT);
}

bool DpGrib_pi::DeInit(void) {
  // Reset timeline to system time before shutting down
  SendTimelineMessage(wxInvalidDateTime);

  if (m_pGribCtrlBar) {
    m_pGribCtrlBar->Close();
    delete m_pGribCtrlBar;
    m_pGribCtrlBar = nullptr;
  }

  delete m_pGRIBOverlayFactory;
  m_pGRIBOverlayFactory = nullptr;

  // Clean up API and notify deepreygui
  if (m_gribAPI) {
    delete m_gribAPI;
    m_gribAPI = nullptr;
    UpdateApiPtr();  // Send nullptr notification to deepreygui
    wxLogMessage("deeprey_grib_pi: API destroyed");
  }

  return true;
}

void DpGrib_pi::LateInit(void) {
  // LateInit is called after OpenCPN's main window is fully initialized.
  // This is the place for operations that depend on the UI being ready.
  //
  // Proactively announce API availability to any listening plugins.
  // This handles the case where deeprey-gui loaded before us.
  // If deeprey-gui loads later, it will send a discovery request and we respond.
  UpdateApiPtr();
  wxLogMessage("deeprey_grib_pi: LateInit - API announced to deepreygui");
}

int DpGrib_pi::GetAPIVersionMajor() { return MY_API_VERSION_MAJOR; }

int DpGrib_pi::GetAPIVersionMinor() { return MY_API_VERSION_MINOR; }

int DpGrib_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }

int DpGrib_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }

wxBitmap *DpGrib_pi::GetPlugInBitmap() { return &m_panelBitmap; }

wxString DpGrib_pi::GetCommonName() { return _T("deeprey_grib"); }

wxString DpGrib_pi::GetShortDescription() { return _("GRIB PlugIn for OpenCPN"); }

wxString DpGrib_pi::GetLongDescription() {
  return _(
      "GRIB PlugIn for OpenCPN\n\
Provides basic GRIB file overlay capabilities for several GRIB file types\n\
and a request function to get GRIB files by eMail.\n\n\
Supported GRIB data include:\n\
- wind direction and speed (at 10 m)\n\
- wind gust\n\
- surface pressure\n\
- rainfall\n\
- cloud cover\n\
- significant wave height and direction\n\
- air surface temperature (at 2 m)\n\
- sea surface temperature\n\
- surface current direction and speed\n\
- Convective Available Potential Energy (CAPE)\n\
- wind, altitude, temperature and relative humidity at 300, 500, 700, 850 hPa.");
}

void DpGrib_pi::SetDefaults(void) {}

int DpGrib_pi::GetToolBarToolCount(void) { return 1; }

bool DpGrib_pi::MouseEventHook(wxMouseEvent &event) {
  if ((m_pGribCtrlBar && m_pGribCtrlBar->pReq_Dialog))
    return m_pGribCtrlBar->pReq_Dialog->MouseEventHook(event);
  return false;
}

void DpGrib_pi::ShowPreferencesDialog(wxWindow *parent) {
  GribPreferencesDialog *Pref = new GribPreferencesDialog(parent);

  DimeWindow(Pref);     // aplly global colours scheme
  SetDialogFont(Pref);  // Apply global font

  Pref->m_cbUseHiDef->SetValue(m_bGRIBUseHiDef);
  Pref->m_cbUseGradualColors->SetValue(m_bGRIBUseGradualColors);
  Pref->m_cbDrawBarbedArrowHead->SetValue(m_bDrawBarbedArrowHead);
  Pref->m_cZoomToCenterAtInit->SetValue(m_bZoomToCenterAtInit);
  Pref->m_cbCopyFirstCumulativeRecord->SetValue(m_bCopyFirstCumRec);
  Pref->m_cbCopyMissingWaveRecord->SetValue(m_bCopyMissWaveRec);
  Pref->m_rbLoadOptions->SetSelection(m_bLoadLastOpenFile);
  Pref->m_rbStartOptions->SetSelection(m_bStartOptions);

  wxFileConfig *pConf = GetOCPNConfigObject();
  if (pConf) {
    wxString l_grib_dir;
    pConf->SetPath(_T ( "/Directories" ));
    pConf->Read(_T ( "GRIBDirectory" ), &l_grib_dir);
    Pref->m_grib_dir_sel = l_grib_dir;
    Pref->m_textDirectory->ChangeValue(l_grib_dir);
  }

#ifdef __WXMSW__
  int val = (m_GribIconsScaleFactor * 10.) - 10;
  Pref->m_sIconSizeFactor->SetValue(val);
#endif

#ifdef __OCPN__ANDROID__
  if (m_parent_window) {
    int xmax = m_parent_window->GetSize().GetWidth();
    int ymax = m_parent_window->GetParent()
                   ->GetSize()
                   .GetHeight();  // This would be the Options dialog itself
    Pref->SetSize(xmax, ymax);
    Pref->Layout();
    Pref->Move(0, 0);
  }
  Pref->Show();
#else
  // Constrain size on small displays

  int display_width, display_height;
  wxDisplaySize(&display_width, &display_height);
  int char_width = GetOCPNCanvasWindow()->GetCharWidth();
  int char_height = GetOCPNCanvasWindow()->GetCharHeight();
  if (display_height < 600) {
    wxSize canvas_size = GetOCPNCanvasWindow()->GetSize();
    Pref->SetMaxSize(GetOCPNCanvasWindow()->GetSize());
    Pref->SetSize(wxSize(60 * char_width, canvas_size.x * 8 / 10));
    Pref->CentreOnScreen();
  } else {
    Pref->SetMaxSize(GetOCPNCanvasWindow()->GetSize());
    Pref->SetSize(wxSize(60 * char_width, 32 * char_height));
  }

  Pref->ShowModal();
#endif
}

void DpGrib_pi::UpdatePrefs(GribPreferencesDialog *Pref) {
  m_bGRIBUseHiDef = Pref->m_cbUseHiDef->GetValue();
  m_bGRIBUseGradualColors = Pref->m_cbUseGradualColors->GetValue();
  m_bLoadLastOpenFile = Pref->m_rbLoadOptions->GetSelection();
  m_bDrawBarbedArrowHead = Pref->m_cbDrawBarbedArrowHead->GetValue();
  m_bZoomToCenterAtInit = Pref->m_cZoomToCenterAtInit->GetValue();
#ifdef __WXMSW__
  double val = Pref->m_sIconSizeFactor->GetValue();
  m_GribIconsScaleFactor = 1. + (val / 10);
#endif

  if (m_pGRIBOverlayFactory)
    m_pGRIBOverlayFactory->SetSettings(m_bGRIBUseHiDef, m_bGRIBUseGradualColors,
                                       m_bDrawBarbedArrowHead);

  int updatelevel = 0;

  if (m_bStartOptions != Pref->m_rbStartOptions->GetSelection()) {
    m_bStartOptions = Pref->m_rbStartOptions->GetSelection();
    updatelevel = 1;
  }

  bool copyrec = Pref->m_cbCopyFirstCumulativeRecord->GetValue();
  bool copywave = Pref->m_cbCopyMissingWaveRecord->GetValue();
  if (m_bCopyFirstCumRec != copyrec || m_bCopyMissWaveRec != copywave) {
    m_bCopyFirstCumRec = copyrec;
    m_bCopyMissWaveRec = copywave;
    updatelevel = 3;
  }

  if (m_pGribCtrlBar) {
    switch (updatelevel) {
      case 0:
        break;
      case 3:
        // rebuild current activefile with new parameters and rebuil data list
        // with current index
        m_pGribCtrlBar->CreateActiveFileFromNames(
            m_pGribCtrlBar->m_bGRIBActiveFile->GetFileNames());
        m_pGribCtrlBar->PopulateComboDataList();
        m_pGribCtrlBar->TimelineChanged();
        break;
      case 2:
        // only rebuild  data list with current index and new timezone
        // This no longer applicable because the timezone is set in the
        // OpenCPN core global settings (Options -> Display -> General)
        m_pGribCtrlBar->PopulateComboDataList();
        m_pGribCtrlBar->TimelineChanged();
        break;
      case 1:
        // only re-compute the best forecast
        m_pGribCtrlBar->ComputeBestForecastForNow();
        break;
    }
    if (Pref->m_grib_dir_sel.Length()) {
      m_pGribCtrlBar->m_grib_dir = Pref->m_grib_dir_sel;
      m_pGribCtrlBar->m_file_names.Clear();
    }
  }

  if (Pref->m_grib_dir_sel.Length()) {
    wxFileConfig *pConf = GetOCPNConfigObject();
    if (pConf) {
      pConf->SetPath(_T ( "/Directories" ));
      pConf->Write(_T ( "GRIBDirectory" ), Pref->m_grib_dir_sel);
      pConf->DeleteGroup(_T ( "/Settings/GRIB/FileNames" ));
      pConf->Flush();
    }
  }
  SaveConfig();
}

bool DpGrib_pi::QualifyCtrlBarPosition(
    wxPoint position,
    wxSize size) {  // Make sure drag bar (title bar) or grabber always screen
  bool b_reset_pos = false;
#ifdef __WXMSW__
  //  Support MultiMonitor setups which an allow negative window positions.
  //  If the requested window does not intersect any installed monitor,
  //  then default to simple primary monitor positioning.
  RECT frame_title_rect;
  frame_title_rect.left = position.x;
  frame_title_rect.top = position.y;
  frame_title_rect.right = position.x + size.x;
  frame_title_rect.bottom = m_DialogStyle == ATTACHED_HAS_CAPTION
                                ? position.y + 30
                                : position.y + size.y;

  if (nullptr == MonitorFromRect(&frame_title_rect, MONITOR_DEFAULTTONULL))
    b_reset_pos = true;
#else
  wxRect window_title_rect;  // conservative estimate
  window_title_rect.x = position.x;
  window_title_rect.y = position.y;
  window_title_rect.width = size.x;
  window_title_rect.height =
      m_DialogStyle == ATTACHED_HAS_CAPTION ? 30 : size.y;

  wxRect ClientRect = wxGetClientDisplayRect();
  if (!ClientRect.Intersects(window_title_rect)) b_reset_pos = true;

#endif
  return !b_reset_pos;
}

void DpGrib_pi::MoveDialog(wxDialog *dialog, wxPoint position) {
  //  Use the application frame to bound the control bar position.
  wxApp *app = wxTheApp;

  wxWindow *frame =
      app->GetTopWindow();  // or GetOCPNCanvasWindow()->GetParent();
  if (!frame) return;

  wxPoint p = frame->ScreenToClient(position);
  // Check and ensure there is always a "grabb" zone always visible wathever the
  // dialoue size is.
  if (p.x + dialog->GetSize().GetX() > frame->GetClientSize().GetX())
    p.x = frame->GetClientSize().GetX() - dialog->GetSize().GetX();
  if (p.y + dialog->GetSize().GetY() > frame->GetClientSize().GetY())
    p.y = frame->GetClientSize().GetY() - dialog->GetSize().GetY();

#ifdef __WXGTK__
  dialog->Move(0, 0);
#endif
  dialog->Move(frame->ClientToScreen(p));
}

void DpGrib_pi::OnToolbarToolCallback(int id) {
  // if( !::wxIsBusy() ) ::wxBeginBusyCursor();

  bool starting = false;

  double scale_factor =
      GetOCPNGUIToolScaleFactor_PlugIn() * OCPN_GetWinDIPScaleFactor();
#ifdef __WXMSW__
  scale_factor *= m_GribIconsScaleFactor;
#endif
  if (scale_factor != m_GUIScaleFactor) starting = true;

  if (!m_pGribCtrlBar) {
    starting = true;
    long style = m_DialogStyle == ATTACHED_HAS_CAPTION
                     ? wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU
                     : wxBORDER_NONE | wxSYSTEM_MENU;
#ifdef __WXOSX__
    style |= wxSTAY_ON_TOP;
#endif
    m_pGribCtrlBar = new GRIBUICtrlBar(m_parent_window, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition, wxDefaultSize, style,
                                       this, scale_factor);
    m_pGribCtrlBar->SetScaledBitmap(scale_factor);

    wxMenu *dummy = new wxMenu(_T("Plugin"));
    wxMenuItem *table =
        new wxMenuItem(dummy, wxID_ANY, wxString(_("Weather table")),
                       wxEmptyString, wxITEM_NORMAL);
    /* Menu font do not work properly for MSW (wxWidgets 3.2.1)
    #ifdef __WXMSW__
        wxFont *qFont = OCPNGetFont(_("Menu"));
        table->SetFont(*qFont);
    #endif
    */
    m_MenuItem = AddCanvasContextMenuItem(table, this);
    SetCanvasContextMenuItemViz(m_MenuItem, false);

    // Create the drawing factory
    m_pGRIBOverlayFactory = new GRIBOverlayFactory(*m_pGribCtrlBar);
    m_pGRIBOverlayFactory->SetMessageFont();
    m_pGRIBOverlayFactory->SetParentSize(m_display_width, m_display_height);
    m_pGRIBOverlayFactory->SetSettings(m_bGRIBUseHiDef, m_bGRIBUseGradualColors,
                                       m_bDrawBarbedArrowHead);

    m_pGribCtrlBar->OpenFile(m_bLoadLastOpenFile == 0);

    // Sync units with OpenCPN settings on first open
    SyncUnitsToGribSettings();
  }

  // Toggle GRIB overlay display
  m_bShowGrib = !m_bShowGrib;

  //    Toggle dialog?
  if (m_bShowGrib) {
    // A new file could have been added since grib plugin opened
    if (!starting && m_bLoadLastOpenFile == 0) {
      m_pGribCtrlBar->OpenFile(true);
      starting = true;
    }
    // the dialog font could have been changed since grib plugin opened
    if (m_pGribCtrlBar->GetFont() != *OCPNGetFont(_("Dialog"))) starting = true;
    if (starting) {
      m_pGRIBOverlayFactory->SetMessageFont();
      SetDialogFont(m_pGribCtrlBar);
      m_GUIScaleFactor = scale_factor;
      m_pGribCtrlBar->SetScaledBitmap(m_GUIScaleFactor);
      m_pGribCtrlBar->SetDialogsStyleSizePosition(true);
      m_pGribCtrlBar->Refresh();
    } else {
      MoveDialog(m_pGribCtrlBar, GetCtrlBarXY());
      if (m_DialogStyle >> 1 == SEPARATED) {
        MoveDialog(m_pGribCtrlBar->GetCDataDialog(), GetCursorDataXY());
        m_pGribCtrlBar->GetCDataDialog()->Show(m_pGribCtrlBar->m_CDataIsShown);
      }
#ifdef __OCPN__ANDROID__
      m_pGribCtrlBar->SetDialogsStyleSizePosition(true);
      m_pGribCtrlBar->Refresh();
#endif
    }
    m_pGribCtrlBar->Show();
    if (m_pGribCtrlBar->m_bGRIBActiveFile) {
      if (m_pGribCtrlBar->m_bGRIBActiveFile->IsOK()) {
        ArrayOfGribRecordSets *rsa =
            m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
        if (rsa->GetCount() > 1) {
          SetCanvasContextMenuItemViz(m_MenuItem, true);
        }
        if (rsa->GetCount() >= 1) {  // XXX Should be only on Show
          SendTimelineMessage(m_pGribCtrlBar->TimelineTime());
        }
      }
    }
    // Toggle is handled by the CtrlBar but we must keep plugin manager b_toggle
    // updated to actual status to ensure correct status upon CtrlBar rebuild
    SetToolbarItemState(m_leftclick_tool_id, m_bShowGrib);

    // Do an automatic "zoom-to-center" on the overlay canvas if set in
    // Preferences
    if (m_pGribCtrlBar && m_bZoomToCenterAtInit) {
      m_pGribCtrlBar->DoZoomToCenter();
    }

    RequestRefresh(m_parent_window);  // refresh main window
  } else
    m_pGribCtrlBar->Close();
}

void DpGrib_pi::OnGribCtrlBarClose() {
  m_bShowGrib = false;
  SetToolbarItemState(m_leftclick_tool_id, m_bShowGrib);

  m_pGribCtrlBar->Hide();

  SaveConfig();

  SetCanvasContextMenuItemViz(m_MenuItem, false);

  RequestRefresh(m_parent_window);  // refresh main window

  if (::wxIsBusy()) ::wxEndBusyCursor();

#ifdef __OCPN__ANDROID__
  m_DialogStyleChanged = true;  //  Force a delete of the control bar dialog
#endif

  if (m_DialogStyleChanged) {
    m_pGribCtrlBar->Destroy();
    m_pGribCtrlBar = nullptr;
    m_DialogStyleChanged = false;
  }
}

bool DpGrib_pi::RenderOverlay(wxDC &dc, PlugIn_ViewPort *vp) { return false; }

bool DpGrib_pi::DoRenderOverlay(wxDC &dc, PlugIn_ViewPort *vp, int canvasIndex) {
  if (!m_bShowGrib) return true;

  if (!m_pGribCtrlBar || !m_pGribCtrlBar->IsShown() || !m_pGRIBOverlayFactory)
    return false;

  m_pGRIBOverlayFactory->RenderGribOverlay(dc, vp);
  if (PluginGetFocusCanvas() == GetCanvasByIndex(canvasIndex)) {
    m_pGribCtrlBar->SetViewPortWithFocus(vp);
  }

  if (GetCanvasByIndex(canvasIndex) == GetCanvasUnderMouse()) {
    m_pGribCtrlBar->SetViewPortUnderMouse(vp);
    if (m_pGribCtrlBar->pReq_Dialog &&
        GetCanvasIndexUnderMouse() ==
            m_pGribCtrlBar->pReq_Dialog->GetBoundingBoxCanvasIndex()) {
      m_pGribCtrlBar->pReq_Dialog->RenderZoneOverlay(dc);
    }
  }
  if (::wxIsBusy()) ::wxEndBusyCursor();
  return true;
}

bool DpGrib_pi::RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp) {
  return false;
}

bool DpGrib_pi::DoRenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp,
                                int canvasIndex) {
  if (!m_bShowGrib) return true;

  if (!m_pGribCtrlBar || !m_pGribCtrlBar->IsShown() || !m_pGRIBOverlayFactory)
    return false;

  m_pGRIBOverlayFactory->RenderGLGribOverlay(pcontext, vp);
  if (PluginGetFocusCanvas() == GetCanvasByIndex(canvasIndex)) {
    m_pGribCtrlBar->SetViewPortWithFocus(vp);
  }

  if (GetCanvasByIndex(canvasIndex) == GetCanvasUnderMouse()) {
    m_pGribCtrlBar->SetViewPortUnderMouse(vp);
    if (m_pGribCtrlBar->pReq_Dialog &&
        GetCanvasIndexUnderMouse() ==
            m_pGribCtrlBar->pReq_Dialog->GetBoundingBoxCanvasIndex()) {
      m_pGribCtrlBar->pReq_Dialog->RenderGlZoneOverlay();
    }
  }

  if (::wxIsBusy()) ::wxEndBusyCursor();

#ifdef __OCPN__ANDROID__
  m_pGribCtrlBar->Raise();  // Control bar should always be visible
#endif

  return true;
}

bool DpGrib_pi::RenderGLOverlayMultiCanvas(wxGLContext *pcontext,
                                         PlugIn_ViewPort *vp, int canvasIndex) {
  return DoRenderGLOverlay(pcontext, vp, canvasIndex);
}

bool DpGrib_pi::RenderOverlayMultiCanvas(wxDC &dc, PlugIn_ViewPort *vp,
                                       int canvasIndex) {
  return DoRenderOverlay(dc, vp, canvasIndex);
}

void DpGrib_pi::SetCursorLatLon(double lat, double lon) {
  if (m_pGribCtrlBar && m_pGribCtrlBar->IsShown())
    m_pGribCtrlBar->SetCursorLatLon(lat, lon);
  
  // Notify API listeners about cursor position change
  if (m_gribAPI) {
    static_cast<DpGrib::DpGribAPI*>(m_gribAPI)->NotifyCursorPosition(lat, lon);
  }
}

void DpGrib_pi::OnContextMenuItemCallback(int id) {
  if (!m_pGribCtrlBar->m_bGRIBActiveFile) return;
  m_pGribCtrlBar->ContextMenuItemCallback(id);
}

void DpGrib_pi::SetDialogFont(wxWindow *dialog, wxFont *font) {
  dialog->SetFont(*font);
  wxWindowList list = dialog->GetChildren();
  wxWindowListNode *node = list.GetFirst();
  for (size_t i = 0; i < list.GetCount(); i++) {
    wxWindow *win = node->GetData();
    win->SetFont(*font);
    node = node->GetNext();
  }
  dialog->Fit();
  dialog->Refresh();
}

void DpGrib_pi::SetPluginMessage(wxString &message_id, wxString &message_body) {
  // Handle discovery request from deeprey-gui
  //
  // When deeprey-gui starts, it broadcasts discovery messages to find plugins.
  // When we receive our message ID, we respond with our API pointer.
  if (message_id == _T("DP_GUI_TO_GRIB")) {
    UpdateApiPtr();
    // Toggle to show GRIB on charts
    // if (!m_bShowGrib) {
    //   OnToolbarToolCallback(0);  // This will toggle m_bShowGrib and show the dialog
    // }
    return;
  }

  // Handle global settings update
  // OpenCPN broadcasts this when user changes Settings → Display → Units
  if (message_id == _T("GLOBAL_SETTINGS_UPDATED")) {
    DpUnitManager::Instance().LoadSettings();
    SyncUnitsToGribSettings();
    return;
  }

  // Handle legacy GRIB-specific messages below

  if (message_id == _T("GRIB_VALUES_REQUEST")) {
    if (!m_pGribCtrlBar) OnToolbarToolCallback(0);

    // lat, lon, time, what
    wxJSONReader r;
    wxJSONValue v;
    r.Parse(message_body, &v);
    if (!v.HasMember(_T("Day"))) {
      // bogus or loading grib
      SendPluginMessage(wxString(_T("GRIB_VALUES")), _T(""));
      return;
    }
    wxDateTime time(v[_T("Day")].AsInt(),
                    (wxDateTime::Month)v[_T("Month")].AsInt(),
                    v[_T("Year")].AsInt(), v[_T("Hour")].AsInt(),
                    v[_T("Minute")].AsInt(), v[_T("Second")].AsInt());
    double lat = v[_T("lat")].AsDouble();
    double lon = v[_T("lon")].AsDouble();

    if (m_pGribCtrlBar) {
      if (v.HasMember(_T("WIND SPEED"))) {
        double vkn, ang;
        if (m_pGribCtrlBar->getTimeInterpolatedValues(
                vkn, ang, Idx_WIND_VX, Idx_WIND_VY, lon, lat, time) &&
            vkn != GRIB_NOTDEF) {
          v[_T("Type")] = wxT("Reply");
          v[_T("WIND SPEED")] = vkn;
          v[_T("WIND DIR")] = ang;
        } else {
          v.Remove(_T("WIND SPEED"));
          v.Remove(_T("WIND DIR"));
        }
      }
      if (v.HasMember(_T("CURRENT SPEED"))) {
        double vkn, ang;
        if (m_pGribCtrlBar->getTimeInterpolatedValues(
                vkn, ang, Idx_SEACURRENT_VX, Idx_SEACURRENT_VY, lon, lat,
                time) &&
            vkn != GRIB_NOTDEF) {
          v[_T("Type")] = wxT("Reply");
          v[_T("CURRENT SPEED")] = vkn;
          v[_T("CURRENT DIR")] = ang;
        } else {
          v.Remove(_T("CURRENT SPEED"));
          v.Remove(_T("CURRENT DIR"));
        }
      }
      if (v.HasMember(_T("GUST"))) {
        double vkn = m_pGribCtrlBar->getTimeInterpolatedValue(Idx_WIND_GUST,
                                                              lon, lat, time);
        if (vkn != GRIB_NOTDEF) {
          v[_T("Type")] = wxT("Reply");
          v[_T("GUST")] = vkn;
        } else
          v.Remove(_T("GUST"));
      }
      if (v.HasMember(_T("SWELL"))) {
        double vkn = m_pGribCtrlBar->getTimeInterpolatedValue(Idx_HTSIGW, lon,
                                                              lat, time);
        if (vkn != GRIB_NOTDEF) {
          v[_T("Type")] = wxT("Reply");
          v[_T("SWELL")] = vkn;
        } else
          v.Remove(_T("SWELL"));
      }

      wxJSONWriter w;
      wxString out;
      w.Write(v, out);
      SendPluginMessage(wxString(_T("GRIB_VALUES")), out);
    }
  } else if (message_id == _T("GRIB_VERSION_REQUEST")) {
    wxJSONValue v;
    v[_T("GribVersionMinor")] = GetAPIVersionMinor();
    v[_T("GribVersionMajor")] = GetAPIVersionMajor();

    wxJSONWriter w;
    wxString out;
    w.Write(v, out);
    SendPluginMessage(wxString(_T("GRIB_VERSION")), out);
  } else if (message_id == _T("GRIB_TIMELINE_REQUEST")) {
    // local time
    SendTimelineMessage(m_pGribCtrlBar ? m_pGribCtrlBar->TimelineTime()
                                       : wxDateTime::Now());
  } else if (message_id == _T("GRIB_TIMELINE_RECORD_REQUEST")) {
    wxJSONReader r;
    wxJSONValue v;
    r.Parse(message_body, &v);
    wxDateTime time(v[_T("Day")].AsInt(),
                    (wxDateTime::Month)v[_T("Month")].AsInt(),
                    v[_T("Year")].AsInt(), v[_T("Hour")].AsInt(),
                    v[_T("Minute")].AsInt(), v[_T("Second")].AsInt());

    if (!m_pGribCtrlBar) OnToolbarToolCallback(0);

    GribTimelineRecordSet *set =
        m_pGribCtrlBar ? m_pGribCtrlBar->GetTimeLineRecordSet(time) : nullptr;

    char ptr[64];
    snprintf(ptr, sizeof ptr, "%p", set);

    v[_T("GribVersionMajor")] = PLUGIN_VERSION_MAJOR;
    v[_T("GribVersionMinor")] = PLUGIN_VERSION_MINOR;
    v[_T("TimelineSetPtr")] = wxString::From8BitData(ptr);

    wxJSONWriter w;
    wxString out;
    w.Write(v, out);
    SendPluginMessage(wxString(_T("GRIB_TIMELINE_RECORD")), out);
    delete m_pLastTimelineSet;
    m_pLastTimelineSet = set;
  }

  else if (message_id == _T("GRIB_APPLY_JSON_CONFIG")) {
    wxLogMessage(_T("Got GRIB_APPLY_JSON_CONFIG"));

    if (m_pGribCtrlBar) {
      m_pGribCtrlBar->OpenFileFromJSON(message_body);

      m_pGribCtrlBar->m_OverlaySettings.JSONToSettings(message_body);
      m_pGribCtrlBar->m_OverlaySettings.Write();
      m_pGribCtrlBar->SetDialogsStyleSizePosition(true);
    }
  }
}

void DpGrib_pi::UpdateApiPtr() {
  // Send our API pointer to deeprey-gui.
  //
  // Message format: Pointer value as string (base 10 unsigned long long)
  // When m_gribAPI is nullptr (during DeInit), this notifies deeprey-gui
  // to clear its cached pointer.
  wxString apiPtrStr = wxString::Format("%llu", (unsigned long long)m_gribAPI);
  SendPluginMessage("GRIB_API_TO_DP_GUI", apiPtrStr);
}

bool DpGrib_pi::LoadConfig(void) {
  wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath(_T( "/PlugIns/GRIB" ));
  pConf->Read(_T( "LoadLastOpenFile" ), &m_bLoadLastOpenFile, 0);
  pConf->Read(_T("OpenFileOption" ), &m_bStartOptions, 1);
  pConf->Read(_T( "GRIBUseHiDef" ), &m_bGRIBUseHiDef, 0);
  pConf->Read(_T( "GRIBUseGradualColors" ), &m_bGRIBUseGradualColors, 0);
  pConf->Read(_T( "DrawBarbedArrowHead" ), &m_bDrawBarbedArrowHead, 1);
  pConf->Read(_T( "ZoomToCenterAtInit"), &m_bZoomToCenterAtInit, 1);
  pConf->Read(_T( "ShowGRIBIcon" ), &m_bGRIBShowIcon, 1);
  pConf->Read(_T( "CopyFirstCumulativeRecord" ), &m_bCopyFirstCumRec, 1);
  pConf->Read(_T( "CopyMissingWaveRecord" ), &m_bCopyMissWaveRec, 1);
#ifdef __WXMSW__
  pConf->Read(_T("GribIconsScaleFactor"), &m_GribIconsScaleFactor, 1);
#endif

  m_CtrlBar_Sizexy.x = pConf->Read(_T ( "GRIBCtrlBarSizeX" ), 1400L);
  m_CtrlBar_Sizexy.y = pConf->Read(_T ( "GRIBCtrlBarSizeY" ), 800L);
  m_CtrlBarxy.x = pConf->Read(_T ( "GRIBCtrlBarPosX" ), 20L);
  m_CtrlBarxy.y = pConf->Read(_T ( "GRIBCtrlBarPosY" ), 60L);
  m_CursorDataxy.x = pConf->Read(_T ( "GRIBCursorDataPosX" ), 20L);
  m_CursorDataxy.y = pConf->Read(_T ( "GRIBCursorDataPosY" ), 170L);

  pConf->Read(_T ( "GribCursorDataDisplayStyle" ), &m_DialogStyle, 0);
  if (m_DialogStyle > 3)
    m_DialogStyle = 0;  // ensure validity of the .conf value

  return true;
}

bool DpGrib_pi::SaveConfig(void) {
  wxFileConfig *pConf = (wxFileConfig *)m_pconfig;

  if (!pConf) return false;

  pConf->SetPath(_T( "/PlugIns/GRIB" ));

  pConf->Write(_T ( "LoadLastOpenFile" ), m_bLoadLastOpenFile);
  pConf->Write(_T ( "OpenFileOption" ), m_bStartOptions);
  pConf->Write(_T ( "ShowGRIBIcon" ), m_bGRIBShowIcon);
  pConf->Write(_T ( "GRIBUseHiDef" ), m_bGRIBUseHiDef);
  pConf->Write(_T ( "GRIBUseGradualColors" ), m_bGRIBUseGradualColors);
  pConf->Write(_T ( "CopyFirstCumulativeRecord" ), m_bCopyFirstCumRec);
  pConf->Write(_T ( "CopyMissingWaveRecord" ), m_bCopyMissWaveRec);
  pConf->Write(_T ( "DrawBarbedArrowHead" ), m_bDrawBarbedArrowHead);
  pConf->Write(_T ( "ZoomToCenterAtInit"), m_bZoomToCenterAtInit);
#ifdef __WXMSW__
  pConf->Write(_T("GribIconsScaleFactor"), m_GribIconsScaleFactor);
#endif

  pConf->Write(_T ( "GRIBCtrlBarSizeX" ), m_CtrlBar_Sizexy.x);
  pConf->Write(_T ( "GRIBCtrlBarSizeY" ), m_CtrlBar_Sizexy.y);
  pConf->Write(_T ( "GRIBCtrlBarPosX" ), m_CtrlBarxy.x);
  pConf->Write(_T ( "GRIBCtrlBarPosY" ), m_CtrlBarxy.y);
  pConf->Write(_T ( "GRIBCursorDataPosX" ), m_CursorDataxy.x);
  pConf->Write(_T ( "GRIBCursorDataPosY" ), m_CursorDataxy.y);

  return true;
}

void DpGrib_pi::SetColorScheme(PI_ColorScheme cs) {
  DimeWindow(m_pGribCtrlBar);
  if (m_pGribCtrlBar) {
    if (m_pGRIBOverlayFactory) m_pGRIBOverlayFactory->ClearCachedLabel();
    if (m_pGribCtrlBar->pReq_Dialog) m_pGribCtrlBar->pReq_Dialog->Refresh();
    m_pGribCtrlBar->Refresh();
    // m_pGribDialog->SetDataBackGroundColor();
  }
}

void DpGrib_pi::SendTimelineMessage(wxDateTime time) {
  if (!m_pGribCtrlBar) return;

  wxJSONValue v;
  if (time.IsValid()) {
    v[_T("Day")] = time.GetDay();
    v[_T("Month")] = time.GetMonth();
    v[_T("Year")] = time.GetYear();
    v[_T("Hour")] = time.GetHour();
    v[_T("Minute")] = time.GetMinute();
    v[_T("Second")] = time.GetSecond();
  } else {
    v[_T("Day")] = -1;
    v[_T("Month")] = -1;
    v[_T("Year")] = -1;
    v[_T("Hour")] = -1;
    v[_T("Minute")] = -1;
    v[_T("Second")] = -1;
  }
  wxJSONWriter w;
  wxString out;
  w.Write(v, out);
  SendPluginMessage(wxString(_T("GRIB_TIMELINE")), out);
}

void DpGrib_pi::SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix) {
  m_boat_cog = pfix.Cog;
  m_boat_sog = pfix.Sog;
  m_boat_lat = pfix.Lat;
  m_boat_lon = pfix.Lon;
  if (pfix.FixTime != 0) {
    m_boat_time = pfix.FixTime;
  } else {
    m_boat_time = wxDateTime::Now().GetTicks();
  }
}

// Internal methods for API access
void DpGrib_pi::Internal_SetVisible(bool visible) {
  // Only act if the desired state differs from current state
  if (visible == m_bShowGrib) {
    return;  // Already in desired state
  }
  
  // Reuse the existing toolbar callback which handles all the UI logic
  OnToolbarToolCallback(0);
  
  // Notify API callbacks that visibility changed
  if (m_gribAPI) {
    m_gribAPI->NotifyStateChanged();
  }
}

bool DpGrib_pi::Internal_IsVisible() const {
  return m_bShowGrib;
}

void DpGrib_pi::Internal_StartWorldDownload(double latMin, double lonMin,
                                            double latMax, double lonMax,
                                            int durationHours) {

  // Forward to the request dialog's API download method
  m_pGribCtrlBar->pReq_Dialog->StartWorldDownloadFromAPI(
      latMin, lonMin, latMax, lonMax, durationHours);
}

bool DpGrib_pi::Internal_IsDownloading() const {
  if (m_pGribCtrlBar && m_pGribCtrlBar->pReq_Dialog) {
    return m_pGribCtrlBar->pReq_Dialog->IsDownloading();
  }
  return false;
}

void DpGrib_pi::Internal_CancelDownload() {
  if (m_pGribCtrlBar && m_pGribCtrlBar->pReq_Dialog) {
    m_pGribCtrlBar->pReq_Dialog->CancelCurrentDownload();
  }
}

void DpGrib_pi::NotifyDownloadProgress(long transferred, long total,
                                        bool completed, bool success) {
  if (m_gribAPI) {
    m_gribAPI->NotifyDownloadProgress(transferred, total, completed, success);
  }
}

// Playback controls
void DpGrib_pi::Internal_SetLoopMode(bool loop) {
  if (!m_pGribCtrlBar) return;
  m_pGribCtrlBar->m_OverlaySettings.m_bLoopMode = loop;
  m_pGribCtrlBar->m_OverlaySettings.Write();
  // Refresh UI state
  m_pGribCtrlBar->SetFactoryOptions();
}

bool DpGrib_pi::Internal_GetLoopMode() const {
  if (!m_pGribCtrlBar) return false;
  return m_pGribCtrlBar->m_OverlaySettings.m_bLoopMode;
}

void DpGrib_pi::Internal_SetPlaybackSpeed(int speed) {
  if (!m_pGribCtrlBar) return;
  if (speed < 1) speed = 1; // enforce reasonable minimum
  m_pGribCtrlBar->m_OverlaySettings.m_UpdatesPerSecond = speed;
  m_pGribCtrlBar->m_OverlaySettings.Write();

  // If playback is running, restart timer with the new interval
  if (m_pGribCtrlBar->m_tPlayStop.IsRunning()) {
    m_pGribCtrlBar->m_tPlayStop.Stop();
    m_pGribCtrlBar->m_tPlayStop.Start(3000 / speed, wxTIMER_CONTINUOUS);
  }

  m_pGribCtrlBar->SetFactoryOptions();
}

int DpGrib_pi::Internal_GetPlaybackSpeed() const {
  if (!m_pGribCtrlBar) return 4;
  return m_pGribCtrlBar->m_OverlaySettings.m_UpdatesPerSecond;
}

void DpGrib_pi::Internal_SetGlobalSymbolSpacing(int pixels) {
  if (!m_pGribCtrlBar) return;

  GribOverlaySettings& overlay = m_pGribCtrlBar->m_OverlaySettings;

  // 1. Calculate Particle Density based on pixel spacing
  // We map the pixel range [30..100] inversely to Density [2.0..0.5]
  // 30px (Dense) -> 2.0 density
  // 100px (Sparse) -> 0.5 density
  double normalized = (double)(pixels - 30) / 70.0; // 0.0 to 1.0 (Dense to Sparse)
  double density = 2.0 - (1.5 * normalized);        // Map to 2.0 down to 0.5

  // 2. Apply to WIND (Barbs + Particles)
  overlay.Settings[GribOverlaySettings::WIND].m_iBarbArrSpacing = pixels;
  overlay.Settings[GribOverlaySettings::WIND].m_bBarbArrFixSpac = false;
  overlay.Settings[GribOverlaySettings::WIND].m_dParticleDensity = density; // <--- NEW

  // 3. Apply to WIND_GUST (Barbs only)
  overlay.Settings[GribOverlaySettings::WIND_GUST].m_iBarbArrSpacing = pixels;
  overlay.Settings[GribOverlaySettings::WIND_GUST].m_bBarbArrFixSpac = false;  // Changed to false for consistency

  // 4. Apply to WAVES (Arrows only)
  overlay.Settings[GribOverlaySettings::WAVE].m_iDirArrSpacing = pixels;
  overlay.Settings[GribOverlaySettings::WAVE].m_bDirArrFixSpac = false;

  // 5. Apply to CURRENT (Arrows + Particles)
  overlay.Settings[GribOverlaySettings::CURRENT].m_iDirArrSpacing = pixels;
  overlay.Settings[GribOverlaySettings::CURRENT].m_bDirArrFixSpac = false;
  overlay.Settings[GribOverlaySettings::CURRENT].m_dParticleDensity = density; // <--- NEW

  // 6. Save and Refresh
  overlay.Write();
  m_pGribCtrlBar->SetFactoryOptions(); 
  RequestRefresh(m_parent_window);
}

int DpGrib_pi::Internal_GetGlobalSymbolSpacing() const {
  if (!m_pGribCtrlBar) return 50;  // Default spacing
  
  // Return the spacing from WIND barbed arrows (they should all be the same)
  return m_pGribCtrlBar->m_OverlaySettings.Settings[GribOverlaySettings::WIND].m_iBarbArrSpacing;
}
int DpGrib_pi::Internal_GetTimeStepCount() const {
  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile) {
    return 0;
  }
  
  ArrayOfGribRecordSets *rsa = m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (!rsa) return 0;
  
  return rsa->GetCount();
}

int DpGrib_pi::Internal_GetCurrentTimeIndex() const {
  if (!m_pGribCtrlBar) {
    return -1;
  }
  return m_pGribCtrlBar->m_cRecordForecast->GetCurrentSelection();
}

bool DpGrib_pi::Internal_SetTimeIndex(int index) {
  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile) {
    wxLogWarning("DpGrib_pi::Internal_SetTimeIndex(%d) - No GRIB file loaded", index);
    return false;
  }
  
  ArrayOfGribRecordSets *rsa = m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (!rsa) {
    wxLogWarning("DpGrib_pi::Internal_SetTimeIndex(%d) - No record sets available", index);
    return false;
  }
  
  int count = rsa->GetCount();
  if (index < 0 || index >= count) {
    wxLogWarning("DpGrib_pi::Internal_SetTimeIndex(%d) - Index out of range [0-%d]", 
                 index, count - 1);
    return false;
  }
  
  wxLogMessage("DpGrib_pi::Internal_SetTimeIndex(%d/%d) - Setting timeline", index, count - 1);
  m_pGribCtrlBar->m_cRecordForecast->SetSelection(index);
  m_pGribCtrlBar->TimelineChanged();
  RequestRefresh(m_parent_window);
  return true;
}

bool DpGrib_pi::Internal_SetDisplayToCurrentTime() {
  // Call the plugin's existing "now" logic
  // This finds nearest forecast and updates the control bar and display
  m_pGribCtrlBar->ComputeBestForecastForNow();
  
  return true;
}

wxString DpGrib_pi::Internal_GetCurrentTimeString() const {
  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile) {
    return wxEmptyString;
  }
  
  ArrayOfGribRecordSets *rsa = m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (!rsa || rsa->GetCount() == 0) return wxEmptyString;
  
  int index = m_pGribCtrlBar->m_cRecordForecast->GetCurrentSelection();
  if (index < 0 || index >= (int)rsa->GetCount()) {
    return wxEmptyString;
  }
  
  wxDateTime time = rsa->Item(index).m_Reference_Time;
  return time.Format(_T("%Y-%m-%d %H:%M UTC"));
}

wxString DpGrib_pi::Internal_GetTimeString(int index) const {
  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile) {
    return wxEmptyString;
  }
  
  ArrayOfGribRecordSets *rsa = m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (!rsa) return wxEmptyString;
  
  int count = rsa->GetCount();
  if (index < 0 || index >= count) {
    return wxEmptyString;
  }
  
  wxDateTime time = rsa->Item(index).m_Reference_Time;
  DateTimeFormatOptions options;
  options.SetFormatString("$weekday_short_date $hour_minutes");  // "Wed 12/15/2021 10:00"
  options.SetTimezone("UTC");  // Use UTC

  return toUsrDateTimeFormat_Plugin(time, options);
}

wxString DpGrib_pi::Internal_GetCurrentTimeStringLocal() const {
  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile) {
    return wxEmptyString;
  }
  
  ArrayOfGribRecordSets *rsa = m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (!rsa) return wxEmptyString;
  
  int index = m_pGribCtrlBar->m_cRecordForecast->GetCurrentSelection();
  if (index < 0 || index >= (int)rsa->GetCount()) {
    return wxEmptyString;
  }
  
  // GRIB Reference_Time is in UTC (stored as time_t)
  // We need to convert it to local time before passing to toUsrDateTimeFormat_Plugin
  wxDateTime timeUTC;
  timeUTC.Set(rsa->Item(index).m_Reference_Time);  // time_t is UTC
  wxDateTime timeLocal = timeUTC.FromTimezone(wxDateTime::Local);
  
  // Configure formatting options for local time
  DateTimeFormatOptions options;
  options.SetFormatString("$weekday_short_date $hour_minutes");  // "Wed 12/15/2021 10:00"
  options.SetTimezone("Local Time");  // Use system local time
  options.SetShowTimezone(true);  // Include timezone abbreviation (e.g., "EST")

  return toUsrDateTimeFormat_Plugin(timeLocal, options);
}

wxString DpGrib_pi::Internal_GetTimeStringLocal(int index) const {
  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile) {
    return wxEmptyString;
  }
  
  ArrayOfGribRecordSets *rsa = m_pGribCtrlBar->m_bGRIBActiveFile->GetRecordSetArrayPtr();
  if (!rsa) return wxEmptyString;
  
  int count = rsa->GetCount();
  if (index < 0 || index >= count) {
    return wxEmptyString;
  }
  
  // GRIB Reference_Time is in UTC (stored as time_t)
  // We need to convert it to local time before passing to toUsrDateTimeFormat_Plugin
  wxDateTime timeUTC;
  timeUTC.Set(rsa->Item(index).m_Reference_Time);  // time_t is UTC
  wxDateTime timeLocal = timeUTC.FromTimezone(wxDateTime::Local);
  
  // Configure formatting options for local time
  DateTimeFormatOptions options;
  options.SetFormatString("$weekday_short_date $hour_minutes");  // "Wed 12/15/2021 10:00"
  options.SetTimezone("Local Time");  // Use system local time
  options.SetShowTimezone(true);  // Include timezone abbreviation (e.g., "EST")

  return toUsrDateTimeFormat_Plugin(timeLocal, options);
}

void DpGrib_pi::Internal_SetOverlayTransparency(int transparency) {
  if (!m_pGribCtrlBar) return;
  
  // Clamp to 0-100 range
  if (transparency < 0) transparency = 0;
  if (transparency > 100) transparency = 100;
  
  // Convert from percentage (0-100) to internal alpha (0-254)
  // 0% = fully transparent (alpha 0)
  // 100% = fully opaque (alpha 254)
  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.m_iOverlayTransparency = (int)(transparency * 254.0 / 100.0);
  
  // Save to config
  settings.Write();
  
  // Apply changes immediately
  m_pGribCtrlBar->SetFactoryOptions();
  RequestRefresh(GetOCPNCanvasWindow());
}

int DpGrib_pi::Internal_GetOverlayTransparency() const {
  if (!m_pGribCtrlBar) return 50; // Default 50%
  
  // Convert from internal alpha (0-254) to percentage (0-100)
  int alpha = m_pGribCtrlBar->m_OverlaySettings.m_iOverlayTransparency;
  return (int)(alpha * 100.0 / 254.0);
}
//----------------------------------------------------------------------------------------------------------
//          Layer Management Implementation
//----------------------------------------------------------------------------------------------------------
bool DpGrib_pi::Internal_SetLayerVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    return false;
  }
  
  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    return false;
  }
  
  m_pGribCtrlBar->m_bDataPlot[layerId] = visible;
  RequestRefresh(m_parent_window);
  return true;
}

bool DpGrib_pi::Internal_IsLayerVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    return false;
  }
  
  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    return false;
  }
  
  return m_pGribCtrlBar->m_bDataPlot[layerId];
}

bool DpGrib_pi::Internal_IsLayerAvailable(int layerId) const {
  wxLogDebug("DpGrib_pi::Internal_IsLayerAvailable(%d) called", layerId);

  if (!m_pGribCtrlBar || !m_pGribCtrlBar->m_bGRIBActiveFile)
    return false;

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT)
    return false;

  const GRIBFile::GribIdxArray &idxArray = m_pGribCtrlBar->m_bGRIBActiveFile->m_GribIdxArray;

  switch (layerId) {
    case GribOverlaySettings::WIND:
      return (idxArray.Index(Idx_WIND_VX) != wxNOT_FOUND) &&
             (idxArray.Index(Idx_WIND_VY) != wxNOT_FOUND);
    case GribOverlaySettings::WIND_GUST:
      return (idxArray.Index(Idx_WIND_GUST) != wxNOT_FOUND);
    case GribOverlaySettings::PRESSURE:
      return (idxArray.Index(Idx_PRESSURE) != wxNOT_FOUND);
    case GribOverlaySettings::WAVE:
      return (idxArray.Index(Idx_HTSIGW) != wxNOT_FOUND);
    case GribOverlaySettings::CURRENT:
      return (idxArray.Index(Idx_SEACURRENT_VX) != wxNOT_FOUND) &&
             (idxArray.Index(Idx_SEACURRENT_VY) != wxNOT_FOUND);
    case GribOverlaySettings::PRECIPITATION:
      return (idxArray.Index(Idx_PRECIP_TOT) != wxNOT_FOUND);
    case GribOverlaySettings::CLOUD:
      return (idxArray.Index(Idx_CLOUD_TOT) != wxNOT_FOUND);
    case GribOverlaySettings::AIR_TEMPERATURE:
      return (idxArray.Index(Idx_AIR_TEMP) != wxNOT_FOUND);
    case GribOverlaySettings::SEA_TEMPERATURE:
      return (idxArray.Index(Idx_SEA_TEMP) != wxNOT_FOUND);
    case GribOverlaySettings::CAPE:
      return (idxArray.Index(Idx_CAPE) != wxNOT_FOUND);
    case GribOverlaySettings::COMP_REFL:
      return (idxArray.Index(Idx_COMP_REFL) != wxNOT_FOUND);
    default:
      return false;
  }
}

wxString DpGrib_pi::Internal_GetLayerValueAtPoint(int layerId, double latitude, double longitude) const {
  if (!m_pGribCtrlBar) {
    return wxEmptyString;
  }
  
  // Safety check: Return empty if no GRIB file is loaded yet
  // This prevents crashes during download initialization (reentrancy bug)
  if (!m_pGribCtrlBar->m_bGRIBActiveFile) {
    return wxEmptyString;
  }
  
  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    return wxEmptyString;
  }
  
  // Use the control bar's helper method for consistent formatting
  return m_pGribCtrlBar->GetFormattedLayerValueAtPoint(layerId, longitude, latitude);
}

//----------------------------------------------------------------------------------------------------------
//          Prefrence dialog Implementation
//----------------------------------------------------------------------------------------------------------
void GribPreferencesDialog::OnStartOptionChange(wxCommandEvent &event) {
  if (m_rbStartOptions->GetSelection() == 2) {
    OCPNMessageBox_PlugIn(
        this,
        _("You have chosen to authorize interpolation.\nDon't forget that data "
          "displayed at current time will not be real but Recomputed\nThis can "
          "decrease accuracy!"),
        _("Warning!"));
  }
}

void GribPreferencesDialog::OnOKClick(wxCommandEvent &event) {
  if (g_pi) g_pi->UpdatePrefs(this);
  Close();
}

//----------------------------------------------------------------------------------------------------------
//          Unit Synchronization Implementation
//----------------------------------------------------------------------------------------------------------

// Map DeepRey GUI SpeedFormat to Grib Units0/Units7
// DeepRey GUI: 0=Knots, 1=Mph, 2=Km/h, 3=m/s
// Grib Units0: KNOTS=0, M_S=1, MPH=2, KPH=3, BFS=4
// Grib Units7: KNOTS=0, M_S=1, MPH=2, KPH=3
static int MapOcpnSpeedToGrib(int ocpnUnit) {
  switch (ocpnUnit) {
    case 0: return GribOverlaySettings::KNOTS;  // 0
    case 1: return GribOverlaySettings::MPH;    // 2
    case 2: return GribOverlaySettings::KPH;    // 3
    case 3: return GribOverlaySettings::M_S;    // 1
    default: return GribOverlaySettings::KNOTS;
  }
}

// Map OpenCPN S52_DEPTH_UNIT to Grib Units2
// OpenCPN: 0=ft, 1=m, 2=fa
// Grib Units2: METERS=0, FEET=1, FATHOMS=2
static int MapOcpnDepthToGrib(int ocpnUnit) {
  switch (ocpnUnit) {
    case 0: return GribOverlaySettings::FEET;    // 1
    case 1: return GribOverlaySettings::METERS;  // 0
    case 2: return GribOverlaySettings::FATHOMS; // 2
    default: return GribOverlaySettings::METERS;
  }
}

// Map OpenCPN TemperatureFormat to Grib Units3
// OpenCPN: 0=Celsius, 1=Fahrenheit, 2=Kelvin
// Grib Units3: CELCIUS=0, FAHRENHEIT=1, KELVIN=2
static int MapOcpnTempToGrib(int ocpnUnit) {
  switch (ocpnUnit) {
    case 0: return GribOverlaySettings::CELCIUS;
    case 1: return GribOverlaySettings::FAHRENHEIT;
    case 2: return GribOverlaySettings::KELVIN;
    default: return GribOverlaySettings::CELCIUS;
  }
}

// Map OpenCPN PressureFormat to Grib Units1
// OpenCPN: 0=hPa, 1=mmHg, 2=inHg
// Grib Units1: MILLIBARS=0, MMHG=1, INHG=2
static int MapOcpnPressureToGrib(int ocpnUnit) {
  // Direct mapping
  return ocpnUnit;
}

// Map OpenCPN RainfallFormat to Grib Units4
// OpenCPN: 0=Millimeters, 1=Inches
// Grib Units4: MILLIMETERS=0, INCHES=1
static int MapOcpnRainfallToGrib(int ocpnUnit) {
  // Direct mapping
  return ocpnUnit;
}

void DpGrib_pi::SyncUnitsToGribSettings() {
  auto& um = DpUnitManager::Instance();
  if (!um.IsInitialized()) return;
  if (!m_pGribCtrlBar) return;

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;

  // Map OpenCPN wind speed to Grib speed enum
  int gribSpeed = MapOcpnSpeedToGrib(um.GetWindSpeedUnit());
  settings.Settings[GribOverlaySettings::WIND].m_Units = gribSpeed;
  settings.Settings[GribOverlaySettings::WIND_GUST].m_Units = gribSpeed;

  // Map OpenCPN speed to Grib current enum (uses same mapping)
  int gribCurrent = MapOcpnSpeedToGrib(um.GetSpeedUnit());
  settings.Settings[GribOverlaySettings::CURRENT].m_Units = gribCurrent;

  // Temperature: Map OpenCPN temperature to Grib enum
  int gribTemp = MapOcpnTempToGrib(um.GetTemperatureUnit());
  settings.Settings[GribOverlaySettings::AIR_TEMPERATURE].m_Units = gribTemp;
  settings.Settings[GribOverlaySettings::SEA_TEMPERATURE].m_Units = gribTemp;

  // Wave height uses depth unit
  settings.Settings[GribOverlaySettings::WAVE].m_Units = MapOcpnDepthToGrib(um.GetDepthUnit());

  // Pressure: Map OpenCPN pressure to Grib enum
  int gribPressure = MapOcpnPressureToGrib(um.GetPressureUnit());
  settings.Settings[GribOverlaySettings::PRESSURE].m_Units = gribPressure;

  // Rainfall: Map OpenCPN rainfall to Grib enum
  int gribRainfall = MapOcpnRainfallToGrib(um.GetRainfallUnit());
  settings.Settings[GribOverlaySettings::PRECIPITATION].m_Units = gribRainfall;

  // DO NOT sync: CAPE, COMP_REFL, CLOUD, GEO_ALTITUDE, REL_HUMIDITY
  // These have grib-specific options that OpenCPN doesn't provide

  m_pGribCtrlBar->SetFactoryOptions(); // This will clear the cache AND request a refresh.
}

//----------------------------------------------------------------------------------------------------------
//          Visualization Feature Toggles Implementation
//----------------------------------------------------------------------------------------------------------

void DpGrib_pi::Internal_SetBarbedArrowsVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetBarbedArrowsVisible - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetBarbedArrowsVisible - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bBarbedArrows = visible;
    if (m_pGribCtrlBar->m_gCursorData) {
    m_pGribCtrlBar->m_gCursorData->ResolveDisplayConflicts(layerId);
  }
  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

void DpGrib_pi::Internal_SetIsoBarsVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetIsoBarsVisible - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetIsoBarsVisible - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bIsoBars = visible;
  if (m_pGribCtrlBar->m_gCursorData) {
    m_pGribCtrlBar->m_gCursorData->ResolveDisplayConflicts(layerId);
  }
  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

void DpGrib_pi::Internal_SetNumbersVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetNumbersVisible - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetNumbersVisible - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bNumbers = visible;
  if (m_pGribCtrlBar->m_gCursorData) {
    m_pGribCtrlBar->m_gCursorData->ResolveDisplayConflicts(layerId);
  }
  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

void DpGrib_pi::Internal_SetOverlayMapVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetOverlayMapVisible - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetOverlayMapVisible - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bOverlayMap = visible;
  if (m_pGribCtrlBar->m_gCursorData) {
    m_pGribCtrlBar->m_gCursorData->ResolveDisplayConflicts(layerId);
  }
  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

void DpGrib_pi::Internal_SetDirectionArrowsVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetDirectionArrowsVisible - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetDirectionArrowsVisible - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bDirectionArrows = visible;
  if (m_pGribCtrlBar->m_gCursorData) {
    m_pGribCtrlBar->m_gCursorData->ResolveDisplayConflicts(layerId);
  }
  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

void DpGrib_pi::Internal_SetParticlesVisible(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetParticlesVisible - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetParticlesVisible - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bParticles = visible;
  if (m_pGribCtrlBar->m_gCursorData) {
    m_pGribCtrlBar->m_gCursorData->ResolveDisplayConflicts(layerId);
  }
  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

//----------------------------------------------------------------------------------------------------------
//          Visualization State Getters Implementation
//----------------------------------------------------------------------------------------------------------

bool DpGrib_pi::Internal_IsBarbedArrowsVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_IsBarbedArrowsVisible - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_IsBarbedArrowsVisible - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bBarbedArrows;
}

bool DpGrib_pi::Internal_IsIsoBarsVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_IsIsoBarsVisible - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_IsIsoBarsVisible - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bIsoBars;
}

bool DpGrib_pi::Internal_AreNumbersVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_AreNumbersVisible - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_AreNumbersVisible - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bNumbers;
}

bool DpGrib_pi::Internal_IsOverlayMapVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_IsOverlayMapVisible - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_IsOverlayMapVisible - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bOverlayMap;
}

bool DpGrib_pi::Internal_AreDirectionArrowsVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_AreDirectionArrowsVisible - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_AreDirectionArrowsVisible - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bDirectionArrows;
}

bool DpGrib_pi::Internal_AreParticlesVisible(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_AreParticlesVisible - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_AreParticlesVisible - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bParticles;
}

//----------------------------------------------------------------------------------------------------------
//          Additional Visualization Settings Implementation
//----------------------------------------------------------------------------------------------------------

void DpGrib_pi::Internal_SetIsoBarVisibility(int layerId, bool visible) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetIsoBarVisibility - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetIsoBarVisibility - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_iIsoBarVisibility = visible;

  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

bool DpGrib_pi::Internal_GetIsoBarVisibility(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_GetIsoBarVisibility - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_GetIsoBarVisibility - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_iIsoBarVisibility;
}

void DpGrib_pi::Internal_SetAbbreviatedNumbers(int layerId, bool abbreviated) {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_SetAbbreviatedNumbers - m_pGribCtrlBar is null");
    return;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_SetAbbreviatedNumbers - Invalid layerId: %d", layerId);
    return;
  }

  GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  settings.Settings[layerId].m_bAbbrIsoBarsNumbers = abbreviated;

  // Persist the change
  settings.Write();

  // Clear cache and request refresh
  m_pGribCtrlBar->SetFactoryOptions();
}

bool DpGrib_pi::Internal_AreNumbersAbbreviated(int layerId) const {
  if (!m_pGribCtrlBar) {
    wxLogWarning("DpGrib_pi::Internal_AreNumbersAbbreviated - m_pGribCtrlBar is null");
    return false;
  }

  if (layerId < 0 || layerId >= GribOverlaySettings::SETTINGS_COUNT) {
    wxLogWarning("DpGrib_pi::Internal_AreNumbersAbbreviated - Invalid layerId: %d", layerId);
    return false;
  }

  const GribOverlaySettings& settings = m_pGribCtrlBar->m_OverlaySettings;
  return settings.Settings[layerId].m_bAbbrIsoBarsNumbers;
}

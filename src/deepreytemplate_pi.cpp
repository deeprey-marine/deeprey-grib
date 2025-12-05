/******************************************************************************
 * Project: Deeprey Template Plugin
 * Purpose: Reference plugin for Deeprey OpenCPN ecosystem
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *
 * This is a reference template demonstrating how to build a Deeprey ecosystem
 * plugin. Study this code and the accompanying header file to understand:
 * - OpenCPN plugin lifecycle (Init, DeInit, LateInit)
 * - Inter-plugin messaging pattern with deeprey-gui
 * - Chart canvas overlay rendering
 * - Configuration persistence
 *****************************************************************************/

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "deepreytemplate_pi.h"
#include "config.h"

// =============================================================================
// Plugin Factory Functions
// =============================================================================

/**
 * Factory function to create plugin instance.
 * Called by OpenCPN when loading the plugin.
 */
extern "C" DECL_EXP opencpn_plugin* create_pi(void* ppimgr) {
    return new deepreytemplate_pi(ppimgr);
}

/**
 * Factory function to destroy plugin instance.
 * Called by OpenCPN when unloading the plugin.
 */
extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p) {
    delete p;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

deepreytemplate_pi::deepreytemplate_pi(void* ppimgr)
    : opencpn_plugin_119(ppimgr)
    , m_pidc(nullptr)
    , m_pconfig(nullptr)
    , m_templateAPI(nullptr)
{
    // Initialize all image handlers for icon loading
    wxInitAllImageHandlers();

    // Load plugin panel icon (if you have one in data/icons/)
    // For now, create a simple placeholder bitmap
    m_panelBitmap = wxBitmap(32, 32);
}

deepreytemplate_pi::~deepreytemplate_pi() {
    // Cleanup is handled in DeInit()
}

// =============================================================================
// Plugin Lifecycle
// =============================================================================

int deepreytemplate_pi::Init(void) {
    // 1. Get OpenCPN's config object for settings persistence
    m_pconfig = GetOCPNConfigObject();

    // 2. Load saved configuration
    LoadConfig();

    // 3. Initialize drawing context for chart overlay rendering
    //    piDC provides cross-platform drawing on OpenGL canvases
    if (!m_pidc) {
        m_pidc = new piDC();
    }

    // 4. Create the public API instance
    //    This will be shared with deeprey-gui via plugin messaging
    m_templateAPI = new DpTemplate::DpTemplateAPI(&m_settings);

    // 5. Return capability flags
    //    These tell OpenCPN what features this plugin uses
    return (WANTS_CONFIG                    // Plugin uses OpenCPN config system
          | WANTS_LATE_INIT                 // Plugin needs LateInit() callback
          | WANTS_OPENGL_OVERLAY_CALLBACK   // Plugin renders on chart canvas
          | WANTS_PLUGIN_MESSAGING);        // Plugin uses inter-plugin messaging
}

bool deepreytemplate_pi::DeInit(void) {
    // 1. Save configuration before shutdown
    SaveConfig();

    // 2. Delete API and notify deeprey-gui that it's no longer available
    //    This is important: deeprey-gui may still hold a pointer to our API,
    //    so we send a message with nullptr to let it know to clear its pointer
    if (m_templateAPI) {
        delete m_templateAPI;
        m_templateAPI = nullptr;
        UpdateApiPtr();  // Send nullptr notification
    }

    // 3. Clean up drawing context
    if (m_pidc) {
        delete m_pidc;
        m_pidc = nullptr;
    }

    return true;
}

void deepreytemplate_pi::LateInit(void) {
    // LateInit is called after OpenCPN's main window is fully initialized.
    // This is the place for operations that depend on the UI being ready.
    //
    // For this minimal template, we don't need to do anything here,
    // but more complex plugins might:
    // - Access the AUI manager to add dockable panes
    // - Register with other plugins
    // - Initialize UI components that need the main window
}

// =============================================================================
// Plugin Metadata
// =============================================================================

int deepreytemplate_pi::GetAPIVersionMajor() {
    return atoi(API_VERSION);
}

int deepreytemplate_pi::GetAPIVersionMinor() {
    std::string v(API_VERSION);
    size_t dotpos = v.find('.');
    return atoi(v.substr(dotpos + 1).c_str());
}

int deepreytemplate_pi::GetPlugInVersionMajor() {
    return PLUGIN_VERSION_MAJOR;
}

int deepreytemplate_pi::GetPlugInVersionMinor() {
    return PLUGIN_VERSION_MINOR;
}

wxBitmap* deepreytemplate_pi::GetPlugInBitmap() {
    return &m_panelBitmap;
}

wxString deepreytemplate_pi::GetCommonName() {
    return _T("deepreytemplate");
}

wxString deepreytemplate_pi::GetShortDescription() {
    return _T("Deeprey Template Plugin");
}

wxString deepreytemplate_pi::GetLongDescription() {
    return _T("Reference template plugin for the Deeprey OpenCPN ecosystem. "
              "Demonstrates inter-plugin communication, chart overlay rendering, "
              "and configuration persistence.");
}

// =============================================================================
// Chart Canvas Overlay Rendering
// =============================================================================

bool deepreytemplate_pi::RenderGLOverlayMultiCanvas(
    wxGLContext* pcontext,
    PlugIn_ViewPort* vp,
    int canvasIndex,
    int priority)
{
    // OpenCPN calls this method multiple times per frame with different priorities.
    // Most overlays should render at priority 128 (standard overlay layer).
    if (priority != 128) {
        return false;
    }

    // Only render if enabled via the API
    if (!m_settings.m_enabled) {
        return true;
    }

    // Ensure we have a valid drawing context
    if (!m_pidc) {
        return false;
    }

    // Example: Draw a simple text overlay
    // This demonstrates the basic pattern for chart canvas rendering.
    //
    // In a real plugin, you would:
    // - Convert lat/lon coordinates to screen pixels using vp
    // - Draw your visualization (targets, routes, markers, etc.)

    // Position in center of canvas
    int x = vp->pix_width / 2;
    int y = vp->pix_height / 2;

    // Save current drawing state
    wxPen oldPen = m_pidc->GetPen();
    wxBrush oldBrush = m_pidc->GetBrush();

    // Configure drawing style
    m_pidc->SetPen(wxPen(wxColour(0, 100, 200), 2));
    m_pidc->SetBrush(wxBrush(wxColour(0, 100, 200, 128)));

    // Draw a simple marker
    int size = m_settings.m_parameter / 2;  // Size based on parameter setting
    m_pidc->DrawCircle(x, y, size);

    // Draw label
    wxString label = wxString::Format(_T("Template Plugin (param=%d)"), m_settings.m_parameter);
    m_pidc->SetTextForeground(wxColour(0, 100, 200));
    m_pidc->DrawText(label, x - 80, y + size + 10);

    // Restore drawing state
    m_pidc->SetPen(oldPen);
    m_pidc->SetBrush(oldBrush);

    return true;
}

// =============================================================================
// Inter-Plugin Communication
// =============================================================================

void deepreytemplate_pi::SetPluginMessage(wxString& message_id, wxString& message_body) {
    // Handle discovery request from deeprey-gui
    //
    // When deeprey-gui starts, it broadcasts discovery messages to find plugins.
    // When we receive our message ID, we respond with our API pointer.
    if (message_id == _T("DP_GUI_TO_TEMPLATE")) {
        UpdateApiPtr();
        return;
    }

    // Handle global settings update (optional)
    // deeprey-gui may broadcast theme changes or other global settings
    if (message_id == _T("GLOBAL_SETTINGS_UPDATED")) {
        // React to global settings change if needed
        // For example: update colors for night mode
        return;
    }

    // Ignore other messages (OpenCPN sends many messages for various purposes)
    // Common messages to ignore:
    // - "WMM_VARIATION_BOAT": Magnetic variation updates
    // - "OpenCPN Config": Config changes
    // - "OCPN_OPENGL_CONFIG": OpenGL config changes
}

void deepreytemplate_pi::UpdateApiPtr() {
    // Send our API pointer to deeprey-gui.
    //
    // Message format: Pointer value as string (base 10 unsigned long long)
    // When m_templateAPI is nullptr (during DeInit), this notifies deeprey-gui
    // to clear its cached pointer.
    wxString apiPtrStr = wxString::Format("%llu", (unsigned long long)m_templateAPI);
    SendPluginMessage("TEMPLATE_API_TO_DP_GUI", apiPtrStr);
}

// =============================================================================
// Configuration Persistence
// =============================================================================

bool deepreytemplate_pi::LoadConfig() {
    if (!m_pconfig) {
        return false;
    }

    m_pconfig->SetPath(CONFIG_SECTION);

    // Load settings with defaults
    m_pconfig->Read(wxT("Enabled"), &m_settings.m_enabled, false);
    m_pconfig->Read(wxT("Parameter"), &m_settings.m_parameter, 50);

    return true;
}

bool deepreytemplate_pi::SaveConfig() {
    if (!m_pconfig) {
        return false;
    }

    m_pconfig->SetPath(CONFIG_SECTION);

    // Save current settings
    m_pconfig->Write(wxT("Enabled"), m_settings.m_enabled);
    m_pconfig->Write(wxT("Parameter"), m_settings.m_parameter);

    return true;
}

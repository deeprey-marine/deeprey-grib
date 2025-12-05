/******************************************************************************
 * Project: Deeprey Template Plugin
 * Purpose: Reference plugin for Deeprey OpenCPN ecosystem
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *
 * This is a reference template demonstrating how to build a Deeprey ecosystem
 * plugin. It shows:
 * - OpenCPN plugin API integration
 * - Inter-plugin communication with deeprey-gui
 * - Chart canvas overlay rendering
 * - Configuration persistence
 *****************************************************************************/

#ifndef _DEEPREYTEMPLATE_PI_H_
#define _DEEPREYTEMPLATE_PI_H_

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/fileconf.h>

#include "ocpn_plugin.h"
#include "pidc.h"

#include "TemplateAPI.h"
#include "TemplatePersistentSettings.h"

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

/**
 * Main plugin class for the Deeprey Template plugin.
 *
 * Inherits from:
 * - opencpn_plugin_119: OpenCPN plugin API version 1.19
 *
 * This class demonstrates the minimal required structure for a Deeprey
 * ecosystem plugin that:
 * - Renders overlays on the chart canvas
 * - Communicates with deeprey-gui via plugin messaging
 * - Persists settings via OpenCPN's config system
 */
class deepreytemplate_pi : public opencpn_plugin_119 {
public:
    deepreytemplate_pi(void* ppimgr);
    ~deepreytemplate_pi();

    // =========================================================================
    // Required OpenCPN Plugin API Methods
    // =========================================================================

    /**
     * Initialize the plugin.
     * Called when OpenCPN loads the plugin.
     * @return Capability flags indicating what features the plugin uses
     */
    int Init(void);

    /**
     * Deinitialize the plugin.
     * Called when OpenCPN unloads the plugin.
     */
    bool DeInit(void);

    /**
     * Late initialization.
     * Called after the main OpenCPN window is fully initialized.
     * Use this for operations that depend on the main window being ready.
     */
    void LateInit(void);

    // Version information
    int GetAPIVersionMajor();
    int GetAPIVersionMinor();
    int GetPlugInVersionMajor();
    int GetPlugInVersionMinor();

    // Plugin metadata
    wxBitmap* GetPlugInBitmap();
    wxString GetCommonName();
    wxString GetShortDescription();
    wxString GetLongDescription();

    // =========================================================================
    // Optional OpenCPN Plugin API Overrides
    // =========================================================================

    /**
     * Render OpenGL overlay on chart canvas.
     *
     * OpenCPN calls this method when the chart canvas needs to be rendered.
     * This is the NATIVE way to draw on the chart canvas - no deeprey-api
     * overlay interface needed.
     *
     * @param pcontext OpenGL context
     * @param vp Current viewport information
     * @param canvasIndex Index of the canvas (for multi-canvas support)
     * @param priority Rendering priority (typically check for 128)
     * @return true if overlay was rendered
     */
    bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext,
                                     PlugIn_ViewPort* vp,
                                     int canvasIndex,
                                     int priority);

    /**
     * Handle inter-plugin messages.
     *
     * This is how deeprey-gui discovers and communicates with this plugin.
     * Message pattern:
     * - deeprey-gui sends "DP_GUI_TO_TEMPLATE" to request API pointer
     * - This plugin responds with "TEMPLATE_API_TO_DP_GUI" containing pointer
     *
     * @param message_id Message identifier
     * @param message_body Message content
     */
    void SetPluginMessage(wxString& message_id, wxString& message_body);

private:
    // =========================================================================
    // Configuration
    // =========================================================================

    static constexpr const char* CONFIG_SECTION = "/Plugins/DeepreyTemplate";

    bool LoadConfig();
    bool SaveConfig();

    // =========================================================================
    // Inter-Plugin Communication
    // =========================================================================

    /**
     * Send API pointer to deeprey-gui.
     * Called in response to discovery request, and when plugin is deinitialized
     * (sends nullptr to notify that API is no longer available).
     */
    void UpdateApiPtr();

    // =========================================================================
    // Member Variables
    // =========================================================================

    wxBitmap m_panelBitmap;                      // Plugin icon
    piDC* m_pidc;                                // Drawing context for overlays
    wxFileConfig* m_pconfig;                     // OpenCPN config object

    TemplatePlugin::TemplatePersistentSettings m_settings;  // Plugin settings
    TemplatePlugin::TemplateAPI* m_templateAPI;             // Public API instance
};

#endif // _DEEPREYTEMPLATE_PI_H_

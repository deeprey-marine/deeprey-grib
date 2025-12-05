/******************************************************************************
 * Project: Deeprey Template Plugin
 * Purpose: Reference plugin for Deeprey OpenCPN ecosystem
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *****************************************************************************/

#ifndef TEMPLATE_API_H
#define TEMPLATE_API_H

#include <functional>
#include <unordered_map>
#include <cstdint>

#include "TemplatePersistentSettings.h"

namespace TemplatePlugin {

/**
 * Public API for the Template plugin.
 *
 * This class exposes plugin functionality to deeprey-gui (and other consumers).
 * It follows the Facade pattern, providing a clean interface without exposing
 * internal implementation details.
 *
 * Key patterns demonstrated:
 * - State control methods (SetEnabled/IsEnabled)
 * - Callback registration with ID-based management
 * - Settings access for persistence
 *
 * NOTE: This API does NOT include overlay rendering methods because
 * chart canvas rendering is handled directly by OpenCPN via the
 * RenderGLOverlayMultiCanvas() callback in the plugin class.
 * The ISonarOverlayView-style interface is only needed for plugins
 * that have their own wxWindow canvases (like sonar's NavFrame).
 */
class TemplateAPI {
public:
    /**
     * Constructor.
     * @param settings Pointer to the persistent settings struct (owned by plugin)
     */
    TemplateAPI(TemplatePersistentSettings* settings);

    ~TemplateAPI();

    // =========================================================================
    // State Control
    // =========================================================================

    /**
     * Enable or disable the plugin's overlay rendering.
     * When disabled, RenderGLOverlayMultiCanvas() will skip drawing.
     */
    void SetEnabled(bool enabled);

    /**
     * Check if overlay rendering is enabled.
     */
    bool IsEnabled() const;

    /**
     * Set the example parameter value (0-100).
     */
    void SetParameter(int value);

    /**
     * Get the current parameter value.
     */
    int GetParameter() const;

    // =========================================================================
    // Callback Management
    // =========================================================================

    /**
     * Register a callback to be notified when plugin state changes.
     *
     * This pattern allows multiple listeners (e.g., different UI components
     * in deeprey-gui) to react to state changes without tight coupling.
     *
     * @param callback Function to call when state changes
     * @return Unique callback ID for later removal
     */
    uint64_t AddStateChangedCallback(std::function<void()> callback);

    /**
     * Remove a previously registered callback.
     * @param callbackId ID returned from AddStateChangedCallback()
     */
    void RemoveStateChangedCallback(uint64_t callbackId);

    // =========================================================================
    // Settings Access
    // =========================================================================

    /**
     * Get direct access to settings struct.
     * Used by deeprey-gui to read/write settings for UI binding.
     */
    TemplatePersistentSettings* GetSettings();

private:
    TemplatePersistentSettings* m_settings;

    // Callback infrastructure
    std::unordered_map<uint64_t, std::function<void()>> m_stateCallbacks;
    uint64_t m_nextCallbackId = 1;

    /**
     * Notify all registered callbacks that state has changed.
     */
    void NotifyStateChanged();
};

} // namespace TemplatePlugin

#endif // TEMPLATE_API_H

/******************************************************************************
 * Project: Deeprey Template Plugin
 * Purpose: Reference plugin for Deeprey OpenCPN ecosystem
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *****************************************************************************/

#ifndef TEMPLATE_PERSISTENT_SETTINGS_H
#define TEMPLATE_PERSISTENT_SETTINGS_H

namespace TemplatePlugin {

/**
 * Shared settings structure for the template plugin.
 *
 * This struct is shared between:
 * - The plugin itself (for internal state)
 * - The TemplateAPI (for exposing settings to deeprey-gui)
 *
 * Settings are persisted via OpenCPN's wxFileConfig system.
 */
struct TemplatePersistentSettings {
    // Whether the plugin overlay rendering is enabled
    bool m_enabled = false;

    // Example parameter (0-100) - demonstrates a configurable value
    int m_parameter = 50;
};

} // namespace TemplatePlugin

#endif // TEMPLATE_PERSISTENT_SETTINGS_H

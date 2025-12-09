/******************************************************************************
 * Project: Deeprey Grib Plugin
 * Purpose: GRIB weather data visualization plugin for OpenCPN
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *****************************************************************************/

#include <GL/glew.h>
#include "DpGribAPI.h"
#include "DpGrib_pi.h"

namespace DpGrib {

// =============================================================================
// Constructor / Destructor
// =============================================================================

DpGribAPI::DpGribAPI(DpGribPersistentSettings* settings, void* plugin)
    : m_settings(settings)
    , m_plugin(plugin)
    , m_nextCallbackId(1)
{
}

DpGribAPI::~DpGribAPI() {
    // Clear all callbacks on destruction
    m_stateCallbacks.clear();
}

// =============================================================================
// State Control
// =============================================================================

void DpGribAPI::SetEnabled(bool enabled) {
    if (m_settings->m_enabled != enabled) {
        m_settings->m_enabled = enabled;
        NotifyStateChanged();
    }
}

bool DpGribAPI::IsEnabled() const {
    return m_settings->m_enabled;
}

void DpGribAPI::SetParameter(int value) {
    // Clamp value to valid range
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    if (m_settings->m_parameter != value) {
        m_settings->m_parameter = value;
        NotifyStateChanged();
    }
}

int DpGribAPI::GetParameter() const {
    return m_settings->m_parameter;
}

// =============================================================================
// Visibility Control
// =============================================================================

void DpGribAPI::SetVisible(bool visible) {
    if (m_plugin) {
        static_cast<DpGrib_pi*>(m_plugin)->Internal_SetVisible(visible);
    }
}

bool DpGribAPI::IsVisible() const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_IsVisible();
    }
    return false;
}

// =============================================================================
// Callback Management
// =============================================================================

uint64_t DpGribAPI::AddStateChangedCallback(std::function<void()> callback) {
    uint64_t id = m_nextCallbackId++;
    m_stateCallbacks[id] = std::move(callback);
    return id;
}

void DpGribAPI::RemoveStateChangedCallback(uint64_t callbackId) {
    m_stateCallbacks.erase(callbackId);
}

void DpGribAPI::NotifyStateChanged() {
    // Call all registered callbacks
    // Make a copy of the callback map in case a callback modifies it
    auto callbacks = m_stateCallbacks;
    for (auto& [id, callback] : callbacks) {
        if (callback) {
            callback();
        }
    }
}

// =============================================================================
// Settings Access
// =============================================================================

DpGribPersistentSettings* DpGribAPI::GetSettings() {
    return m_settings;
}

} // namespace DpGrib

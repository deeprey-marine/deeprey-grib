/******************************************************************************
 * Project: Deeprey Template Plugin
 * Purpose: Reference plugin for Deeprey OpenCPN ecosystem
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *****************************************************************************/

#include "TemplateAPI.h"

namespace TemplatePlugin {

// =============================================================================
// Constructor / Destructor
// =============================================================================

TemplateAPI::TemplateAPI(TemplatePersistentSettings* settings)
    : m_settings(settings)
    , m_nextCallbackId(1)
{
}

TemplateAPI::~TemplateAPI() {
    // Clear all callbacks on destruction
    m_stateCallbacks.clear();
}

// =============================================================================
// State Control
// =============================================================================

void TemplateAPI::SetEnabled(bool enabled) {
    if (m_settings->m_enabled != enabled) {
        m_settings->m_enabled = enabled;
        NotifyStateChanged();
    }
}

bool TemplateAPI::IsEnabled() const {
    return m_settings->m_enabled;
}

void TemplateAPI::SetParameter(int value) {
    // Clamp value to valid range
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    if (m_settings->m_parameter != value) {
        m_settings->m_parameter = value;
        NotifyStateChanged();
    }
}

int TemplateAPI::GetParameter() const {
    return m_settings->m_parameter;
}

// =============================================================================
// Callback Management
// =============================================================================

uint64_t TemplateAPI::AddStateChangedCallback(std::function<void()> callback) {
    uint64_t id = m_nextCallbackId++;
    m_stateCallbacks[id] = std::move(callback);
    return id;
}

void TemplateAPI::RemoveStateChangedCallback(uint64_t callbackId) {
    m_stateCallbacks.erase(callbackId);
}

void TemplateAPI::NotifyStateChanged() {
    // Call all registered callbacks
    // Use a copy of the map in case a callback modifies the map
    for (auto& [id, callback] : m_stateCallbacks) {
        if (callback) {
            callback();
        }
    }
}

// =============================================================================
// Settings Access
// =============================================================================

TemplatePersistentSettings* TemplateAPI::GetSettings() {
    return m_settings;
}

} // namespace TemplatePlugin

/******************************************************************************
 * Project: Deeprey Template Plugin
 * Purpose: Reference plugin for Deeprey OpenCPN ecosystem
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *****************************************************************************/

#include "DpTemplateAPI.h"

namespace DpTemplate {

// =============================================================================
// Constructor / Destructor
// =============================================================================

DpTemplateAPI::DpTemplateAPI(DpTemplatePersistentSettings* settings)
    : m_settings(settings)
    , m_nextCallbackId(1)
{
}

DpTemplateAPI::~DpTemplateAPI() {
    // Clear all callbacks on destruction
    m_stateCallbacks.clear();
}

// =============================================================================
// State Control
// =============================================================================

void DpTemplateAPI::SetEnabled(bool enabled) {
    if (m_settings->m_enabled != enabled) {
        m_settings->m_enabled = enabled;
        NotifyStateChanged();
    }
}

bool DpTemplateAPI::IsEnabled() const {
    return m_settings->m_enabled;
}

void DpTemplateAPI::SetParameter(int value) {
    // Clamp value to valid range
    if (value < 0) value = 0;
    if (value > 100) value = 100;

    if (m_settings->m_parameter != value) {
        m_settings->m_parameter = value;
        NotifyStateChanged();
    }
}

int DpTemplateAPI::GetParameter() const {
    return m_settings->m_parameter;
}

// =============================================================================
// Callback Management
// =============================================================================

uint64_t DpTemplateAPI::AddStateChangedCallback(std::function<void()> callback) {
    uint64_t id = m_nextCallbackId++;
    m_stateCallbacks[id] = std::move(callback);
    return id;
}

void DpTemplateAPI::RemoveStateChangedCallback(uint64_t callbackId) {
    m_stateCallbacks.erase(callbackId);
}

void DpTemplateAPI::NotifyStateChanged() {
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

DpTemplatePersistentSettings* DpTemplateAPI::GetSettings() {
    return m_settings;
}

} // namespace DpTemplate

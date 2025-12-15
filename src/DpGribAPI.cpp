/******************************************************************************
 * Project: Deeprey Grib Plugin
 * Purpose: GRIB weather data visualization plugin for OpenCPN
 * Author: Deeprey Research Ltd
 *
 * Copyright (C) 2024 Deeprey Research Ltd
 * All Rights Reserved
 *****************************************************************************/

#include <GL/glew.h>
#include <cmath>
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
    m_dataChangedCallbacks.clear();
    m_progressCallbacks.clear();
    m_cursorCallbacks.clear();
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

uint64_t DpGribAPI::AddDataChangedCallback(std::function<void()> callback) {
    uint64_t id = m_nextCallbackId++;
    m_dataChangedCallbacks[id] = std::move(callback);
    return id;
}

void DpGribAPI::RemoveDataChangedCallback(uint64_t callbackId) {
    m_dataChangedCallbacks.erase(callbackId);
}

void DpGribAPI::NotifyDataChanged() {
    // Call all registered data changed callbacks
    // Make a copy of the callback map in case a callback modifies it
    auto callbacks = m_dataChangedCallbacks;
    for (auto& [id, callback] : callbacks) {
        if (callback) {
            callback();
        }
    }
}

// =============================================================================
// Download Control
// =============================================================================

void DpGribAPI::StartWorldDownload(double latMin, double lonMin, double latMax, 
                                   double lonMax, int durationHours) {
    if (m_plugin) {
        static_cast<DpGrib_pi*>(m_plugin)->Internal_StartWorldDownload(
            latMin, lonMin, latMax, lonMax, durationHours);
    }
}

// =============================================================================
// Settings Access
// =============================================================================

DpGribPersistentSettings* DpGribAPI::GetSettings() {
    return m_settings;
}

// =============================================================================
// Download State
// =============================================================================

bool DpGribAPI::IsDownloading() const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_IsDownloading();
    }
    return false;
}

void DpGribAPI::CancelDownload() {
    if (m_plugin) {
        static_cast<DpGrib_pi*>(m_plugin)->Internal_CancelDownload();
    }
}

// =============================================================================
// Timeline / GRIB Record Management
// =============================================================================

int DpGribAPI::GetTimeStepCount() const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetTimeStepCount();
    }
    return 0;
}

int DpGribAPI::GetCurrentTimeIndex() const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetCurrentTimeIndex();
    }
    return -1;
}

bool DpGribAPI::SetTimeIndex(int index) {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_SetTimeIndex(index);
    }
    return false;
}

bool DpGribAPI::SetDisplayToCurrentTime() {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_SetDisplayToCurrentTime();
    }
    return false;
}

wxString DpGribAPI::GetCurrentTimeString() const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetCurrentTimeString();
    }
    return wxEmptyString;
}

wxString DpGribAPI::GetTimeString(int index) const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetTimeString(index);
    }
    return wxEmptyString;
}

wxString DpGribAPI::GetCurrentTimeStringLocal() const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetCurrentTimeStringLocal();
    }
    return wxEmptyString;
}

wxString DpGribAPI::GetTimeStringLocal(int index) const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetTimeStringLocal(index);
    }
    return wxEmptyString;
}

// =============================================================================
// Layer / Data Field Management
// =============================================================================

bool DpGribAPI::SetLayerVisible(int layerId, bool visible) {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_SetLayerVisible(layerId, visible);
    }
    return false;
}

bool DpGribAPI::IsLayerVisible(int layerId) const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_IsLayerVisible(layerId);
    }
    return false;
}

bool DpGribAPI::IsLayerAvailable(int layerId) const {
    if (m_plugin) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_IsLayerAvailable(layerId);
    }
    return false;
}

wxString DpGribAPI::GetLayerValueAtPoint(int layerId, double latitude, double longitude) const {
    // Safety check: Return placeholder if no timesteps are available
    // This prevents crashes during download initialization (reentrancy bug)
    if (m_plugin && GetTimeStepCount() > 0) {
        return static_cast<DpGrib_pi*>(m_plugin)->Internal_GetLayerValueAtPoint(layerId, latitude, longitude);
    }
    return wxString(_T("--"));
}

wxString DpGribAPI::GetLayerValueAtCursor(int layerId) const {
    // Dummy: return placeholder; real implementation would read last cursor position
    if (std::isnan(m_lastCursorLat) || std::isnan(m_lastCursorLon))
        return wxString(_T("--"));
    return GetLayerValueAtPoint(layerId, m_lastCursorLat, m_lastCursorLon);
}

// =============================================================================
// Cursor Position Callbacks
// =============================================================================

uint64_t DpGribAPI::AddCursorPositionCallback(CursorPositionCallback callback) {
    uint64_t id = m_nextCallbackId++;
    m_cursorCallbacks[id] = std::move(callback);
    return id;
}

void DpGribAPI::RemoveCursorPositionCallback(uint64_t callbackId) {
    m_cursorCallbacks.erase(callbackId);
}

void DpGribAPI::NotifyCursorPosition(double latitude, double longitude) {
    m_lastCursorLat = latitude;
    m_lastCursorLon = longitude;
    auto callbacks = m_cursorCallbacks;
    for (auto &kv : callbacks) {
        auto &callback = kv.second;
        if (callback) callback(latitude, longitude);
    }
}

// =============================================================================
// Download Progress Callbacks
// =============================================================================

uint64_t DpGribAPI::AddDownloadProgressCallback(DownloadProgressCallback callback) {
    uint64_t id = m_nextCallbackId++;
    m_progressCallbacks[id] = std::move(callback);
    return id;
}

void DpGribAPI::RemoveDownloadProgressCallback(uint64_t callbackId) {
    m_progressCallbacks.erase(callbackId);
}

void DpGribAPI::NotifyDownloadProgress(long transferred, long total,
                                        bool completed, bool success) {
    // Make a copy in case a callback modifies the map
    auto callbacks = m_progressCallbacks;
    for (auto& [id, callback] : callbacks) {
        if (callback) {
            callback(transferred, total, completed, success);
        }
    }
}

} // namespace DpGrib

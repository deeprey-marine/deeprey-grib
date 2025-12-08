#
# Plugin.cmake - Plugin metadata and source organization
#
# This file defines plugin metadata, source files, and build options.
# It is included by CMakeLists.txt during the build process.
#

# ============================================================================
# Repository Options (for CloudSmith deployment - optional)
# ============================================================================

set(OCPN_TEST_REPO
    "opencpn/deeprey-alpha"
    CACHE STRING "Default repository for untagged builds"
)
set(OCPN_BETA_REPO
    "opencpn/deeprey-beta"
    CACHE STRING "Default repository for tagged builds matching 'beta'"
)
set(OCPN_RELEASE_REPO
    "opencpn/deeprey-prod"
    CACHE STRING "Default repository for tagged builds not matching 'beta'"
)

# ============================================================================
# Plugin Metadata
# ============================================================================

set(PKG_NAME deepreytemplate_pi)
set(PKG_VERSION 1.0.0.0)
set(PKG_PRERELEASE "")  # Empty, or a tag like 'beta'

set(DISPLAY_NAME "deepreytemplate")      # Dialogs, installer artifacts
set(PLUGIN_API_NAME "deepreytemplate")   # As of GetCommonName() in plugin API
set(PKG_SUMMARY "Deeprey Template Plugin")
set(PKG_DESCRIPTION [=[
Reference template plugin for the Deeprey OpenCPN ecosystem.
Demonstrates inter-plugin communication, chart overlay rendering,
and configuration persistence.
]=])

set(PKG_AUTHOR "Deeprey Research Ltd")
set(PKG_IS_OPEN_SOURCE "no")
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/)
set(PKG_INFO_URL https://opencpn.org/OpenCPN/plugins/)

# ============================================================================
# Source Files
# ============================================================================

# API files (shared interface for deeprey-gui)
set(API_SOURCES
    src/DpGribAPI.cpp
)
set(API_HEADERS
    deeprey-api/grib/DpGribAPI.h
    deeprey-api/grib/DpGribPersistentSettings.h
)

# Core plugin files
set(CORE_SOURCES
    src/DpGrib_pi.cpp
    src/GribOverlayFactory.cpp
    src/GribReader.cpp
    src/GribRecord.cpp
    src/GribV1Record.cpp
    src/GribV2Record.cpp
    src/GribUIDialog.cpp
    src/GribUIDialogBase.cpp
    src/GribRequestDialog.cpp
    src/GribSettingsDialog.cpp
    src/GribTable.cpp
    src/CustomGrid.cpp
    src/CursorData.cpp
    src/IsoLine.cpp
    src/XyGribPanel.cpp
    src/XyGribModelDef.cpp
    src/email.cpp
    src/GrabberWin.cpp
    src/zuFile.cpp
)

set(CORE_HEADERS
    include/DpGrib_pi.h
    include/GribOverlayFactory.h
    include/GribReader.h
    include/GribRecord.h
    include/GribRecordSet.h
    include/GribV1Record.h
    include/GribV2Record.h
    include/GribUIDialog.h
    include/GribUIDialogBase.h
    include/GribRequestDialog.h
    include/GribSettingsDialog.h
    include/GribTable.h
    include/CustomGrid.h
    include/CursorData.h
    include/IsoLine.h
    include/XyGribPanel.h
    include/XyGribModelDef.h
    include/email.h
    include/GrabberWin.h
    include/zuFile.h
    include/icons.h
    include/msg.h
)

# OpenGL/Drawing files
set(GL_SOURCES
    src/pi_ocpndc.cpp
    src/pi_shaders.cpp
    src/pi_TexFont.cpp
    src/icons.cpp
)

set(GL_HEADERS
    include/pi_ocpndc.h
    include/pi_shaders.h
    include/pi_TexFont.h
    include/pi_gl.h
    include/linmath.h
)

# Utility files
set(UTIL_SOURCES
    src/smapi.cpp
)

set(UTIL_HEADERS
    include/smapi.h
    include/version.h
)

# ============================================================================
# Source Groups (for IDE organization)
# ============================================================================

source_group("API\\Source" FILES ${API_SOURCES})
source_group("API\\Headers" FILES ${API_HEADERS})
source_group("Core\\Source" FILES ${CORE_SOURCES})
source_group("Core\\Headers" FILES ${CORE_HEADERS})
source_group("OpenGL\\Source" FILES ${GL_SOURCES})
source_group("OpenGL\\Headers" FILES ${GL_HEADERS})
source_group("Utilities\\Source" FILES ${UTIL_SOURCES})
source_group("Utilities\\Headers" FILES ${UTIL_HEADERS})

# ============================================================================
# Final Source Lists
# ============================================================================

set(SRC
    ${CORE_SOURCES}
    ${API_SOURCES}
    ${GL_SOURCES}
    ${UTIL_SOURCES}
)

set(HEADERS
    ${CORE_HEADERS}
    ${API_HEADERS}
    ${GL_HEADERS}
    ${UTIL_HEADERS}
)

# ============================================================================
# Build Configuration
# ============================================================================

add_definitions("-DocpnUSE_GL")

if (MSVC)
    # Enable parallel builds on MSVC
    target_compile_options(${PACKAGE_NAME} PRIVATE /MP)
endif ()

# OpenCPN API version to use
set(PKG_API_LIB api-21)

# ============================================================================
# Macros
# ============================================================================

macro(late_init)
    # Perform initialization after the PACKAGE_NAME library, compilers
    # and ocpn::api is available.
    # Add any post-initialization steps here.
endmacro()

macro(add_plugin_libraries)
    # Add libraries required by this plugin
    
    # Enable CMP0079 to allow linking to targets from other directories
    cmake_policy(SET CMP0079 NEW)

    # GLEW for OpenGL extensions - Set up BEFORE plugin_dc to ensure proper header order
    if (CMAKE_HOST_WIN32)
        add_subdirectory("${CMAKE_SOURCE_DIR}/libs/glew/build/cmake" EXCLUDE_FROM_ALL)
        set(GLEW_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/libs/glew/include")
        set(GLEW_LIBRARY glew)
        target_link_libraries(${PACKAGE_NAME} glew)
        target_include_directories(${PACKAGE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/libs/glew/include)
    elseif (NOT APPLE)
        find_package(GLEW REQUIRED)
        set(GLEW_INCLUDE_DIR ${GLEW_INCLUDE_DIRS})
        set(GLEW_LIBRARY ${GLEW_LIBRARIES})
        target_include_directories(${PACKAGE_NAME} PRIVATE ${GLEW_INCLUDE_DIRS})
        target_link_libraries(${PACKAGE_NAME} ${GLEW_LIBRARIES})
    endif ()

    # plugin_dc provides piDC for cross-platform drawing
    add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc")
    target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)
    
    # Inject GLEW into the submodule's dc_utils target to fix header ordering
    # This ensures GLEW is included before wx/glcanvas.h without modifying submodule
    if (TARGET _DC_UTILS AND NOT APPLE)
        # Add GLEW include directories with highest priority
        target_include_directories(_DC_UTILS BEFORE PRIVATE ${GLEW_INCLUDE_DIR})
        
        # Force include a minimal GLEW wrapper that loads GLEW first
        if (CMAKE_HOST_WIN32)
            target_compile_options(_DC_UTILS PRIVATE 
                /FI"${CMAKE_SOURCE_DIR}/include/glew_force_include.h"
            )
        else()
            target_compile_options(_DC_UTILS PRIVATE 
                -include "${CMAKE_SOURCE_DIR}/include/glew_force_include.h"
            )
        endif()
        
        # Link GLEW to the submodule target
        target_link_libraries(_DC_UTILS PRIVATE ${GLEW_LIBRARY})
    endif ()

    # wxJSON for JSON parsing
    add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxJSON")
    target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)
endmacro()

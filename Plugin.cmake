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
    src/DpTemplateAPI.cpp
)
set(API_HEADERS
    deeprey-api/template/DpTemplateAPI.h
    deeprey-api/template/DpTemplatePersistentSettings.h
)

# Core plugin files
set(CORE_SOURCES
    src/deepreytemplate_pi.cpp
)
set(CORE_HEADERS
    include/deepreytemplate_pi.h
)

# ============================================================================
# Source Groups (for IDE organization)
# ============================================================================

source_group("API\\Source" FILES ${API_SOURCES})
source_group("API\\Headers" FILES ${API_HEADERS})
source_group("Core\\Source" FILES ${CORE_SOURCES})
source_group("Core\\Headers" FILES ${CORE_HEADERS})

# ============================================================================
# Final Source Lists
# ============================================================================

set(SRC
    ${CORE_SOURCES}
    ${API_SOURCES}
)

set(HEADERS
    ${CORE_HEADERS}
    ${API_HEADERS}
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
set(PKG_API_LIB api-19)

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

    # plugin_dc provides piDC for cross-platform drawing
    add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc")
    target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

    # wxJSON for JSON parsing (optional, uncomment if needed)
    # add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxJSON")
    # target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)

    # GLEW for OpenGL extensions
    if (CMAKE_HOST_WIN32)
        add_subdirectory("${CMAKE_SOURCE_DIR}/libs/glew/build/cmake" EXCLUDE_FROM_ALL)
        target_link_libraries(${PACKAGE_NAME} glew)
        target_include_directories(${PACKAGE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/libs/glew/include)
    elseif (NOT APPLE)
        find_package(GLEW REQUIRED)
        target_include_directories(${PACKAGE_NAME} PRIVATE ${GLEW_INCLUDE_DIRS})
        target_link_libraries(${PACKAGE_NAME} ${GLEW_LIBRARIES})
    endif ()
endmacro()

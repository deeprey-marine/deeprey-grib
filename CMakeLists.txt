#
# CMakeLists.txt - Main build configuration for Deeprey Template Plugin
#
# Build instructions:
#   mkdir build && cd build
#   cmake ..
#   cmake --build . --config Release --target tarball
#

# ============================================================================
# CMake Setup
# ============================================================================

cmake_minimum_required(VERSION 3.12.0)
include(${CMAKE_SOURCE_DIR}/cmake/CmakeSetup.cmake NO_POLICY_SCOPE)
message(STATUS "Cmake version: ${CMAKE_VERSION}.")

# ============================================================================
# Build Setup
# ============================================================================

include(PluginOptions)
if (BUILD_TYPE)
    message(STATUS "Building: ${BUILD_TYPE}")
else ()
    message(STATUS "Configuring")
endif ()

# ============================================================================
# Plugin Setup
# ============================================================================

include(Plugin.cmake)

# ============================================================================
# Project Definition
# ============================================================================

project(${PKG_NAME} VERSION ${PKG_VERSION})
include(PluginCompiler)

add_library(${CMAKE_PROJECT_NAME} SHARED EXCLUDE_FROM_ALL ${SRC} ${HEADERS})

# Deploy plugin on build (Windows, optional)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND DEPLOY_ON_BUILD)
    add_custom_command(
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND copy "$(TargetPath)" "$(LOCALAPPDATA)\\opencpn\\plugins\\$(TargetFileName)"
        COMMENT "Deploy to import location"
        VERBATIM
    )
endif()

# ============================================================================
# Include Directories
# ============================================================================

include_directories(
    BEFORE
    ${CMAKE_SOURCE_DIR}/include
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deeprey-api/grib")

# ============================================================================
# Link OpenCPN API
# ============================================================================

add_subdirectory("opencpn-libs/${PKG_API_LIB}")
target_link_libraries(${CMAKE_PROJECT_NAME} ocpn::api)

# ============================================================================
# Version Setup
# ============================================================================

set(VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION_PATCH ${PROJECT_VERSION_PATCH})
if (PROJECT_VERSION_TWEAK STREQUAL "")
    set(PROJECT_VERSION_TWEAK 0)
endif ()

set(PACKAGE_NAME ${CMAKE_PROJECT_NAME})

# Create the minimum ocpn version id string like ov50
string(REGEX REPLACE "\([0-9]\)\.\([0-9]\).*" "ov\\1\\2"
       OCPN_MIN_VERSION ${API_OCPN_MIN_VERSION})

if (COMMAND late_init)
    late_init()
endif ()

# ============================================================================
# Manifest and Targets
# ============================================================================

# Configure the flatpak manifest (if present)
file(
    GLOB manifest ${PROJECT_SOURCE_DIR}/flatpak/org.opencpn.OpenCPN.Plugin.*.yaml
)
include(ConfigureManifest)
configure_manifest(${manifest} _new_manifest)

# Set up targets (flatpak, android, tarball)
include(Targets)
create_targets(${_new_manifest})

if ("${BUILD_TYPE}" STREQUAL "")
    return ()
endif ()

# ============================================================================
# Platform-Specific Setup
# ============================================================================

if (NOT ${BUILD_TYPE} STREQUAL "flatpak")
    if (APPLE)
        include(MacosWxwidgets)
    endif ()
    include(PluginInstall)
    if (QT_ANDROID)
        include(AndroidLibs)
    else ()
        include(PluginLibs)
    endif ()
    include(PluginLocalization)

    include_directories(BEFORE ${CMAKE_BINARY_DIR}/include)

    if (COMMAND add_plugin_libraries)
        add_plugin_libraries()
    endif ()
endif ()

# ============================================================================
# Configure Generated Files
# ============================================================================

configure_file(
    ${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_BINARY_DIR}/include/config.h
    @ONLY
)

# Configure upload scripts (optional, for CloudSmith deployment)
if (EXISTS ${CMAKE_SOURCE_DIR}/ci/upload.sh.in)
    configure_file(
        ${CMAKE_SOURCE_DIR}/ci/upload.sh.in ${CMAKE_BINARY_DIR}/upload.sh
        @ONLY
    )
endif ()

if (EXISTS ${CMAKE_SOURCE_DIR}/ci/upload.bat.in)
    configure_file(
        ${CMAKE_SOURCE_DIR}/ci/upload.bat.in ${CMAKE_BINARY_DIR}/upload.bat
        @ONLY
    )
endif ()

# Configure XML metadata file (optional)
if (EXISTS ${CMAKE_SOURCE_DIR}/plugin.xml.in)
    set(checksum "@checksum@")
    configure_file(
        ${CMAKE_SOURCE_DIR}/plugin.xml.in
        ${CMAKE_BINARY_DIR}/${pkg_displayname}.xml.in
        @ONLY
    )
endif ()

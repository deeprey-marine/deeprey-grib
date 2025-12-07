#!/bin/bash

# --- CONFIGURATION ---
SOURCE_DIR="/home/houssemhammami/OpenCPN/plugins/grib_pi"
DEST_DIR="/home/houssemhammami/deeprey-template"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Starting Migration from grib_pi to Deeprey Template structure...${NC}"

# 1. Prepare Directories
echo -e "1. Creating directory structure in ${DEST_DIR}..."
mkdir -p "${DEST_DIR}/include"
mkdir -p "${DEST_DIR}/src"
mkdir -p "${DEST_DIR}/libs"
mkdir -p "${DEST_DIR}/data"
mkdir -p "${DEST_DIR}/manual"
mkdir -p "${DEST_DIR}/po"

# 2. Copy Assets (Data, Manual, PO)
echo -e "2. Copying Assets..."
cp -r "${SOURCE_DIR}/data/"* "${DEST_DIR}/data/"
cp -r "${SOURCE_DIR}/manual/"* "${DEST_DIR}/manual/"
cp -r "${SOURCE_DIR}/po/"* "${DEST_DIR}/po/"

# 3. Move Libraries
echo -e "3. Moving Libraries (jasper and bzip2)..."
# Copy jasper
cp -r "${SOURCE_DIR}/libs/jasper" "${DEST_DIR}/libs/"
# Move bzip2 from src to libs
cp -r "${SOURCE_DIR}/src/bzip2" "${DEST_DIR}/libs/"

# 4. Separate Headers and Source Files
echo -e "4. Splitting .h to include/ and .cpp to src/..."

# Copy all .h files to include
find "${SOURCE_DIR}/src" -maxdepth 1 -name "*.h" -exec cp {} "${DEST_DIR}/include/" \;

# Copy all .cpp (and .c if any exist in root src) to src
find "${SOURCE_DIR}/src" -maxdepth 1 -name "*.cpp" -exec cp {} "${DEST_DIR}/src/" \;
find "${SOURCE_DIR}/src" -maxdepth 1 -name "*.c" -exec cp {} "${DEST_DIR}/src/" \;

# 5. Rename Main Plugin Files
echo -e "5. Renaming grib_pi files to DpGrib_pi..."
if [ -f "${DEST_DIR}/include/grib_pi.h" ]; then
    mv "${DEST_DIR}/include/grib_pi.h" "${DEST_DIR}/include/DpGrib_pi.h"
fi

if [ -f "${DEST_DIR}/src/grib_pi.cpp" ]; then
    mv "${DEST_DIR}/src/grib_pi.cpp" "${DEST_DIR}/src/DpGrib_pi.cpp"
fi

# 6. Create API Stubs (if they don't exist)
echo -e "6. Creating DpGribAPI stubs..."
if [ ! -f "${DEST_DIR}/include/DpGribAPI.h" ]; then
    cat <<EOT >> "${DEST_DIR}/include/DpGribAPI.h"
#ifndef DPGRIBAPI_H
#define DPGRIBAPI_H
#include "wx/wx.h"
class DpGrib_pi; // Forward declaration
namespace DpGrib {
class DpGribAPI {
public:
    DpGribAPI(DpGrib_pi* plugin);
    void TestConnection();
    void StartWorldDownload(double latMin, double lonMin, double latMax, double lonMax);
private:
    DpGrib_pi* m_plugin;
};
}
#endif
EOT
fi

if [ ! -f "${DEST_DIR}/src/DpGribAPI.cpp" ]; then
    cat <<EOT >> "${DEST_DIR}/src/DpGribAPI.cpp"
#include "DpGribAPI.h"
#include "DpGrib_pi.h"
namespace DpGrib {
DpGribAPI::DpGribAPI(DpGrib_pi* plugin) : m_plugin(plugin) {}
void DpGribAPI::TestConnection() { wxLogMessage("DpGribAPI::TestConnection Success"); }
void DpGribAPI::StartWorldDownload(double latMin, double lonMin, double latMax, double lonMax) {
    if(m_plugin) m_plugin->Internal_StartDownload(latMin, lonMin, latMax, lonMax);
}
}
EOT
fi

# 7. Perform Search and Replace in Code
echo -e "7. Refactoring code references (grib_pi -> DpGrib_pi)..."

# Replace include "grib_pi.h" with "DpGrib_pi.h"
find "${DEST_DIR}/src" "${DEST_DIR}/include" -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's/#include "grib_pi.h"/#include "DpGrib_pi.h"/g' {} +

# Replace class name "grib_pi" with "DpGrib_pi"
# Note: We use word boundaries \b to avoid replacing things like "grib_pi_plugin" if they exist, though standard is usually safe.
find "${DEST_DIR}/src" "${DEST_DIR}/include" -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's/\bgrib_pi\b/DpGrib_pi/g' {} +

# 8. Clean up template artifacts (Optional but recommended)
# Remove the template API files if they exist to avoid confusion
rm -f "${DEST_DIR}/src/DpTemplateAPI.cpp"
rm -f "${DEST_DIR}/include/DpTemplateAPI.h"
rm -f "${DEST_DIR}/src/deepreytemplate_pi.cpp"
rm -f "${DEST_DIR}/include/deepreytemplate_pi.h"


echo -e "${GREEN}Migration Complete!${NC}"
echo -e "Next steps:"
echo -e "1. Open ${DEST_DIR}/CMakeLists.txt"
echo -e "2. Update project name to 'dpgrib'"
echo -e "3. Ensure include directories point to '\${CMAKE_CURRENT_SOURCE_DIR}/include' and '\${CMAKE_CURRENT_SOURCE_DIR}/libs/bzip2'"

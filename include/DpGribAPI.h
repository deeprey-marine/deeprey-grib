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

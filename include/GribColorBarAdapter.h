#ifndef GRIB_COLOR_BAR_ADAPTER_H
#define GRIB_COLOR_BAR_ADAPTER_H

// Bridges the shared DpColorBar widget to the GRIB plugin's TexFont text
// renderer. The widget draws the panel + gradient + icon itself; only text is
// delegated. Two fonts are held so the widget's `emphasis` flag selects the bold
// title font vs the normal label font (matching deepview's bold-12 / normal-11).

#include "DpColorBar.h"

class TexFont;

class GribTexFontColorBarText : public dp::IDpColorBarText {
 public:
  GribTexFontColorBarText(TexFont& normal, TexFont& bold)
      : m_normal(normal), m_bold(bold) {}

  void Measure(const std::string& s, bool emphasis, int* w, int* h) override;
  void Draw(const std::string& s, int x, int y, bool emphasis, uint8_t r,
            uint8_t g, uint8_t b, uint8_t a) override;

 private:
  TexFont& m_normal;
  TexFont& m_bold;
};

#endif  // GRIB_COLOR_BAR_ADAPTER_H

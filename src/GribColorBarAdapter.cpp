#include "GribColorBarAdapter.h"

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "pi_gl.h"
#include "pi_TexFont.h"

void GribTexFontColorBarText::Measure(const std::string& s, bool emphasis,
                                      int* w, int* h) {
  TexFont& f = emphasis ? m_bold : m_normal;
  f.GetTextExtent(wxString::FromUTF8(s.c_str()), w, h);
}

void GribTexFontColorBarText::Draw(const std::string& s, int x, int y,
                                   bool emphasis, uint8_t r, uint8_t g,
                                   uint8_t b, uint8_t a) {
#ifndef USE_ANDROID_GLES2
  TexFont& f = emphasis ? m_bold : m_normal;
  // TexFont glyph atlas is GL_ALPHA, so the current colour tints (and fades) it.
  glColor4ub(r, g, b, a);
  glEnable(GL_TEXTURE_2D);
  f.RenderString(wxString::FromUTF8(s.c_str()), x, y);
  glDisable(GL_TEXTURE_2D);
#else
  (void)s;
  (void)x;
  (void)y;
  (void)emphasis;
  (void)r;
  (void)g;
  (void)b;
  (void)a;
#endif
}

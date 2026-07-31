/*
 * WIREFIGHT 32X - framebuffer drawing primitives.
 *
 * All drawing goes to the 32X (non displayed) frame buffer in 256
 * colour mode, using the overwrite image for single pixel plots and
 * word writes to the direct image for spans/rects.
 */
#ifndef WF_GFX_H
#define WF_GFX_H

#include <stdint.h>

/* screen geometry: the game renders 320x180 letterboxed in 320x224 */
#define WF_VIEW_W 320
#define WF_VIEW_H 180

void gfx_pset(int x, int y, uint8_t col);
/* absolute, pre-offset variant for the full 320x224 screen */
#if defined(WF_TESTBUILD) || defined(WF_HOST)
/* white-box draw stats for the on-device test suite (COMM-published) */
extern int wf_dbg_plots, wf_dbg_lines, wf_dbg_maxc;
#define DBG_PLOT()  (wf_dbg_plots++)
#define DBG_LINE(a,b,c,d) do { \
    int m_ = (a) < 0 ? -(a) : (a); \
    if (((b) < 0 ? -(b) : (b)) > m_) m_ = (b) < 0 ? -(b) : (b); \
    if (((c) < 0 ? -(c) : (c)) > m_) m_ = (c) < 0 ? -(c) : (c); \
    if (((d) < 0 ? -(d) : (d)) > m_) m_ = (d) < 0 ? -(d) : (d); \
    wf_dbg_lines++; if (m_ > wf_dbg_maxc) wf_dbg_maxc = m_; } while (0)
#else
#define DBG_PLOT()
#define DBG_LINE(a,b,c,d)
#endif

void gfx_pset_abs(int x, int y, uint8_t col);
void gfx_line(int x1, int y1, int x2, int y2, uint8_t col);
void gfx_rect(int x, int y, int w, int h, uint8_t col);
void gfx_rectb(int x, int y, int w, int h, uint8_t col);

/* 5x7 text, one pixel spacing */
void gfx_text(int x, int y, const char *s, uint8_t col);
/* big outlined text, 5x7 scaled by 'scale' with per-row gradient */
void gfx_bigtext(int x, int y, const char *s, int scale, const uint8_t grad[7], uint8_t outline_col);

uint32_t gfx_rand(void);
void gfx_srand(uint32_t s);

#endif /* WF_GFX_H */

/*
 * Host shim shared by the host-side tests: implements the Mars_* API
 * against a software framebuffer with 32X-like bank/line-table layout.
 */
#ifndef WF_HOST_SHIM_H
#define WF_HOST_SHIM_H

#include <stdint.h>
#include <string.h>

#define WF_HOST 1
#include "../../src-sh2/marsl.h"

/* ---- software framebuffer ---------------------------------------------- */
static uint16_t wf_fbmem[2][65536];      /* two 128KB banks as words */
static int wf_fb_active;                 /* front bank; back = ^1 */

volatile uint16_t *host_fb_direct, *host_fb_overw;

static inline void wf_select_back(void)
{
    host_fb_direct = wf_fbmem[wf_fb_active ^ 1];
    host_fb_overw  = wf_fbmem[wf_fb_active ^ 1];
}

static inline void wf_host_boot_video(void)
{
    int j, b;
    for (b = 0; b < 2; b++) {
        uint16_t *lines = wf_fbmem[b];
        for (j = 0; j < 256; j++)
            lines[j] = (uint16_t)(j * (SCREEN_W / 2) + 0x100);
    }
    wf_fb_active = 0;
    wf_select_back();
}

/* ---- Mars_* implementations --------------------------------------------- */
static uint8_t wf_host_pal[256 * 3];
volatile unsigned mars_vblank_count;
volatile unsigned mars_pwdt_ovf_count, mars_swdt_ovf_count;
uint16_t mars_refresh_hz = 60;
volatile uint16_t mars_pad[2];

void Mars_Init(void) {}
void Mars_InitVideo(void) { wf_host_boot_video(); }

void Mars_FlipFrameBuffers(char wait)
{
    (void)wait;
    wf_fb_active ^= 1;
    wf_select_back();
}
void Mars_WaitFrameBuffersFlip(void) {}
char Mars_FramebuffersFlipped(void) { return 1; }
int  Mars_BackBuffer(void) { return wf_fb_active; }

void Mars_SetPalette(const uint8_t *palette) { memcpy(wf_host_pal, palette, 256 * 3); }
uint16_t *Mars_FrameBufferLines(void) { return wf_fbmem[wf_fb_active ^ 1]; }

void Mars_InitLineTable(void) { wf_host_boot_video(); }
void Mars_WaitFill(void) {}
char Mars_FillBusy(void) { return 0; }
void Mars_FillLine(int y, int x0, int x1, uint8_t color)
{
    uint16_t *line = wf_fbmem[wf_fb_active ^ 1] + wf_fbmem[wf_fb_active ^ 1][y];
    uint16_t fill = (uint16_t)(color | (color << 8));
    int i;
    for (i = x0 >> 1; i < x1 >> 1; i++) line[i] = fill;
}
void Mars_ClearScreen(uint8_t color)
{
    uint16_t fill = (uint16_t)(color | (color << 8));
    int y, i;
    for (y = 0; y < FRAMEBUFFER_HEIGHT; y++) {
        uint16_t *line = wf_fbmem[wf_fb_active ^ 1] + wf_fbmem[wf_fb_active ^ 1][y];
        for (i = 0; i < SCREEN_W / 2; i++)
            line[i] = fill;
    }
}
void Mars_WaitTicks(int ticks) { (void)ticks; }

void pri_vbi_handler(void) {}
void pri_cmd_handler(void) {}
void sec_cmd_handler(void) {}
void sec_dma1_handler(void) {}
void CacheControl(int mode) { (void)mode; }

/* sound */
void snd_init(void) {}
void snd_bgm(int song) { (void)song; }
void snd_play(int ch, int id) { (void)ch; (void)id; }
void snd_slave(void) {}

/* dump helpers */
static inline void wf_host_dump_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    uint16_t *front = wf_fbmem[wf_fb_active];
    int x, y;
    fprintf(fp, "P6\n%d %d\n255\n", SCREEN_W, FRAMEBUFFER_HEIGHT);
    for (y = 0; y < FRAMEBUFFER_HEIGHT; y++) {
        uint16_t *line = front + front[y];
        for (x = 0; x < SCREEN_W; x++) {
            uint16_t w = line[x >> 1];
            uint8_t px = (x & 1) ? (w & 0xFF) : (w >> 8);
            uint8_t *rgb = &wf_host_pal[px * 3];
            fputc(rgb[0], fp); fputc(rgb[1], fp); fputc(rgb[2], fp);
        }
    }
    fclose(fp);
}

#endif /* WF_HOST_SHIM_H */
